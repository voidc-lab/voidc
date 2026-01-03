//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_compiler.h"

#include "voidc_types.h"
#include "voidc_target.h"


//=====================================================================
//- AST Visitor - Compiler (level 0) ...
//=====================================================================
static
void compile_stmt_list(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &list = static_cast<const ast_stmt_list_data_t &>(**self);

    for (auto &it : list.data)
    {
        voidc_visitor_data_t::visit(*vis, it);
    }
}

static
void compile_expr_list(void *, const visitor_t *vis, const ast_base_t *self)
{
    assert(false);      //- Sic!!!
}


//---------------------------------------------------------------------
//- unit
//---------------------------------------------------------------------
static
void compile_unit(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &unit = static_cast<const ast_unit_data_t &>(**self);

    if (!unit.stmt_list)  return;

    auto saved_target = voidc_global_ctx_t::target;

    voidc_global_ctx_t::target = voidc_global_ctx_t::voidc;

    auto &gctx = *voidc_global_ctx_t::target;                           //- target == voidc (!)
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);     //- Sic!

    auto saved_module = lctx.module;

    lctx.prepare_unit_action(unit.line, unit.column);

    voidc_visitor_data_t::visit(*vis, unit.stmt_list);

    lctx.finish_unit_action();

    lctx.module = saved_module;

    voidc_global_ctx_t::target = saved_target;
}


//---------------------------------------------------------------------
//- stmt
//---------------------------------------------------------------------
static
void compile_stmt(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &stmt = static_cast<const ast_stmt_data_t &>(**self);

    if (!stmt.expr) return;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_temporaries();

    lctx.result_type = INVIOLABLE_TAG;

    //- Don't touch lctx.result_value !..

    voidc_visitor_data_t::visit(*vis, stmt.expr);

    auto tr = lctx.result_type;
    auto vr = lctx.result_value;

    lctx.pop_temporaries();

    auto const *ret_name = v_quark_to_string(stmt.name);

    if (ret_name  &&  ret_name[0])
    {
        if (tr->llvm_type())
        {
            size_t len = 0;

            LLVMGetValueName2(vr, &len);

            if (len == 0)
            {
                LLVMSetValueName2(vr, ret_name, v_quark_to_string_size(stmt.name));
            }
        }

        lctx.vars = lctx.vars.set(stmt.name, {tr, vr});
    }
}


//---------------------------------------------------------------------
//- expr_call
//---------------------------------------------------------------------
static
void compile_expr_call(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &call = static_cast<const ast_expr_call_data_t &>(**self);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    void *void_fun = nullptr;
    void *void_aux;

    if (auto fname = std::dynamic_pointer_cast<const ast_expr_identifier_data_t>(call.fun_expr))
    {
        if (auto p = lctx.decls.intrinsics.find(fname->name))
        {
            void_fun = p->first;
            void_aux = p->second;
        }
    }

    if (void_fun)       //- Compile-time intrinsic?
    {
        typedef void (*intrinsic_t)(void *aux, const visitor_t *vis, const ast_base_t *self);

        reinterpret_cast<intrinsic_t>(void_fun)(void_aux, vis, self);

        return;
    }

    //- Function call

    lctx.push_result();

    lctx.result_type = UNREFERENCE_TAG;

    voidc_visitor_data_t::visit(*vis, call.fun_expr);

    v_type_t    *t = lctx.result_type;
    LLVMValueRef f = lctx.result_value;


    if (auto *ft = dynamic_cast<v_type_pointer_t *>(t))
    {
        t = ft->element_type();
    }

    auto ft = static_cast<v_type_function_t *>(t);

    auto par_count = ft->param_count();
    auto par_types = ft->param_types();

    auto &args_data = call.arg_list->data;
    auto  arg_count = args_data.size();


    auto values = std::make_unique<LLVMValueRef[]>(arg_count);      //- TODO(?)

    for (int i=0; i<arg_count; ++i)
    {
        lctx.reset_result();

        if (i < par_count)  lctx.result_type = par_types[i];
        else                lctx.result_type = UNREFERENCE_TAG;

        voidc_visitor_data_t::visit(*vis, args_data[i]);

        values[i] = lctx.result_value;
    }

    auto v = LLVMBuildCall2(gctx.builder, ft->llvm_type(), f, values.get(), arg_count, "");


    lctx.pop_result();

    lctx.adopt_result(ft->return_type(), v);
}


//---------------------------------------------------------------------
//- expr_identifier
//---------------------------------------------------------------------
static
void compile_expr_identifier(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &ident = static_cast<const ast_expr_identifier_data_t &>(**self);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    LLVMValueRef v = nullptr;
    v_type_t    *t = nullptr;

    if (!lctx.obtain_identifier(ident.name, t, v))
    {
        throw std::runtime_error(std::string("Identifier not found: ") + v_quark_to_string(ident.name));
    }

    auto &vctx = *voidc_global_ctx_t::voidc;

    if (&gctx == &vctx  &&  t == vctx.static_type_type  &&  lctx.result_type == vctx.type_ptr_type)
    {
        auto raw_name = lctx.lookup_alias(ident.name);

        t = lctx.get_symbol_type(raw_name);

        assert(t == vctx.type_type);

        auto cname = v_quark_to_string(raw_name);

        v = LLVMGetNamedGlobal(lctx.module, cname);

        if (!v) v = LLVMAddGlobal(lctx.module, t->llvm_type(), cname);

        t = vctx.type_ptr_type;
    }

    lctx.adopt_result(t, v);
}


//---------------------------------------------------------------------
//- expr_integer
//---------------------------------------------------------------------
static
void compile_expr_integer(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &dnum = static_cast<const ast_expr_integer_data_t &>(**self);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto t = lctx.result_type;

    LLVMValueRef v;

    auto num = dnum.number;

    static const unsigned tot_bits = 8*sizeof(long long);

    unsigned w = tot_bits - __builtin_clrsbll(num);

    if (t == INVIOLABLE_TAG  ||  t == UNREFERENCE_TAG)
    {
        t = gctx.int_type;

        if (w > static_cast<v_type_integer_t *>(t)->width())
        {
            t = gctx.intptr_t_type;
        }

        v = LLVMConstInt(t->llvm_type(), (long long)num, true);

        lctx.result_type  = t;
        lctx.result_value = v;
    }
    else
    {
        bool is_reference = (t->kind() == v_type_t::k_reference);

        if (is_reference) t = static_cast<v_type_reference_t *>(t)->element_type();

        if (num == 0)
        {
            v = LLVMConstNull(t->llvm_type());      //- Sic!!!
        }
        else
        {
            assert(w);      //- Sic!

            if (num > 0)    t = gctx.make_uint_type(w-1);
            else            t = gctx.make_int_type(w);

            v = LLVMConstInt(t->llvm_type(), (long long)num, true);
        }

        lctx.adopt_result(t, v);
    }
}


//---------------------------------------------------------------------
//- expr_string
//---------------------------------------------------------------------
static
void compile_expr_string(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &dstr = static_cast<const ast_expr_string_data_t &>(**self);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto &str = dstr.string;

    auto len = str.size();

    auto val = LLVMConstStringInContext(gctx.llvm_ctx, str.c_str(), unsigned(len), 0);

    auto typ = gctx.make_array_type(gctx.char_type, len+1);

    lctx.adopt_result(typ, val);
}


//---------------------------------------------------------------------
//- expr_char
//---------------------------------------------------------------------
static
void compile_expr_char(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &dchr = static_cast<const ast_expr_char_data_t &>(**self);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto t = gctx.char32_t_type;

    auto v = LLVMConstInt(t->llvm_type(), dchr.char_, false);

    lctx.adopt_result(t, v);
}


//---------------------------------------------------------------------
//- expr_compiled
//---------------------------------------------------------------------
static
void compile_expr_compiled(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &dtv = static_cast<const ast_expr_compiled_data_t &>(**self);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto t = (v_type_t *)  dtv.void_type;
    auto v = (LLVMValueRef)dtv.void_value;

    lctx.adopt_result(t, v);
}


//=====================================================================
//- Intrinsics (true)
//=====================================================================
static void
v_alloca(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &call = static_cast<const ast_expr_call_data_t &>(**self);

    auto &args = call.arg_list;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_result();

    lctx.result_type = gctx.static_type_type;

    voidc_visitor_data_t::visit(*vis, args->data[0]);               //- Element type

    auto type = reinterpret_cast<v_type_t *>(lctx.result_value);

    auto llvm_type = type->llvm_type();

    LLVMValueRef v;

    if (args->data.size() == 1)                 //- Just one
    {
        v = LLVMBuildAlloca(gctx.builder, llvm_type, "");
    }
    else                                        //- Array...
    {
        lctx.reset_result();

        lctx.result_type = UNREFERENCE_TAG;

        voidc_visitor_data_t::visit(*vis, args->data[1]);               //- Array size

        v = LLVMBuildArrayAlloca(gctx.builder, llvm_type, lctx.result_value, "");
    }

    auto t = gctx.make_pointer_type(type, LLVMGetPointerAddressSpace(LLVMTypeOf(v)));

    lctx.pop_result();

    lctx.adopt_result(t, v);
}

//---------------------------------------------------------------------
static void
v_getelementptr(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &call = static_cast<const ast_expr_call_data_t &>(**self);

    auto &args = call.arg_list;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_result();

    auto arg_count = args->data.size();

    auto types  = std::make_unique<v_type_t *[]>(arg_count);
    auto values = std::make_unique<LLVMValueRef[]>(arg_count);

    for (int i=0; i<arg_count; ++i)
    {
        if (i > 0)  lctx.reset_result();

        lctx.result_type = UNREFERENCE_TAG;

        voidc_visitor_data_t::visit(*vis, args->data[i]);

        auto ti = lctx.result_type;
        auto vi = lctx.result_value;

        if (auto *sti = dynamic_cast<v_type_uint_t *>(v_type_get_scalar_type(ti)))
        {
            if (auto w = sti->width();  w < 32)
            {
                v_type_t *t1 = gctx.make_uint_type(32);

                if (auto *vt = dynamic_cast<v_type_vector_t *>(ti))
                {
                    auto count = vt->size();

                    if (vt->is_scalable())  t1 = gctx.make_svector_type(t1, count);
                    else                    t1 = gctx.make_vector_type(t1, count);
                }

                lctx.reset_result();

                lctx.result_type = t1;

                lctx.adopt_result(ti, vi);

                ti = lctx.result_type;
                vi = lctx.result_value;
            }
        }

        types[i]  = ti;
        values[i] = vi;
    }

    auto *p = static_cast<v_type_pointer_t *>(v_type_get_scalar_type(types[0]));

    v_type_t *t = p->element_type();

    auto v = LLVMBuildGEP2(gctx.builder, t->llvm_type(), values[0], &values[1], arg_count-1, "");

    for (int i=2; i<arg_count; ++i)
    {
        if (auto *ti = dynamic_cast<v_type_array_t *>(t))
        {
            t = ti->element_type();
            continue;
        }

        if (auto *ti = dynamic_cast<v_type_vector_t *>(t))
        {
            t = ti->element_type();
            continue;
        }

        if (auto *ti = dynamic_cast<v_type_struct_t *>(t))
        {
            auto idx = values[i];

            auto num = unsigned(LLVMConstIntGetZExtValue(idx));

            t = ti->element_types()[num];
            continue;
        }

        assert(false && "Wrong GEP index");

        t = nullptr;
        break;
    }

    t = gctx.make_pointer_type(t, p->address_space());

    for (int i=0; i<arg_count; ++i)
    {
        if (auto *vt = dynamic_cast<v_type_vector_t *>(types[i]))
        {
            auto count = vt->size();

            if (vt->is_scalable())  t = gctx.make_svector_type(t, count);
            else                    t = gctx.make_vector_type(t, count);

            break;
        }
    }

    lctx.pop_result();

    lctx.adopt_result(t, v);
}

//---------------------------------------------------------------------
static void
v_store(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &call = static_cast<const ast_expr_call_data_t &>(**self);

    auto &args = call.arg_list;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.reset_result();                //- Sic !!!

    lctx.result_type = UNREFERENCE_TAG;

    voidc_visitor_data_t::visit(*vis, args->data[1]);               //- "Place"

    auto place = lctx.result_value;

    auto et = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    lctx.reset_result();

    lctx.result_type = et;

    voidc_visitor_data_t::visit(*vis, args->data[0]);               //- "Value"

    LLVMBuildStore(gctx.builder, lctx.result_value, place);
}

//---------------------------------------------------------------------
static void
v_load(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &call = static_cast<const ast_expr_call_data_t &>(**self);

    auto &args = call.arg_list;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_result();

    lctx.result_type = UNREFERENCE_TAG;

    voidc_visitor_data_t::visit(*vis, args->data[0]);

    auto t = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    auto v = LLVMBuildLoad2(gctx.builder, t->llvm_type(), lctx.result_value, "");

    lctx.pop_result();

    lctx.adopt_result(t, v);
}

//---------------------------------------------------------------------
static void
v_cast(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &call = static_cast<const ast_expr_call_data_t &>(**self);

    auto &args = call.arg_list;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_result();

//  lctx.result_type = INVIOLABLE_TAG;

    voidc_visitor_data_t::visit(*vis, args->data[0]);               //- Value

    auto src_type  = lctx.result_type;
    auto src_value = lctx.result_value;

    lctx.reset_result();

    lctx.result_type = gctx.static_type_type;

    voidc_visitor_data_t::visit(*vis, args->data[1]);               //- Type

    auto dst_type = reinterpret_cast<v_type_t *>(lctx.result_value);

    auto opcode = LLVMOpcode(0);

    if (auto *pst = dynamic_cast<v_type_reference_t *>(src_type))
    {
        if (auto *pdt = dynamic_cast<v_type_reference_t *>(dst_type))
        {
            //- WTF ?!?!?!?!?!?!?

            if (pst->address_space() == pdt->address_space())   opcode = LLVMBitCast;
            else                                                opcode = LLVMAddrSpaceCast;
        }
        else if (pst->element_type()->kind() == v_type_t::k_array  &&
                 dst_type->kind() == v_type_t::k_pointer
                )
        {
            //- C-like array-to-pointer "promotion" (from reference)...

            auto et = static_cast<v_type_array_t *>(pst->element_type())->element_type();

            src_type  = gctx.make_pointer_type(et, pst->address_space());

            src_value = LLVMBuildPointerCast(gctx.builder, src_value, src_type->llvm_type(), "");
        }
        else
        {
            src_type  = pst->element_type();

            src_value = LLVMBuildLoad2(gctx.builder, src_type->llvm_type(), src_value, "tmp");
        }
    }

    LLVMValueRef v;

    if (!opcode)
    {
        if (src_type->kind() == v_type_t::k_array  &&
            dst_type->kind() == v_type_t::k_pointer
           )
        {
            //- C-like array-to-pointer "promotion" (from value)...

            LLVMValueRef v1;

            if (LLVMIsConstant(src_value))
            {
                v1 = LLVMAddGlobal(lctx.module, src_type->llvm_type(), "arr");

                LLVMSetInitializer(v1, src_value);

                LLVMSetLinkage(v1, LLVMPrivateLinkage);

                LLVMSetUnnamedAddress(v1, LLVMGlobalUnnamedAddr);

                LLVMSetGlobalConstant(v1, true);
            }
            else
            {
                v1 = lctx.make_temporary(src_type, src_value);
            }

            v = LLVMBuildPointerCast(gctx.builder, v1, dst_type->llvm_type(), "");
        }
        else
        {
            auto src_stype = v_type_get_scalar_type(src_type);
            auto dst_stype = v_type_get_scalar_type(dst_type);

            if (v_type_is_floating_point(src_stype))
            {
                if (v_type_is_floating_point(dst_stype))
                {
                    auto sz = v_type_floating_point_get_width(src_stype);
                    auto dz = v_type_floating_point_get_width(dst_stype);

                    if (sz == dz)       opcode = LLVMBitCast;       //- ?
                    else if (sz > dz)   opcode = LLVMFPTrunc;
                    else                opcode = LLVMFPExt;
                }
                else if (auto *idt = dynamic_cast<v_type_integer_t *>(dst_stype))
                {
                    if (idt->is_signed()) opcode = LLVMFPToSI;
                    else                  opcode = LLVMFPToUI;
                }
                else
                {
                    throw std::runtime_error("Bad cast from floating point");
                }
            }
            else if (auto *ist = dynamic_cast<v_type_integer_t *>(src_stype))
            {
                if (v_type_is_floating_point(dst_stype))
                {
                    if (ist->is_signed()) opcode = LLVMSIToFP;
                    else                  opcode = LLVMUIToFP;
                }
                else if (auto *idt = dynamic_cast<v_type_integer_t *>(dst_stype))
                {
                    auto sw = ist->width();
                    auto dw = idt->width();

                    if (sw == dw)       opcode = LLVMBitCast;       //- ?
                    else if (sw > dw)   opcode = LLVMTrunc;
                    else
                    {
                        if (ist->is_signed()) opcode = LLVMSExt;        //- ?
                        else                  opcode = LLVMZExt;        //- ?
                    }
                }
                else if (auto *pdt = dynamic_cast<v_type_pointer_t *>(dst_stype))
                {
                    opcode = LLVMIntToPtr;
                }
                else
                {
                    throw std::runtime_error("Bad cast from integer");
                }
            }
            else if (auto *pst = dynamic_cast<v_type_pointer_t *>(src_stype))
            {
                if (auto *idt = dynamic_cast<v_type_integer_t *>(dst_stype))
                {
                    opcode = LLVMPtrToInt;
                }
                else if (auto *pdt = dynamic_cast<v_type_pointer_t *>(dst_stype))
                {
                    if (pst->address_space() == pdt->address_space())   opcode = LLVMBitCast;
                    else                                                opcode = LLVMAddrSpaceCast;
                }
                else
                {
                    throw std::runtime_error("Bad cast from pointer");
                }
            }
            else
            {
                auto &vctx = *voidc_global_ctx_t::voidc;

                if (&gctx == &vctx  &&
                    src_stype == vctx.static_type_type  &&
                    dynamic_cast<v_type_pointer_t *>(dst_stype))
                {
                    const v_quark_t *pq = nullptr;

                    static const v_quark_t typenames_q = v_quark_from_string("voidc.typenames_dict");

                    if (auto *i = lctx.decls.overloads.find(typenames_q))           //- WTF ?!?!?!?!?!?!?
                    {
                        pq = i->find(reinterpret_cast<v_type_t *>(src_value));
                    }

                    if (pq)
                    {
                        auto q = *pq;

                        auto t = lctx.get_symbol_type(q);

                        assert(t == vctx.type_type);

                        auto cname = v_quark_to_string(q);

                        v = LLVMGetNamedGlobal(lctx.module, cname);

                        if (!v) v = LLVMAddGlobal(lctx.module, t->llvm_type(), cname);

                        assert(!opcode);
                    }
                    else
                    {
                        throw std::runtime_error("Bad cast from static type");
                    }
                }
                else
                {
                    throw std::runtime_error("Bad cast");
                }
            }

            if (opcode) v = LLVMBuildCast(gctx.builder, opcode, src_value, dst_type->llvm_type(), "");
        }
    }

    lctx.pop_result();

    lctx.adopt_result(dst_type, v);
}

//---------------------------------------------------------------------
static void
v_pointer(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &call = static_cast<const ast_expr_call_data_t &>(**self);

    auto &args = call.arg_list;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_result();

//  lctx.result_type = INVIOLABLE_TAG;

    voidc_visitor_data_t::visit(*vis, args->data[0]);

    auto t = lctx.result_type;
    auto v = lctx.result_value;

    if (t->kind() == v_type_t::k_reference)
    {
        auto rt = static_cast<v_type_reference_t *>(lctx.result_type);

        t = gctx.make_pointer_type(rt->element_type(), rt->address_space());
    }
    else
    {
        if (LLVMIsConstant(v))
        {
            auto v1 = LLVMAddGlobal(lctx.module, t->llvm_type(), "tmp");

            LLVMSetInitializer(v1, v);

            LLVMSetLinkage(v1, LLVMPrivateLinkage);

            LLVMSetUnnamedAddress(v1, LLVMGlobalUnnamedAddr);

            LLVMSetGlobalConstant(v1, true);

            v = v1;
        }
        else
        {
            v = lctx.make_temporary(t, v);
        }

        t = gctx.make_pointer_type(t, 0);
    }

    lctx.pop_result();

    lctx.adopt_result(t, v);
}

//---------------------------------------------------------------------
static void
v_reference(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &call = static_cast<const ast_expr_call_data_t &>(**self);

    auto &args = call.arg_list;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_result();

    lctx.result_type = UNREFERENCE_TAG;

    voidc_visitor_data_t::visit(*vis, args->data[0]);

    auto pt = static_cast<v_type_pointer_t *>(lctx.result_type);

    auto v = lctx.result_value;
    auto t = gctx.make_reference_type(pt->element_type(), pt->address_space());

    lctx.pop_result();

    lctx.adopt_result(t, v);
}

//---------------------------------------------------------------------
static void
v_assign(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &call = static_cast<const ast_expr_call_data_t &>(**self);

    auto &args = call.arg_list;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_result();

//  lctx.result_type = INVIOLABLE_TAG;

    voidc_visitor_data_t::visit(*vis, args->data[0]);               //- "Place"

    auto type  = lctx.result_type;
    auto place = lctx.result_value;

    lctx.reset_result();

    lctx.result_type = static_cast<v_type_reference_t *>(type)->element_type();

    voidc_visitor_data_t::visit(*vis, args->data[1]);               //- "Value"

    LLVMBuildStore(gctx.builder, lctx.result_value, place);

    lctx.pop_result();

    lctx.adopt_result(type, place);
}


//=====================================================================
//- Compiler visitors (level 0)
//=====================================================================
extern "C"
{
VOIDC_DLLEXPORT_BEGIN_VARIABLE

    visitor_t voidc_internal_target_compiler_level_zero;
    visitor_t voidc_internal_voidc_compiler_level_zero;

VOIDC_DLLEXPORT_END
}

//---------------------------------------------------------------------
visitor_t
make_level_0_voidc_compiler(void)
{
    auto &vis0 = voidc_internal_voidc_compiler_level_zero;

    if (!vis0)
    {
        voidc_visitor_data_t vis;

#define DEF_METHOD(name) \
        vis = vis.set_void_method(v_ast_##name##_tag, (void *)compile_##name);

        DEFINE_AST_TAGS(DEF_METHOD)

        vis0 = std::make_shared<const voidc_visitor_data_t>(vis);
    }

    assert(voidc_internal_voidc_compiler_level_zero);

    return vis0;
}

//---------------------------------------------------------------------
visitor_t
make_level_0_target_compiler(void)
{
    auto &vis0 = voidc_internal_target_compiler_level_zero;

    if (!vis0)
    {
        voidc_visitor_data_t vis;

        DEF_METHOD(stmt_list)
        DEF_METHOD(stmt)
        DEF_METHOD(expr_call)
        DEF_METHOD(expr_identifier)
        DEF_METHOD(expr_integer)
        DEF_METHOD(expr_string)
        DEF_METHOD(expr_char)
        DEF_METHOD(expr_compiled)

        vis0 = std::make_shared<const voidc_visitor_data_t>(vis);
    }

    assert(voidc_internal_target_compiler_level_zero);

    return vis0;
}


#undef DEF_METHOD


//=====================================================================
//- Intrinsics (level 0) ...
//=====================================================================
void
make_level_0_intrinsics(base_global_ctx_t &gctx)
{

#define DEF_INTRINSIC(name) \
    gctx.decls.intrinsics_insert({v_quark_from_string(#name), {(void *)name, nullptr}});

#define DEFINE_INTRINSICS(DEF) \
    DEF(v_alloca)          \
    DEF(v_getelementptr)   \
    DEF(v_store)           \
    DEF(v_load)            \
    DEF(v_cast)            \
    DEF(v_pointer)         \
    DEF(v_reference)       \
    DEF(v_assign)          \

    DEFINE_INTRINSICS(DEF_INTRINSIC)

#undef DEFINE_INTRINSICS
#undef DEF_INTRINSIC

}



