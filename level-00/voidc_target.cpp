//---------------------------------------------------------------------
//- Copyright (C) 2020-2023 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_target.h"

#include <stdexcept>
#include <regex>
#include <cassert>

#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Object.h>
#include <llvm-c/Transforms/PassManagerBuilder.h>
#include <llvm-c/Transforms/IPO.h>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/CBindingWrapping.h>

#include "voidc_compiler.h"


//---------------------------------------------------------------------
using namespace llvm;
using namespace llvm::orc;

DEFINE_SIMPLE_CONVERSION_FUNCTIONS(JITDylib, LLVMOrcJITDylibRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(LLJIT, LLVMOrcLLJITRef)


//---------------------------------------------------------------------
//- Base Compilation Context
//---------------------------------------------------------------------
void
base_compile_ctx_t::declarations_t::insert(const declarations_t &other)
{
    if (other.empty())  return;

    for (auto it : other.aliases)     aliases_insert(it);
    for (auto it : other.constants)   constants_insert(it);
    for (auto it : other.symbols)     symbols_insert(it);
    for (auto it : other.intrinsics)  intrinsics_insert(it);
}


//---------------------------------------------------------------------
//- Base Global Context
//---------------------------------------------------------------------
base_global_ctx_t::base_global_ctx_t(LLVMContextRef ctx, size_t int_size, size_t long_size, size_t ptr_size)
  : voidc_types_ctx_t(ctx, int_size, long_size, ptr_size),
    builder(LLVMCreateBuilderInContext(ctx)),
    char_ptr_type(make_pointer_type(char_type, 0)),         //- address space 0 !?
    void_ptr_type(make_pointer_type(void_type, 0))          //- address space 0 !?
{}

base_global_ctx_t::~base_global_ctx_t()
{
    LLVMDisposeBuilder(builder);
}


//---------------------------------------------------------------------
void
base_global_ctx_t::initialize_type(v_quark_t raw_name, v_type_t *type)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    decls.constants_insert({raw_name, vctx.static_type_type});

    constant_values.insert({raw_name, reinterpret_cast<LLVMValueRef>(type)});

    decls.symbols_insert({raw_name, vctx.type_type});

    add_symbol_value(raw_name, type);
};


//---------------------------------------------------------------------
static int debug_print_module = 0;

void
base_global_ctx_t::verify_module(LLVMModuleRef module)
{
    char *msg = nullptr;

    auto err = LLVMVerifyModule(module, LLVMReturnStatusAction, &msg);

    if (err || debug_print_module)
    {
        char *txt = LLVMPrintModuleToString(module);

        printf("\n%s\n", txt);

        LLVMDisposeMessage(txt);

        printf("\n%s\n", msg);

        if (debug_print_module) debug_print_module -= 1;
    }

    LLVMDisposeMessage(msg);

    if (err)  exit(1);          //- Sic !!!
}


//---------------------------------------------------------------------
void
base_global_ctx_t::initialize(void)
{
    assert(voidc_global_ctx_t::voidc);

    //-----------------------------------------------------------------
    auto q = v_quark_from_string;

    //-----------------------------------------------------------------
#define DEF(name) \
    initialize_type(q(#name), name##_type);

    DEF(void)

    DEF(bool)
    DEF(char)
    DEF(short)
    DEF(int)
    DEF(unsigned)
    DEF(long)
    DEF(long_long)
    DEF(intptr_t)
    DEF(size_t)
    DEF(char32_t)
    DEF(uint64_t)

#undef DEF

    //-----------------------------------------------------------------
    auto add_bool_const = [this, q](const char *_raw_name, bool value)
    {
        auto raw_name = q(_raw_name);

        decls.constants_insert({raw_name, bool_type});

        constant_values.insert({raw_name, LLVMConstInt(bool_type->llvm_type(), value, false)});
    };

    add_bool_const("false", false);
    add_bool_const("true",  true);

    //-----------------------------------------------------------------
    v_type_t *i8_ptr_type = make_pointer_type(make_int_type(8), 0);

    auto stacksave_ft    = make_function_type(i8_ptr_type, nullptr, 0, false);
    auto stackrestore_ft = make_function_type(void_type, &i8_ptr_type, 1, false);

    decls.symbols_insert({q("llvm.stacksave"),    stacksave_ft});
    decls.symbols_insert({q("llvm.stackrestore"), stackrestore_ft});
}


//---------------------------------------------------------------------
//- Base Local Context
//---------------------------------------------------------------------
static LLVMValueRef v_convert_to_type_default(void *, v_type_t *t0, LLVMValueRef v0, v_type_t *t1);

base_local_ctx_t::base_local_ctx_t(base_global_ctx_t &_global)
  : global_ctx(_global),
    parent_ctx(_global.local_ctx),
    convert_to_type_fun(v_convert_to_type_default),
    convert_to_type_ctx(0)
{
    global_ctx.local_ctx = this;

    decls.insert(global_ctx.decls);
}

base_local_ctx_t::~base_local_ctx_t()
{
    global_ctx.local_ctx = parent_ctx;
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_alias(v_quark_t name, v_quark_t raw_name)
{
    if (export_decls)   export_decls->aliases_insert({name, raw_name});

    decls.aliases_insert({name, raw_name});
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_alias(v_quark_t name, v_quark_t raw_name)
{
    decls.aliases_insert({name, raw_name});
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_constant(v_quark_t raw_name, v_type_t *type, LLVMValueRef value)
{
    if (export_decls)   export_decls->constants_insert({raw_name, type});

    decls.constants_insert({raw_name, type});

    if (value)  global_ctx.constant_values.insert({raw_name, value});
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_constant(v_quark_t raw_name, v_type_t *type, LLVMValueRef value)
{
    decls.constants_insert({raw_name, type});

    if (value)  constant_values.insert({raw_name, value});
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_symbol(v_quark_t raw_name, v_type_t *type, void *value)
{
    if (type)
    {
        if (export_decls)   export_decls->symbols_insert({raw_name, type});

        decls.symbols_insert({raw_name, type});
    }

    if (value)  global_ctx.add_symbol_value(raw_name, value);
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_symbol(v_quark_t raw_name, v_type_t *type, void *value)
{
    if (type)   decls.symbols_insert({raw_name, type});

    if (value)  add_symbol_value(raw_name, value);
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_intrinsic(v_quark_t fun_name, void *fun, void *aux)
{
    if (export_decls)   export_decls->intrinsics_insert({fun_name, {fun, aux}});

    decls.intrinsics_insert({fun_name, {fun, aux}});
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_intrinsic(v_quark_t fun_name, void *fun, void *aux)
{
    decls.intrinsics_insert({fun_name, {fun, aux}});
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_type(v_quark_t raw_name, v_type_t *type)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    export_constant(raw_name, vctx.static_type_type, reinterpret_cast<LLVMValueRef>(type));

    export_symbol(raw_name, vctx.type_type, type);
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_type(v_quark_t raw_name, v_type_t *type)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    add_constant(raw_name, vctx.static_type_type, reinterpret_cast<LLVMValueRef>(type));

    add_symbol(raw_name, vctx.type_type, type);
}


//---------------------------------------------------------------------
v_quark_t
base_local_ctx_t::check_alias(v_quark_t name)
{
    if (auto q = decls.aliases.find(name))  return *q;

    return  name;
}

//---------------------------------------------------------------------
v_type_t *
base_local_ctx_t::find_type(v_quark_t type_name)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    if (auto *p = vars.find(type_name))
    {
        if (p->first == vctx.static_type_type)
        {
            return reinterpret_cast<v_type_t *>(p->second);
        }
    }

    auto raw_name = check_alias(type_name);

    {   v_type_t    *t = nullptr;
        LLVMValueRef v = nullptr;

        if (find_constant(raw_name, t, v)  &&  t == vctx.static_type_type)
        {
            return reinterpret_cast<v_type_t *>(v);
        }
    }

    return nullptr;
}


//---------------------------------------------------------------------
bool
base_local_ctx_t::find_constant(v_quark_t raw_name, v_type_t * &type, LLVMValueRef &value)
{
    v_type_t    *t = nullptr;
    LLVMValueRef v = nullptr;

    if (auto p = decls.constants.find(raw_name))
    {
        t = *p;

        for (auto &cv : {constant_values, global_ctx.constant_values})
        {
            auto itv = cv.find(raw_name);

            if (itv != cv.end())
            {
                v = itv->second;

                break;
            }
        }
    }

    if (!t) return false;

    type  = t;
    value = v;

    return true;
}

//---------------------------------------------------------------------
bool
base_local_ctx_t::find_symbol(v_quark_t raw_name, v_type_t * &type, void * &value)
{
    v_type_t *t = nullptr;
    void     *v = nullptr;

    t = get_symbol_type(raw_name);

    if (!t) return false;

    v = find_symbol_value(raw_name);

    type  = t;
    value = v;

    return true;
}


//---------------------------------------------------------------------
bool
base_local_ctx_t::obtain_identifier(v_quark_t name, v_type_t * &type, LLVMValueRef &value)
{
    v_type_t    *t = nullptr;
    LLVMValueRef v = nullptr;

    if (auto *p = vars.find(name))
    {
        t = p->first;
        v = p->second;
    }
    else
    {
        auto raw_name = check_alias(name);

        auto cname = v_quark_to_string(raw_name);

        if (!find_constant(raw_name, t, v))
        {
            t = get_symbol_type(raw_name);

            if (!t)  return false;

            if (!(module  ||  (module = obtain_module())))  return false;

            assert(module);

            if (auto *ft = dynamic_cast<v_type_function_t *>(t))
            {
                v = LLVMGetNamedFunction(module, cname);

                if (!v) v = LLVMAddFunction(module, cname, ft->llvm_type());

                t = global_ctx.make_pointer_type(t, 0);         //- Sic!
            }
            else
            {
                v = LLVMGetNamedGlobal(module, cname);

                v_type_t *st = t;

                bool is_reference = (t->kind() == v_type_t::k_reference);

                if (is_reference) st = static_cast<v_type_reference_t *>(st)->element_type();

                if (!v) v = LLVMAddGlobal(module, st->llvm_type(), cname);

                if (!is_reference)  t = global_ctx.make_pointer_type(t, 0);         //- Sic!
            }
        }
    }

    assert(t);

    type  = t;
    value = v;

    return true;
}


//---------------------------------------------------------------------
void
base_local_ctx_t::push_builder_ip(void)
{
    builder_ip_stack.push_front(unwrap(global_ctx.builder)->saveIP());
}

//---------------------------------------------------------------------
void
base_local_ctx_t::pop_builder_ip(void)
{
    auto &top = builder_ip_stack.front();

    unwrap(global_ctx.builder)->restoreIP(top);

    builder_ip_stack.pop_front();
}


//---------------------------------------------------------------------
static v_quark_t voidc_internal_return_type_q;
static v_quark_t voidc_internal_return_value_q;
static v_quark_t voidc_internal_branch_target_leave_q;

//---------------------------------------------------------------------
LLVMValueRef
base_local_ctx_t::prepare_function(const char *name, v_type_t *type)
{
    LLVMValueRef f = LLVMGetNamedFunction(module, name);        //- Sic!

    if (!f)  f = LLVMAddFunction(module, name, type->llvm_type());

    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(f, "entry");

    LLVMPositionBuilderAtEnd(global_ctx.builder, entry);

    vars = variables_t();       //- ?

    push_variables();

    auto ret_type = static_cast<v_type_function_t *>(type)->return_type();

    vars = vars.set(voidc_internal_return_type_q, {ret_type, nullptr});

    if (ret_type->kind() != v_type_t::k_void)
    {
        auto ret_value = LLVMBuildAlloca(global_ctx.builder, ret_type->llvm_type(), "ret_value");

        vars = vars.set(voidc_internal_return_value_q, {nullptr, ret_value});   //- Sic!
    }

    function_leave_b = LLVMAppendBasicBlock(f, "f_leave_b");
    auto f_leave_bv  = LLVMBasicBlockAsValue(function_leave_b);

    vars = vars.set(voidc_internal_branch_target_leave_q, {nullptr, f_leave_bv});   //- Sic!

    return f;
}

//---------------------------------------------------------------------
void
base_local_ctx_t::finish_function(void)
{
    auto cur_b = LLVMGetInsertBlock(global_ctx.builder);

    if (!LLVMGetBasicBlockTerminator(cur_b))
    {
        auto leave_bv = vars[voidc_internal_branch_target_leave_q].second;
        auto leave_b  = LLVMValueAsBasicBlock(leave_bv);

        LLVMBuildBr(global_ctx.builder, leave_b);
    }


    LLVMMoveBasicBlockAfter(function_leave_b, cur_b);

    LLVMPositionBuilderAtEnd(global_ctx.builder, function_leave_b);


    auto ret_type = vars[voidc_internal_return_type_q].first;

    if (ret_type->kind() == v_type_t::k_void)
    {
        LLVMBuildRetVoid(global_ctx.builder);
    }
    else
    {
        auto ret_var_v = vars[voidc_internal_return_value_q].second;
        auto ret_value = LLVMBuildLoad2(global_ctx.builder, ret_type->llvm_type(), ret_var_v, "ret_value");

        LLVMBuildRet(global_ctx.builder, ret_value);
    }

    LLVMClearInsertionPosition(global_ctx.builder);

    pop_variables();
}


//---------------------------------------------------------------------
void
base_local_ctx_t::adopt_result(v_type_t *type, LLVMValueRef value)
{
    bool src_ref = (type->kind() == v_type_t::k_reference);

    switch(intptr_t(result_type))
    {
    case -1:        //- Unreference...

        if (src_ref)
        {
            auto et = static_cast<v_type_reference_t *>(type)->element_type();

            if (et->kind() == v_type_t::k_array)
            {
                //- Special case for C-like array-to-pointer "promotion"...

                et = static_cast<v_type_array_t *>(et)->element_type();

                result_type = global_ctx.make_pointer_type(et, 0);

                value = convert_to_type(type, value, result_type);

                break;
            }

            result_type = et;

            value = LLVMBuildLoad2(global_ctx.builder, et->llvm_type(), value, "tmp");

            break;
        }

        if (type->kind() == v_type_t::k_array)
        {
            //- Special case for C-like array-to-pointer "promotion"...

            auto et = static_cast<v_type_array_t *>(type)->element_type();

            result_type = global_ctx.make_pointer_type(et, 0);

            value = convert_to_type(type, value, result_type);

            break;
        }

        //- Fallthrough!

    case  0:        //- Get "as is"...

        result_type = type;

        break;

    default:        //- Adopt...

        if (result_type == type)  break;

        bool dst_ref = (result_type->kind() == v_type_t::k_reference);

        auto dst_typ = result_type;

        if (dst_ref)  dst_typ = static_cast<v_type_reference_t *>(dst_typ)->element_type();

        auto src_typ = type;

        if (src_ref)
        {
            src_typ = static_cast<v_type_reference_t *>(src_typ)->element_type();

            if (src_typ->kind() == v_type_t::k_array  &&
                dst_typ->kind() == v_type_t::k_pointer)
            {
                auto n0 = LLVMConstNull(global_ctx.int_type->llvm_type());

                LLVMValueRef val[2] = { n0, n0 };

                value = LLVMBuildInBoundsGEP2(global_ctx.builder, src_typ->llvm_type(), value, val, 2, "");

                auto et = static_cast<v_type_array_t *>(src_typ)->element_type();

                auto as = static_cast<v_type_reference_t *>(type)->address_space();

                src_typ = global_ctx.make_pointer_type(et, as);
            }
            else
            {
                value = LLVMBuildLoad2(global_ctx.builder, src_typ->llvm_type(), value, "tmp");
            }
        }

        if (src_typ != dst_typ)
        {
            if (dst_typ == global_ctx.void_ptr_type  &&
                src_typ->kind() == v_type_t::k_pointer)
            {
                value = LLVMBuildPointerCast(global_ctx.builder, value, dst_typ->llvm_type(), "");
            }
            else
            {
                value = convert_to_type(src_typ, value, dst_typ);       //- "generic" ...
            }
        }

        if (dst_ref)  value = make_temporary(dst_typ, value);
    }

    result_value = value;
}


//---------------------------------------------------------------------
void
base_local_ctx_t::push_variables(void)
{
    vars_stack.push_front({decls, vars});
}

//---------------------------------------------------------------------
void
base_local_ctx_t::pop_variables(void)
{
    auto &top = vars_stack.front();

    decls = top.first;
    vars  = top.second;

    vars_stack.pop_front();
}


//---------------------------------------------------------------------
using temporaries_stack_t = std::forward_list<std::pair<LLVMValueRef, base_compile_ctx_t::cleaners_t>>;

static v_quark_t llvm_stacksave_q;
static v_quark_t llvm_stackrestore_q;

static void
initialize_temporaries_stack_front(base_local_ctx_t &lctx, temporaries_stack_t &temporaries_stack)
{
    assert(temporaries_stack.front().first == nullptr);

    v_type_t    *t;
    LLVMValueRef f;

    lctx.obtain_identifier(llvm_stacksave_q, t, f);

    t = static_cast<v_type_pointer_t *>(t)->element_type();

    auto stack_ptr = LLVMBuildCall2(lctx.global_ctx.builder, t->llvm_type(), f, nullptr, 0, "tmp_stack_ptr");

    temporaries_stack.front().first = stack_ptr;
}

//---------------------------------------------------------------------
LLVMValueRef
base_local_ctx_t::make_temporary(v_type_t *type, LLVMValueRef value)
{
    if (!temporaries_stack.front().first)
    {
        initialize_temporaries_stack_front(*this, temporaries_stack);
    }

    auto tmp = LLVMBuildAlloca(global_ctx.builder, type->llvm_type(), "tmp");

    if (value)  LLVMBuildStore(global_ctx.builder, value, tmp);         //- Sic!

    return  tmp;
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_temporary_cleaner(void (*fun)(void *data), void *data)
{
    if (!temporaries_stack.front().first)
    {
        initialize_temporaries_stack_front(*this, temporaries_stack);
    }

    temporaries_stack.front().second.push_front({fun, data});
}

//---------------------------------------------------------------------
void
base_local_ctx_t::push_temporaries(void)
{
    temporaries_stack.push_front({nullptr, {}});        //- Sic!
}

//---------------------------------------------------------------------
void
base_local_ctx_t::pop_temporaries(void)
{
    auto stack_front = temporaries_stack.front();

    temporaries_stack.pop_front();

    if (!stack_front.first)  return;

    {   auto cur_b = LLVMGetInsertBlock(global_ctx.builder);

        if (auto trm_v = LLVMGetBasicBlockTerminator(cur_b))
        {
            LLVMPositionBuilderBefore(global_ctx.builder, trm_v);
        }
    }

    {   auto &cleaners = stack_front.second;

        for (auto &it: cleaners) it.first(it.second);

        cleaners.clear();       //- Sic!
    }

    v_type_t    *t;
    LLVMValueRef f;

    obtain_identifier(llvm_stackrestore_q, t, f);

    t = static_cast<v_type_pointer_t *>(t)->element_type();

    LLVMBuildCall2(global_ctx.builder, t->llvm_type(), f, &stack_front.first, 1, "");
}

//---------------------------------------------------------------------
extern "C"
{

static LLVMValueRef
v_convert_to_type_default(void *, v_type_t *t0, LLVMValueRef v0, v_type_t *t1)
{
    if (t0 == t1)   return v0;      //- No conversion...

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (t1->kind() == v_type_t::k_pointer)
    {
        LLVMValueRef v1 = 0;

        v_type_t *t;

        if (auto tr = dynamic_cast<v_type_reference_t *>(t0))
        {
            if (auto ta = dynamic_cast<v_type_array_t *>(tr->element_type()))
            {
                //- C-like array-to-pointer "promotion" (from reference)...

                v1 = v0;

                t = ta;
            }
        }
        else if (t0->kind() == v_type_t::k_array)
        {
            //- C-like array-to-pointer "promotion" (from value)...

            if (LLVMIsConstant(v0))
            {
                v1 = LLVMAddGlobal(lctx.module, t0->llvm_type(), "arr");

                LLVMSetInitializer(v1, v0);

                LLVMSetLinkage(v1, LLVMPrivateLinkage);

                LLVMSetUnnamedAddress(v1, LLVMGlobalUnnamedAddr);

                LLVMSetGlobalConstant(v1, true);
            }
            else
            {
                v1 = lctx.make_temporary(t0, v0);
            }

            t = t0;
        }

        if (v1)
        {
            auto n0 = LLVMConstNull(gctx.int_type->llvm_type());

            LLVMValueRef val[2] = { n0, n0 };

            return  LLVMBuildInBoundsGEP2(gctx.builder, t->llvm_type(), v1, val, 2, "");
        }
    }

    //- Just simple scalar "promotions" ...
    //- t1 must be "bigger or equal" than t0 ...

    LLVMOpcode opcode = LLVMOpcode(0);      //- ?

    if (v_type_is_floating_point(t1))
    {
        if (v_type_is_floating_point(t0))
        {
            opcode = LLVMFPExt;
        }
        else
        {
            if (auto t = dynamic_cast<v_type_integer_t *>(t0))
            {
                if (t->is_signed()) opcode = LLVMSIToFP;
                else                opcode = LLVMUIToFP;
            }
        }
    }
    else
    {
        if (auto t = dynamic_cast<v_type_integer_t *>(t0))
        {
            if (t->is_signed()) opcode = LLVMSExt;
            else                opcode = LLVMZExt;
        }
    }

    if (!opcode)  return v0;    //- Sic!

    return  LLVMBuildCast(gctx.builder, opcode, v0, t1->llvm_type(), "");
}

}


//---------------------------------------------------------------------
//- Voidc Global Context
//---------------------------------------------------------------------
static
voidc_global_ctx_t *voidc_global_ctx = nullptr;

VOIDC_DLLEXPORT_BEGIN_VARIABLE

voidc_global_ctx_t * const & voidc_global_ctx_t::voidc = voidc_global_ctx;
base_global_ctx_t  *         voidc_global_ctx_t::target;

LLVMOrcLLJITRef      voidc_global_ctx_t::jit;
LLVMOrcJITDylibRef   voidc_global_ctx_t::main_jd;

LLVMTargetMachineRef voidc_global_ctx_t::target_machine;
LLVMPassManagerRef   voidc_global_ctx_t::pass_manager;

VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
voidc_global_ctx_t::voidc_global_ctx_t()
  : base_global_ctx_t(LLVMGetGlobalContext(), sizeof(int), sizeof(long), sizeof(intptr_t)),
    static_type_type(make_struct_type(v_quark_from_string("v_static_type_t"))),
    type_type(make_struct_type(v_quark_from_string("v_type_t"))),
    type_ptr_type(make_pointer_type(type_type, 0))
{
    assert(voidc_global_ctx == nullptr);

    voidc_global_ctx = this;

    auto q = v_quark_from_string;

    v_quark_t v_static_type_t_q = q("v_static_type_t");

    decls.symbols_insert({v_static_type_t_q, type_type});           //- Sic!
    add_symbol_value(v_static_type_t_q, static_type_type);          //- Sic!

    initialize_type(q("v_type_t"),   type_type);
    initialize_type(q("v_type_ptr"), type_ptr_type);

    {   v_type_t *types[] =
        {
            type_ptr_type,
            make_pointer_type(type_ptr_type, 0),
            unsigned_type,
            bool_type
        };

#define DEF(name, ret, num) \
        decls.symbols_insert({q(#name), make_function_type(ret, types, num, false)});

        DEF(v_function_type, type_ptr_type, 4)

        types[0] = char_ptr_type;
        types[1] = type_ptr_type;
        types[2] = make_pointer_type(void_type, 0);

        DEF(v_add_symbol, void_type, 3)

#undef DEF
    }

#ifdef _WIN32

    decls.constants_insert({q("_WIN32"), void_type});           //- Sic!!!

#endif
}


//---------------------------------------------------------------------
void
voidc_global_ctx_t::initialize_type(v_quark_t raw_name, v_type_t *type)
{
    base_global_ctx_t::initialize_type(raw_name, type);

    typenames = typenames.insert({type, raw_name});
};


//---------------------------------------------------------------------
static char *voidc_triple = nullptr;

//---------------------------------------------------------------------
void
voidc_global_ctx_t::static_initialize(void)
{
    auto q = v_quark_from_string;

    voidc_internal_return_type_q         = q("voidc.internal_return_type");
    voidc_internal_return_value_q        = q("voidc.internal_return_value");
    voidc_internal_branch_target_leave_q = q("voidc.internal_branch_target_leave");
    llvm_stacksave_q                     = q("llvm.stacksave");
    llvm_stackrestore_q                  = q("llvm.stackrestore");

    //-------------------------------------------------------------
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmPrinters();

    //-------------------------------------------------------------
    LLVMOrcCreateLLJIT(&jit, 0);            //- Sic!

    main_jd = LLVMOrcLLJITGetMainJITDylib(jit);

    {   LLVMOrcDefinitionGeneratorRef gen = nullptr;

        LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess(&gen, LLVMOrcLLJITGetGlobalPrefix(jit), nullptr, nullptr);

        LLVMOrcJITDylibAddGenerator(main_jd, gen);
    }

    //-------------------------------------------------------------
    target = new voidc_global_ctx_t();      //- Sic!

    voidc_types_static_initialize();        //- Sic!

    static_cast<voidc_global_ctx_t *>(target)->initialize();    //- Sic!

    //-------------------------------------------------------------
    {   char *triple = LLVMGetDefaultTargetTriple();

        LLVMTargetRef tr;

        char *errmsg = nullptr;

        int err = LLVMGetTargetFromTriple(triple, &tr, &errmsg);

        if (errmsg)
        {
            fprintf(stderr, "LLVMGetTargetFromTriple: %s\n", errmsg);

            LLVMDisposeMessage(errmsg);

            errmsg = nullptr;
        }

        assert(err == 0);

        char *cpu_name     = LLVMGetHostCPUName();
        char *cpu_features = LLVMGetHostCPUFeatures();

        target_machine =
            LLVMCreateTargetMachine
            (
                tr,
                triple,
                cpu_name,
                cpu_features,
                LLVMCodeGenLevelAggressive,
                LLVMRelocDefault,
                LLVMCodeModelJITDefault
            );

        LLVMDisposeMessage(cpu_features);
        LLVMDisposeMessage(cpu_name);
        LLVMDisposeMessage(triple);
    }

    voidc->data_layout = LLVMCreateTargetDataLayout(target_machine);
    voidc_triple       = LLVMGetTargetMachineTriple(target_machine);

    //-------------------------------------------------------------
    pass_manager = LLVMCreatePassManager();

    {   auto pm_builder = LLVMPassManagerBuilderCreate();

        LLVMPassManagerBuilderSetOptLevel(pm_builder, 3);       //- -O3
//      LLVMPassManagerBuilderSetSizeLevel(pm_builder, 2);      //- -Oz

        LLVMPassManagerBuilderPopulateModulePassManager(pm_builder, pass_manager);

        LLVMPassManagerBuilderDispose(pm_builder);
    }

    LLVMAddAlwaysInlinerPass(pass_manager);

    //-------------------------------------------------------------
#define DEF(name) \
    voidc->add_symbol_value(q("voidc_" #name), (void *)name);

    DEF(target_machine)
    DEF(pass_manager)

#undef DEF

    //-------------------------------------------------------------
    {   v_type_t *typ[3];

        typ[0] = voidc->char_ptr_type;
        typ[1] = voidc->size_t_type;
        typ[2] = voidc->bool_type;

        auto ft = voidc->make_function_type(voidc->void_type, typ, 3, false);

        voidc->decls.symbols_insert({q("voidc_object_file_load_to_jit_internal_helper"), ft});
    }

    //-------------------------------------------------------------
    voidc->flush_unit_symbols();

    //-------------------------------------------------------------
#ifndef NDEBUG

//  LLVMEnablePrettyStackTrace();

#endif
}

//---------------------------------------------------------------------
void
voidc_global_ctx_t::static_terminate(void)
{
    voidc->run_cleaners();

    voidc_types_static_terminate();

    LLVMDisposePassManager(pass_manager);

    LLVMDisposeMessage(voidc_triple);
    LLVMDisposeTargetData(voidc->data_layout);

    LLVMOrcDisposeLLJIT(jit);

    delete voidc;

    LLVMShutdown();
}


//---------------------------------------------------------------------
static bool verify_jit_module_optimized = false;

void
voidc_global_ctx_t::prepare_module_for_jit(LLVMModuleRef module)
{
    assert(target == voidc);    //- Sic!

    verify_module(module);

    //-------------------------------------------------------------
    LLVMSetModuleDataLayout(module, voidc->data_layout);
    LLVMSetTarget(module, voidc_triple);

    LLVMRunPassManager(pass_manager, module);
    LLVMRunPassManager(pass_manager, module);           //- WTF ?!?!?!?!?!?!?

    if (verify_jit_module_optimized)  verify_module(module);
}


//---------------------------------------------------------------------
static LLVMOrcJITTargetAddress
add_object_file_to_jit_with_rt(LLVMMemoryBufferRef membuf,
                               LLVMOrcJITDylibRef jd,
                               LLVMOrcResourceTrackerRef rt,
                               bool search_action = false)
{
    char *msg = nullptr;

    auto &jit = voidc_global_ctx_t::jit;

    //-------------------------------------------------------------
    auto bref = LLVMCreateBinary(membuf, nullptr, &msg);
    assert(bref);

    auto mb_copy = LLVMBinaryCopyMemoryBuffer(bref);

    //-------------------------------------------------------------
    auto lerr = LLVMOrcLLJITAddObjectFileWithRT(jit, rt, mb_copy);

    if (lerr)
    {
        msg = LLVMGetErrorMessage(lerr);

        printf("\n%s\n", msg);

        LLVMDisposeErrorMessage(msg);
    }

    //-------------------------------------------------------------
    LLVMOrcJITTargetAddress ret = 0;

    auto it = LLVMObjectFileCopySymbolIterator(bref);

    static std::regex act_regex("unit_[0-9]+_[0-9]+_action");

    while(!LLVMObjectFileIsSymbolIteratorAtEnd(bref, it))
    {
        auto sname = LLVMGetSymbolName(it);

        if (auto sym = unwrap(jit)->lookup(*unwrap(jd), sname))
        {
            auto addr = sym->getValue();

            if (search_action  &&  std::regex_match(sname, act_regex))
            {
                ret = addr;
            }
        }

        LLVMMoveToNextSymbol(it);
    }

    LLVMDisposeSymbolIterator(it);

    //-------------------------------------------------------------
    LLVMDisposeBinary(bref);


//  unwrap(jd)->dump(outs());


    return ret;
}

//---------------------------------------------------------------------
static LLVMOrcJITTargetAddress
add_object_file_to_jit(LLVMMemoryBufferRef membuf,
                       LLVMOrcJITDylibRef jd,
                       bool search_action = false)
{
    return add_object_file_to_jit_with_rt(membuf, jd, LLVMOrcJITDylibGetDefaultResourceTracker(jd), search_action);
}

//---------------------------------------------------------------------
void
voidc_global_ctx_t::add_module_to_jit(LLVMModuleRef module)
{
    assert(target == voidc);    //- Sic!

//  verify_module(module);

    //-------------------------------------------------------------
    LLVMMemoryBufferRef mod_buffer = nullptr;

    char *msg = nullptr;

    auto err = LLVMTargetMachineEmitToMemoryBuffer(voidc_global_ctx_t::target_machine,
                                                   module,
                                                   LLVMObjectFile,
                                                   &msg,
                                                   &mod_buffer);

    if (err)
    {
        printf("\n%s\n", msg);

        LLVMDisposeMessage(msg);

        exit(1);                //- Sic !!!
    }

    assert(mod_buffer);

    add_object_file_to_jit(mod_buffer, main_jd);

    LLVMDisposeMemoryBuffer(mod_buffer);
}


//---------------------------------------------------------------------
void
voidc_global_ctx_t::add_symbol_value(v_quark_t raw_name_q, void *value)
{
    auto raw_name = v_quark_to_string(raw_name_q);

    if (unwrap(jit)->lookup(*unwrap(main_jd), raw_name)) return;

    unit_symbols[unwrap(jit)->mangleAndIntern(raw_name)] = JITEvaluatedSymbol::fromPointer(value);
}


//---------------------------------------------------------------------
void
voidc_global_ctx_t::flush_unit_symbols(void)
{
    if (unit_symbols.empty()) return;

    auto err = unwrap(main_jd)->define(absoluteSymbols(unit_symbols));

    unit_symbols.clear();
}


//---------------------------------------------------------------------
//- Voidc Local Context
//---------------------------------------------------------------------
voidc_local_ctx_t::voidc_local_ctx_t(voidc_global_ctx_t &global)
  : base_local_ctx_t(global)
{
    typenames = global.typenames;

    auto es = LLVMOrcLLJITGetExecutionSession(voidc_global_ctx_t::jit);

    std::string jd_name("local_jd_" + std::to_string(global.local_jd_hash));

    global.local_jd_hash += 1;

    LLVMOrcExecutionSessionCreateJITDylib(es, &local_jd, jd_name.c_str());

    assert(local_jd);

    setup_link_order();
}

//---------------------------------------------------------------------
voidc_local_ctx_t::~voidc_local_ctx_t()
{
    run_cleaners();

    if (parent_ctx)
    {
        //- Restore link order

        static_cast<voidc_local_ctx_t *>(parent_ctx)->setup_link_order();
    }
    else    //- ?
    {
        unwrap(voidc_global_ctx_t::main_jd)->removeFromLinkOrder(*unwrap(local_jd));
    }

    LLVMOrcJITDylibClear(local_jd);         //- So, how to "delete" local_jd ?
}


//---------------------------------------------------------------------
void
voidc_local_ctx_t::export_type(v_quark_t raw_name, v_type_t *type)
{
    base_local_ctx_t::export_type(raw_name, type);

    if (auto *etns = export_typenames)  *etns = etns->insert({type, raw_name});

    typenames = typenames.insert({type, raw_name});
}

//---------------------------------------------------------------------
void
voidc_local_ctx_t::add_type(v_quark_t raw_name, v_type_t *type)
{
    base_local_ctx_t::add_type(raw_name, type);

    typenames = typenames.insert({type, raw_name});
}


//---------------------------------------------------------------------
void
voidc_local_ctx_t::add_symbol_value(v_quark_t raw_name_q, void *value)
{
    auto &jit = voidc_global_ctx_t::jit;

    auto raw_name = v_quark_to_string(raw_name_q);

    if (unwrap(jit)->lookup(*unwrap(local_jd), raw_name)) return;

    unit_symbols[unwrap(jit)->mangleAndIntern(raw_name)] = JITEvaluatedSymbol::fromPointer(value);
}


//---------------------------------------------------------------------
void *
voidc_local_ctx_t::find_symbol_value(v_quark_t raw_name_q)
{
    auto raw_name = v_quark_to_string(raw_name_q);

    for (auto jd : {local_jd, voidc_global_ctx_t::main_jd})
    {
        auto sym = unwrap(voidc_global_ctx_t::jit)->lookup(*unwrap(jd), raw_name);

        if (sym)  return (void *)sym->getValue();
    }

    return nullptr;
}


//---------------------------------------------------------------------
void
voidc_local_ctx_t::adopt_result(v_type_t *type, LLVMValueRef value)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    if (result_type == UNREFERENCE_TAG  &&  type == vctx.static_type_type)
    {
        if (auto pq = typenames.find(reinterpret_cast<v_type_t *>(value)))
        {
            auto q = *pq;

            auto t = get_symbol_type(q);

            assert(t == vctx.type_type);

            auto cname = v_quark_to_string(q);

            auto v = LLVMGetNamedGlobal(module, cname);

            if (!v) v = LLVMAddGlobal(module, t->llvm_type(), cname);

            result_type  = vctx.type_ptr_type;
            result_value = v;

            return;
        }
    }

    base_local_ctx_t::adopt_result(type, value);
}


//---------------------------------------------------------------------
void
voidc_local_ctx_t::add_module_to_jit(LLVMModuleRef mod)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);    //- Sic!

//  verify_module(mod);

    //-------------------------------------------------------------
    LLVMMemoryBufferRef mod_buffer = nullptr;

    char *msg = nullptr;

    auto err = LLVMTargetMachineEmitToMemoryBuffer(voidc_global_ctx_t::target_machine,
                                                   mod,
                                                   LLVMObjectFile,
                                                   &msg,
                                                   &mod_buffer);

    if (err)
    {
        printf("\n%s\n", msg);

        LLVMDisposeMessage(msg);

        exit(1);                //- Sic !!!
    }

    assert(mod_buffer);

    add_object_file_to_jit(mod_buffer, local_jd);

    LLVMDisposeMemoryBuffer(mod_buffer);
}


//---------------------------------------------------------------------
void
voidc_local_ctx_t::prepare_unit_action(int line, int column)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);    //- Sic!

    auto &builder = global_ctx.builder;

    std::string hdr = "unit_" + std::to_string(line) + "_" + std::to_string(column);

    std::string mod_name = hdr + "_module";
    std::string fun_name = hdr + "_action";

    module = LLVMModuleCreateWithNameInContext(mod_name.c_str(), global_ctx.llvm_ctx);

    LLVMSetSourceFileName(module, filename.c_str(), filename.size());

    prepare_function(fun_name.c_str(), global_ctx.make_function_type(global_ctx.void_type, nullptr, 0, false));
}

//---------------------------------------------------------------------
void
voidc_local_ctx_t::finish_unit_action(void)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);    //- Sic!

    finish_function();

    finish_module(module);

    //-------------------------------------------------------------
    voidc_global_ctx_t::prepare_module_for_jit(module);

    //-------------------------------------------------------------
    char *msg = nullptr;

    auto err = LLVMTargetMachineEmitToMemoryBuffer(voidc_global_ctx_t::target_machine,
                                                   module,
                                                   LLVMObjectFile,
                                                   &msg,
                                                   &unit_buffer);

    if (err)
    {
        printf("\n%s\n", msg);

        LLVMDisposeMessage(msg);

        exit(1);                //- Sic !!!
    }

    assert(unit_buffer);

    //-------------------------------------------------------------
    LLVMDisposeModule(module);

    vars = variables_t();       //- ?
}

//---------------------------------------------------------------------
void
voidc_local_ctx_t::run_unit_action(void)
{
    if (!unit_buffer) return;

    auto rt = LLVMOrcJITDylibCreateResourceTracker(local_jd);

    auto addr = add_object_file_to_jit_with_rt(unit_buffer, local_jd, rt, true);

    void (*unit_action)() = (void (*)())addr;

    unit_action();

    flush_unit_symbols();

    LLVMOrcResourceTrackerRemove(rt);
    LLVMOrcReleaseResourceTracker(rt);      //- ?

    fflush(stdout);     //- WTF?
    fflush(stderr);     //- WTF?
}


//---------------------------------------------------------------------
void
voidc_local_ctx_t::flush_unit_symbols(void)
{
    if (!unit_symbols.empty())
    {
        auto err = unwrap(local_jd)->define(absoluteSymbols(unit_symbols));

        unit_symbols.clear();
    }

    voidc_global_ctx_t::voidc->flush_unit_symbols();
}


//---------------------------------------------------------------------
void
voidc_local_ctx_t::setup_link_order(void)
{
#ifdef _WIN32
    auto flags = JITDylibLookupFlags::MatchAllSymbols;
#else
    auto flags = JITDylibLookupFlags::MatchExportedSymbolsOnly;
#endif

    auto g_jd = unwrap(voidc_global_ctx_t::main_jd);
    auto l_jd = unwrap(local_jd);

    JITDylibSearchOrder so =
    {
        { l_jd, flags },    //- First  - the local
        { g_jd, flags }     //- Second - the global
    };

    g_jd->setLinkOrder(so, false);      //- Set "as is"
    l_jd->setLinkOrder(so, false);      //- Set "as is"
}


//---------------------------------------------------------------------
//- Target Global Context
//---------------------------------------------------------------------
target_global_ctx_t::target_global_ctx_t(size_t int_size, size_t long_size, size_t ptr_size)
  : base_global_ctx_t(LLVMContextCreate(), int_size, long_size, ptr_size)
{
    initialize();
}

target_global_ctx_t::~target_global_ctx_t()
{
    run_cleaners();

    LLVMContextDispose(llvm_ctx);
}


//---------------------------------------------------------------------
void
target_global_ctx_t::add_symbol_value(v_quark_t raw_name, void *value)
{
    symbol_values.insert({raw_name, value});
}


//---------------------------------------------------------------------
//- Target Local Context
//---------------------------------------------------------------------
target_local_ctx_t::target_local_ctx_t(base_global_ctx_t &global)
  : base_local_ctx_t(global)
{
}

target_local_ctx_t::~target_local_ctx_t()
{
    run_cleaners();
}


//---------------------------------------------------------------------
void
target_local_ctx_t::add_symbol_value(v_quark_t raw_name, void *value)
{
    symbol_values.insert({raw_name, value});
}


//---------------------------------------------------------------------
void *
target_local_ctx_t::find_symbol_value(v_quark_t raw_name)
{
    for (auto &sv : {symbol_values, static_cast<target_global_ctx_t &>(global_ctx).symbol_values})
    {
        auto it = sv.find(raw_name);

        if (it != sv.end())
        {
            return  it->second;
        }
    }

    return  nullptr;
}


//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION


//---------------------------------------------------------------------
void
v_export_symbol_q(v_quark_t qname, v_type_t *type, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_symbol(qname, type, value);
}

void
v_export_symbol(const char *raw_name, v_type_t *type, void *value)
{
    v_export_symbol_q(v_quark_from_string(raw_name), type, value);
}

void
v_export_symbol_type(const char *raw_name, v_type_t *type)
{
    v_export_symbol_q(v_quark_from_string(raw_name), type, nullptr);
}

void
v_export_symbol_value(const char *raw_name, void *value)
{
    v_export_symbol_q(v_quark_from_string(raw_name), nullptr, value);
}

void
v_export_constant_q(v_quark_t qname, v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_constant(qname, type, value);
}

void
v_export_constant(const char *raw_name, v_type_t *type, LLVMValueRef value)
{
    v_export_constant_q(v_quark_from_string(raw_name), type, value);
}

void
v_export_type_q(v_quark_t qname, v_type_t *type)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_type(qname, type);
}

void
v_export_type(const char *raw_name, v_type_t *type)
{
    v_export_type_q(v_quark_from_string(raw_name), type);
}

//---------------------------------------------------------------------
void
v_add_symbol_q(v_quark_t qname, v_type_t *type, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_symbol(qname, type, value);
}

void
v_add_symbol(const char *raw_name, v_type_t *type, void *value)
{
    v_add_symbol_q(v_quark_from_string(raw_name), type, value);
}

void
v_add_constant_q(v_quark_t qname, v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_constant(qname, type, value);
}

void
v_add_constant(const char *raw_name, v_type_t *type, LLVMValueRef value)
{
    v_add_constant_q(v_quark_from_string(raw_name), type, value);
}

void
v_add_type_q(v_quark_t qname, v_type_t *type)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_type(qname, type);
}

void
v_add_type(const char *raw_name, v_type_t *type)
{
    v_add_type_q(v_quark_from_string(raw_name), type);
}

//---------------------------------------------------------------------
bool
v_find_constant_q(v_quark_t qname, v_type_t **type, LLVMValueRef *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    v_type_t    *t = nullptr;
    LLVMValueRef v = nullptr;

    if (auto p = lctx.decls.constants.find(qname))
    {
        t = *p;

        if (value)
        {
            for (auto &cv : {lctx.constant_values, gctx.constant_values})
            {
                auto itv = cv.find(qname);

                if (itv != cv.end())
                {
                    v = itv->second;

                    break;
                }
            }
        }
    }

    if (type)   *type  = t;
    if (value)  *value = v;

    return bool(t);
}

bool
v_find_constant(const char *raw_name, v_type_t **type, LLVMValueRef *value)
{
    return  v_find_constant_q(v_quark_from_string(raw_name), type, value);
}

v_type_t *
v_find_constant_type(const char *raw_name)
{
    v_type_t *t = nullptr;

    v_find_constant(raw_name, &t, nullptr);

    return t;
}

LLVMValueRef
v_find_constant_value(const char *raw_name)
{
    LLVMValueRef v = nullptr;

    v_find_constant(raw_name, nullptr, &v);

    return v;
}

//---------------------------------------------------------------------
LLVMModuleRef
v_get_module(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.module;
}

void
v_set_module(LLVMModuleRef mod)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.module = mod;
}

//---------------------------------------------------------------------
void
v_debug_print_module(int n)
{
    debug_print_module = n;
}

void
v_verify_module(LLVMModuleRef module)
{
    base_global_ctx_t::verify_module(module);
}

void
voidc_verify_jit_module_optimized(bool f)
{
    verify_jit_module_optimized = f;
}

void
voidc_prepare_module_for_jit(LLVMModuleRef module)
{
    voidc_global_ctx_t::prepare_module_for_jit(module);
}

void
voidc_add_module_to_jit(LLVMModuleRef module)
{
    voidc_global_ctx_t::add_module_to_jit(module);
}

void
voidc_add_local_module_to_jit(LLVMModuleRef module)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    lctx.add_module_to_jit(module);
}

//---------------------------------------------------------------------
void
voidc_add_object_file_to_jit(LLVMMemoryBufferRef membuf)
{
    auto &gctx = *voidc_global_ctx_t::voidc;

    add_object_file_to_jit(membuf, gctx.main_jd);
}

void
voidc_add_local_object_file_to_jit(LLVMMemoryBufferRef membuf)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    add_object_file_to_jit(membuf, lctx.local_jd);
}

//---------------------------------------------------------------------
void
voidc_prepare_unit_action(int line, int column)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    lctx.prepare_unit_action(line, column);
}

void
voidc_finish_unit_action(void)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    lctx.finish_unit_action();
}

//---------------------------------------------------------------------
void
voidc_set_unit_buffer(LLVMMemoryBufferRef membuf)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    lctx.unit_buffer = membuf;
}

LLVMMemoryBufferRef
voidc_get_unit_buffer(void)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    return  lctx.unit_buffer;
}

//---------------------------------------------------------------------
void
voidc_flush_unit_symbols(void)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    lctx.flush_unit_symbols();
}


//---------------------------------------------------------------------
void
voidc_object_file_load_to_jit_internal_helper(const char *buf, size_t len, bool is_local)
{
    auto modbuf = LLVMCreateMemoryBufferWithMemoryRange(buf, len, "modbuf", 0);

    if (is_local) voidc_add_local_object_file_to_jit(modbuf);
    else          voidc_add_object_file_to_jit(modbuf);

    LLVMDisposeMemoryBuffer(modbuf);
}

void
voidc_compile_load_object_file_to_jit(LLVMMemoryBufferRef membuf, bool is_local, bool do_load)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);

    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    auto membuf_ptr  = LLVMGetBufferStart(membuf);
    auto membuf_size = LLVMGetBufferSize(membuf);

    auto membuf_const = LLVMConstString(membuf_ptr, membuf_size, 1);
    auto membuf_const_type = LLVMTypeOf(membuf_const);

    auto membuf_glob = LLVMAddGlobal(lctx.module, membuf_const_type, "membuf_g");

    LLVMSetLinkage(membuf_glob, LLVMPrivateLinkage);

    LLVMSetInitializer(membuf_glob, membuf_const);

    LLVMValueRef val[3];

    val[0] = LLVMConstInt(gctx.int_type->llvm_type(), 0, 0);
    val[1] = val[0];

    auto membuf_const_ptr = LLVMBuildInBoundsGEP2(gctx.builder, membuf_const_type, membuf_glob, val, 2, "membuf_const_ptr");

    v_type_t    *t;
    LLVMValueRef f;

    lctx.obtain_identifier(v_quark_from_string("voidc_object_file_load_to_jit_internal_helper"), t, f);
    assert(f);

    val[0] = membuf_const_ptr;
    val[1] = LLVMConstInt(gctx.size_t_type->llvm_type(), membuf_size, 0);
    val[2] = LLVMConstInt(gctx.bool_type->llvm_type(), is_local, 0);

    t = static_cast<v_type_pointer_t *>(t)->element_type();

    LLVMBuildCall2(gctx.builder, t->llvm_type(), f, val, 3, "");

    //-----------------------------------------------------------------
    if (do_load)
    {
        if (is_local) voidc_add_local_object_file_to_jit(membuf);
        else          voidc_add_object_file_to_jit(membuf);
    }
}

void
voidc_compile_load_module_to_jit(LLVMModuleRef module, bool is_local, bool do_load)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);

    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    //- Emit module -> membuf

    LLVMMemoryBufferRef membuf = nullptr;

    char *msg = nullptr;

    auto err = LLVMTargetMachineEmitToMemoryBuffer(voidc_global_ctx_t::target_machine,
                                                   module,
                                                   LLVMObjectFile,
                                                   &msg,
                                                   &membuf);

    if (err)
    {
        printf("\n%s\n", msg);

        LLVMDisposeMessage(msg);

        exit(1);                //- Sic !!!
    }

    assert(membuf);

    voidc_compile_load_object_file_to_jit(membuf, is_local, do_load);

    LLVMDisposeMemoryBuffer(membuf);
}


VOIDC_DLLEXPORT_END


static void
load_object_file_helper(LLVMMemoryBufferRef membuf, bool is_local, bool do_load)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);

    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    //- Generate unit ...

    auto saved_module = lctx.module;

    //- Do replace/make unit...

    if (lctx.unit_buffer)   LLVMDisposeMemoryBuffer(lctx.unit_buffer);

    lctx.prepare_unit_action(0, 0);         //- line, column ?..

    voidc_compile_load_object_file_to_jit(membuf, is_local, do_load);

//  base_global_ctx_t::debug_print_module = 1;

    lctx.finish_unit_action();

    lctx.module = saved_module;
}


static void
load_module_helper(LLVMModuleRef module, bool is_local, bool do_load)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);

    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    lctx.finish_module(module);

    gctx.prepare_module_for_jit(module);

    //- Generate unit ...

    auto saved_module = lctx.module;

    //- Do replace/make unit...

    if (lctx.unit_buffer)   LLVMDisposeMemoryBuffer(lctx.unit_buffer);

    lctx.prepare_unit_action(0, 0);         //- line, column ?..

    voidc_compile_load_module_to_jit(module, is_local, do_load);

//  base_global_ctx_t::debug_print_module = 1;

    lctx.finish_unit_action();

    lctx.module = saved_module;
}


VOIDC_DLLEXPORT_BEGIN_FUNCTION


//---------------------------------------------------------------------
void
voidc_unit_load_local_object_file_to_jit(LLVMMemoryBufferRef membuf, bool do_load)
{
    load_object_file_helper(membuf, true, do_load);
}

//---------------------------------------------------------------------
void
voidc_unit_load_object_file_to_jit(LLVMMemoryBufferRef membuf, bool do_load)
{
    load_object_file_helper(membuf, false, do_load);
}


//---------------------------------------------------------------------
void
voidc_unit_load_local_module_to_jit(LLVMModuleRef module, bool do_load)
{
    load_module_helper(module, true, do_load);
}

//---------------------------------------------------------------------
void
voidc_unit_load_module_to_jit(LLVMModuleRef module, bool do_load)
{
    load_module_helper(module, false, do_load);
}


//---------------------------------------------------------------------
obtain_module_t
v_get_obtain_module(void **pctx)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (pctx) *pctx = lctx.obtain_module_ctx;

    return lctx.obtain_module_fun;
}

void
v_set_obtain_module(obtain_module_t fun, void *ctx)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.obtain_module_fun = fun;
    lctx.obtain_module_ctx = ctx;
}

LLVMModuleRef
v_obtain_module(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.obtain_module();
}

bool
v_obtain_identifier_q(v_quark_t qname, v_type_t * *type, LLVMValueRef *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    v_type_t    *t;
    LLVMValueRef v;

    bool ok = lctx.obtain_identifier(qname, t, v);

    if (ok)
    {
        if (type)   *type  = t;
        if (value)  *value = v;
    }

    return  ok;
}

bool
v_obtain_identifier(const char *name, v_type_t * *type, LLVMValueRef *value)
{
    return  v_obtain_identifier_q(v_quark_from_string(name), type, value);
}

//---------------------------------------------------------------------
finish_module_t
v_get_finish_module(void **pctx)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (pctx) *pctx = lctx.finish_module_ctx;

    return lctx.finish_module_fun;
}

void
v_set_finish_module(finish_module_t fun, void *ctx)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.finish_module_fun = fun;
    lctx.finish_module_ctx = ctx;
}

void
v_finish_module(LLVMModuleRef mod)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.finish_module(mod);
}


//---------------------------------------------------------------------
void
v_save_builder_ip(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_builder_ip();
}

void
v_restore_builder_ip(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.pop_builder_ip();
}


//---------------------------------------------------------------------
LLVMValueRef
v_prepare_function(const char *name, v_type_t *type)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.prepare_function(name, type);
}

//---------------------------------------------------------------------
void
v_finish_function(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.finish_function();
}


//---------------------------------------------------------------------
void
v_add_variable_q(v_quark_t qname, v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.vars = lctx.vars.set(qname, {type, value});
}

void
v_add_variable(const char *name, v_type_t *type, LLVMValueRef value)
{
    v_add_variable_q(v_quark_from_string(name), type, value);
}

bool
v_get_variable_q(v_quark_t qname, v_type_t **type, LLVMValueRef *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (auto *pv = lctx.vars.find(qname))
    {
        if (type)   *type  = pv->first;
        if (value)  *value = pv->second;

        return true;
    }

    return false;
}

bool
v_get_variable(const char *name, v_type_t **type, LLVMValueRef *value)
{
    return  v_get_variable_q(v_quark_from_string(name), type, value);
}

v_type_t *
v_get_variable_type(const char *name)
{
    v_type_t *t = nullptr;

    v_get_variable(name, &t, nullptr);

    return t;
}

LLVMValueRef
v_get_variable_value(const char *name)
{
    LLVMValueRef v = nullptr;

    v_get_variable(name, nullptr, &v);

    return v;
}

//---------------------------------------------------------------------
size_t
v_get_variables_size(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.vars.size();
}

//---------------------------------------------------------------------
void
v_clear_variables(void)         //- ?
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.vars = base_local_ctx_t::variables_t();
}

void
v_save_variables(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_variables();
}

void
v_restore_variables(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.pop_variables();
}

//---------------------------------------------------------------------
v_type_t *
v_get_result_type(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.result_type;
}

void
v_set_result_type(v_type_t *type)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_type = type;
}

LLVMValueRef
v_get_result_value(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.result_value;
}

void
v_set_result_value(LLVMValueRef val)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_value = val;
}

//---------------------------------------------------------------------
void
v_adopt_result(v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.adopt_result(type, value);
}

//---------------------------------------------------------------------
convert_to_type_t
v_get_convert_to_type(void **pctx)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (pctx) *pctx = lctx.convert_to_type_ctx;

    return lctx.convert_to_type_fun;
}

void
v_set_convert_to_type(convert_to_type_t fun, void *ctx)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.convert_to_type_fun = fun;
    lctx.convert_to_type_ctx = ctx;
}

LLVMValueRef
v_convert_to_type(v_type_t *t0, LLVMValueRef v0, v_type_t *t1)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.convert_to_type(t0, v0, t1);
}

//---------------------------------------------------------------------
LLVMValueRef
v_make_temporary(v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.make_temporary(type, value);
}

void
v_add_temporary_cleaner(void (*fun)(void *), void *data)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_temporary_cleaner(fun, data);
}

void
v_push_temporaries(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_temporaries();
}

void
v_pop_temporaries(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.pop_temporaries();
}

LLVMValueRef
v_get_temporaries_front(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.get_temporaries_front();
}


//---------------------------------------------------------------------
v_type_t *
v_find_symbol_type_q(v_quark_t qname)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.get_symbol_type(qname);
}

v_type_t *
v_find_symbol_type(const char *raw_name)
{
    return v_find_symbol_type_q(v_quark_from_string(raw_name));
}

//---------------------------------------------------------------------
void *
v_find_symbol_value_q(v_quark_t qname)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.find_symbol_value(qname);
}

void *
v_find_symbol_value(const char *raw_name)
{
    return v_find_symbol_value_q(v_quark_from_string(raw_name));
}

//---------------------------------------------------------------------
v_type_t *
v_find_type_q(v_quark_t qname)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto ret = lctx.find_type(qname);

#ifndef NDEBUG

    if (!ret)
    {
        printf("v_find_type: %s  not found!\n", v_quark_to_string(qname));
    }

#endif

    return  ret;
}

v_type_t *
v_find_type(const char *name)
{
    return v_find_type_q(v_quark_from_string(name));
}


//---------------------------------------------------------------------
void
v_export_alias_q(v_quark_t qname, v_quark_t qraw_name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_alias(qname, qraw_name);
}

void
v_export_alias(const char *name, const char *raw_name)
{
    auto q = v_quark_from_string;

    v_export_alias_q(q(name), q(raw_name));
}

void
v_add_alias_q(v_quark_t qname, v_quark_t qraw_name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_alias(qname, qraw_name);
}

void
v_add_alias(const char *name, const char *raw_name)
{
    auto q = v_quark_from_string;

    v_add_alias_q(q(name), q(raw_name));
}


//---------------------------------------------------------------------
void
v_export_intrinsic_q(v_quark_t qname, void *fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_intrinsic(qname, fun, aux);
}

void
v_export_intrinsic(const char *name, void *fun, void *aux)
{
    v_export_intrinsic_q(v_quark_from_string(name), fun, aux);
}

void
v_add_intrinsic_q(v_quark_t qname, void *fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_intrinsic(qname, fun, aux);
}

void
v_add_intrinsic(const char *name, void *fun, void *aux)
{
    v_add_intrinsic_q(v_quark_from_string(name), fun, aux);
}

void *
v_get_intrinsic_q(v_quark_t qname, void **aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto &intrinsics = lctx.decls.intrinsics;

    if (auto p = intrinsics.find(qname))
    {
        if (aux)  *aux = p->second;

        return  p->first;
    }

    return nullptr;
}

void *
v_get_intrinsic(const char *name, void **aux)
{
    return v_get_intrinsic_q(v_quark_from_string(name), aux);
}


//---------------------------------------------------------------------
void
v_add_cleaner(void (*fun)(void *), void *data)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.add_cleaner(fun, data);
}

void
v_add_local_cleaner(void (*fun)(void *), void *data)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_cleaner(fun, data);
}


//---------------------------------------------------------------------
LLVMContextRef
v_target_get_voidc_llvm_ctx(void)
{
    return voidc_global_ctx_t::voidc->llvm_ctx;
}

LLVMContextRef
v_target_get_llvm_ctx(void)
{
    return voidc_global_ctx_t::target->llvm_ctx;
}

//---------------------------------------------------------------------
LLVMBuilderRef
v_target_get_voidc_builder(void)
{
    return voidc_global_ctx_t::voidc->builder;
}

LLVMBuilderRef
v_target_get_builder(void)
{
    return voidc_global_ctx_t::target->builder;
}


//---------------------------------------------------------------------
LLVMTargetDataRef
v_target_get_voidc_data_layout(void)
{
    return voidc_global_ctx_t::voidc->data_layout;
}

LLVMTargetDataRef
v_target_get_data_layout(void)
{
    return voidc_global_ctx_t::target->data_layout;
}

void
v_target_set_data_layout(LLVMTargetDataRef layout)
{
    voidc_global_ctx_t::target->data_layout = layout;
}


//---------------------------------------------------------------------
base_global_ctx_t *
v_target_get_voidc_global_ctx(void)
{
    return voidc_global_ctx_t::voidc;
}

base_global_ctx_t *
v_target_get_global_ctx(void)
{
    return voidc_global_ctx_t::target;
}

void
v_target_set_global_ctx(base_global_ctx_t *gctx)
{
    voidc_global_ctx_t::target = gctx;
}


//---------------------------------------------------------------------
base_global_ctx_t *
v_target_create_global_ctx(size_t int_size, size_t long_size, size_t ptr_size)
{
    return  new target_global_ctx_t(int_size, long_size, ptr_size);
}

void
v_target_destroy_global_ctx(base_global_ctx_t *gctx)
{
    delete gctx;
}

//---------------------------------------------------------------------
base_local_ctx_t *
v_target_create_local_ctx(const char *filename, base_global_ctx_t *gctx)
{
    auto ret = new target_local_ctx_t(*gctx);

    ret->filename = filename;

    return ret;
}

void
v_target_destroy_local_ctx(base_local_ctx_t *lctx)
{
    delete lctx;
}

//---------------------------------------------------------------------
bool
v_target_local_ctx_has_parent(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.has_parent();
}


//---------------------------------------------------------------------
#ifdef _WIN32

void ___chkstk_ms(void) {}       //- WTF !?!?!?!?!?!?!?!?!

#endif


VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


