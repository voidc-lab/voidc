//---------------------------------------------------------------------
//- Copyright (C) 2020-2025 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_util.h"

#include "voidc_dllexport.h"
#include "voidc_types.h"
#include "voidc_target.h"
#include "voidc_visitor.h"

#include <cassert>
#include <string>
#include <map>
#include <set>

#include <llvm-c/Core.h>

#include <immer/flex_vector.hpp>


//---------------------------------------------------------------------
namespace utility
{

//---------------------------------------------------------------------
extern "C"
typedef void (*overloaded_intrinsic_t)(void *aux, const visitor_t *vis, const ast_base_t *self,
                                       const ast_expr_t **args, unsigned count);

//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
static
void overloaded_intrinsic_default(void *aux, const visitor_t *vis, const ast_base_t *self,
                                  const ast_expr_t **args, unsigned arg_count)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto qname = v_quark_t(uintptr_t(aux));

    v_type_t    *t;
    LLVMValueRef fv;

    if (!lctx.obtain_identifier(qname, t, fv))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + v_quark_to_string(qname));
    }

    t = static_cast<v_type_pointer_t *>(t)->element_type();

    auto ft = static_cast<v_type_function_t *>(t);

    auto par_count = ft->param_count();
    auto par_types = ft->param_types();

    auto values = std::make_unique<LLVMValueRef[]>(arg_count);

    lctx.push_result();

    for (int i=0; i<arg_count; ++i)
    {
        if (i > 0)  lctx.reset_result();

        if (i < par_count)  lctx.result_type = par_types[i];
        else                lctx.result_type = UNREFERENCE_TAG;

        voidc_visitor_data_t::visit(*vis, *args[i]);

        values[i] = lctx.result_value;
    }

    auto v = LLVMBuildCall2(gctx.builder, ft->llvm_type(), fv, values.get(), arg_count, "");

    lctx.pop_result();

    lctx.adopt_result(ft->return_type(), v);
}

//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
static overloaded_intrinsic_t
lookup_overload_default(void *aux, v_quark_t quark, v_type_t *type, void **paux)
{
    auto &lctx = *(reinterpret_cast<base_local_ctx_t *>(aux));

    const v_quark_t *qname = nullptr;

    if (auto *it = lctx.decls.overloads.find(quark))
    {
        qname = it->find(type);
    }

    if (!qname)  return nullptr;

    if (auto p = lctx.decls.intrinsics.find(*qname))
    {
        if (paux) *paux = p->second;

        return  overloaded_intrinsic_t(p->first);
    }

    if (paux) *paux = (void *)uintptr_t(*qname);

    return  overloaded_intrinsic_default;
}

//---------------------------------------------------------------------
static v_quark_t lookup_overload_q;

//---------------------------------------------------------------------
extern "C"
typedef overloaded_intrinsic_t (*lookup_overload_t)(void *, v_quark_t, v_type_t *, void **);

static inline lookup_overload_t
get_lookup_overload_hook(void **paux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (auto *p = lctx.decls.intrinsics.find(lookup_overload_q))
    {
        if (paux) *paux = p->second;

        return  lookup_overload_t(p->first);
    }

    if (paux) *paux = &lctx;

    return lookup_overload_default;
}

static inline void
set_lookup_overload_hook(lookup_overload_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.decls.intrinsics_insert({lookup_overload_q, {(void *)fun, aux}});
}

static inline overloaded_intrinsic_t
lookup_overload(v_quark_t quark, v_type_t *type, void **paux)
{
    void *aux;
    auto *fun = get_lookup_overload_hook(&aux);

    return fun(aux, quark, type, paux);         //- Sic!
}


//---------------------------------------------------------------------
//- Intrinsics (true)
//---------------------------------------------------------------------
static v_type_t *generic_list_type;

//---------------------------------------------------------------------
static
void v_universal_intrinsic(void *void_aux, const visitor_t *vis, const ast_base_t *self)
{
    auto &args = static_cast<const ast_expr_call_data_t &>(**self).arg_list;

    auto ctx = (unsigned *)void_aux;

    auto quark = v_quark_t(ctx[0]);
    auto par_count = ctx[1];

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_result();

    lctx.result_type = UNREFERENCE_TAG;

    voidc_visitor_data_t::visit(*vis, args->data[0]);

    auto res_type  = lctx.result_type;
    auto res_value = lctx.result_value;

    auto type = static_cast<v_type_pointer_t *>(res_type)->element_type();

    if (par_count == 0)
    {
        //- Special case for v_make_list ...

        if (type == generic_list_type)  par_count = 4;
        else                            par_count = 3;
    }

    void *aux;
    auto *fun = lookup_overload(quark, type, &aux);
    assert(fun);

    auto arg_count = unsigned(args->data.size());

    auto count = std::max(arg_count, par_count);

    auto argp = std::make_unique<const ast_expr_t *[]>(count);

    ast_expr_t arg0 = std::make_shared<const ast_expr_compiled_data_t>(res_type, res_value);

    argp[0] = &arg0;

    ast_expr_t e_one;

    for (int i=1; i<count; ++i)
    {
        if (i >= arg_count)
        {
            static ast_expr_t expr = std::make_shared<const ast_expr_integer_data_t>(1);

            argp[i] = &expr;
        }
        else
        {
            argp[i] = &args->data[i];
        }
    }

    lctx.pop_result();

    fun(aux, vis, self, argp.get(), count);
}


//---------------------------------------------------------------------
static
void v_std_any_get_value_intrinsic(void *void_quark, const visitor_t *vis, const ast_base_t *self)
{
    auto &args = static_cast<const ast_expr_call_data_t &>(**self).arg_list;

    auto quark = v_quark_t(uintptr_t(void_quark));

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_result();

    lctx.result_type = gctx.static_type_type;

    voidc_visitor_data_t::visit(*vis, args->data[0]);       //- Type

    auto type = reinterpret_cast<v_type_t *>(lctx.result_value);

    void *aux;
    auto *fun = lookup_overload(quark, type, &aux);
    assert(fun);

    const ast_expr_t *argp = &args->data[1];

    lctx.pop_result();

    fun(aux, vis, self, &argp, 1);
}

//---------------------------------------------------------------------
static
void v_std_any_get_pointer_intrinsic(void *void_quark, const visitor_t *vis, const ast_base_t *self)
{
    auto &args = static_cast<const ast_expr_call_data_t &>(**self).arg_list;

    auto quark = v_quark_t(uintptr_t(void_quark));

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_result();

    lctx.result_type = gctx.static_type_type;

    voidc_visitor_data_t::visit(*vis, args->data[0]);       //- Type

    auto type = reinterpret_cast<v_type_t *>(lctx.result_value);

    void *aux;
    auto *fun = lookup_overload(quark, type, &aux);
    assert(fun);

    const ast_expr_t *argp = &args->data[1];

    lctx.pop_result();

    fun(aux, vis, self, &argp, 1);
}

//---------------------------------------------------------------------
static
void v_std_any_set_value_intrinsic(void *void_quark, const visitor_t *vis, const ast_base_t *self)
{
    auto &args = static_cast<const ast_expr_call_data_t &>(**self).arg_list;

    auto quark = v_quark_t(uintptr_t(void_quark));

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_result();

    lctx.result_type = UNREFERENCE_TAG;

    voidc_visitor_data_t::visit(*vis, args->data[1]);       //- Second argument

    auto type  = lctx.result_type;
    auto value = lctx.result_value;

    void *aux;
    auto *fun = lookup_overload(quark, type, &aux);
    assert(fun);

    ast_expr_t arg1 = std::make_shared<const ast_expr_compiled_data_t>(type, value);

    const ast_expr_t *argp[2] = { &args->data[0], &arg1 };

    lctx.pop_result();

    fun(aux, vis, self, argp, 2);
}

//---------------------------------------------------------------------
static
void v_std_any_set_pointer_intrinsic(void *void_quark, const visitor_t *vis, const ast_base_t *self)
{
    auto &args = static_cast<const ast_expr_call_data_t &>(**self).arg_list;

    auto quark = v_quark_t(uintptr_t(void_quark));

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_result();

    lctx.result_type = UNREFERENCE_TAG;

    voidc_visitor_data_t::visit(*vis, args->data[1]);       //- Second argument

    auto t = lctx.result_type;
    auto v = lctx.result_value;

    auto type = static_cast<v_type_pointer_t *>(t)->element_type();

    void *aux;
    auto *fun = lookup_overload(quark, type, &aux);
    assert(fun);

    ast_expr_t arg1 = std::make_shared<const ast_expr_compiled_data_t>(t, v);

    const ast_expr_t *argp[2] = { &args->data[0], &arg1 };

    lctx.pop_result();

    fun(aux, vis, self, argp, 2);
}


//---------------------------------------------------------------------
//- v_util_list_t ...
//---------------------------------------------------------------------
using util_list_data_t = immer::flex_vector<std::any>;

using util_list_t = std::shared_ptr<const util_list_data_t>;


//---------------------------------------------------------------------
//- v_util_map_t ...
//---------------------------------------------------------------------
using util_map_data_t = immer::map<intptr_t, std::any>;

using util_map_t = std::shared_ptr<const util_map_data_t>;


//---------------------------------------------------------------------
//- Static init/term ...
//---------------------------------------------------------------------
void static_initialize(void)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    auto q = v_quark_from_string;

    generic_list_type = vctx.make_struct_type(q("v_ast_generic_list_t"));

    lookup_overload_q = q("voidc.hook_lookup_overload");

#define DEF_U(name, num) \
    auto name##_q = q("v_" #name); \
    static unsigned name##_params[2] = { name##_q, num }; \
    vctx.decls.intrinsics_insert({name##_q, {(void *)v_universal_intrinsic, (void *)name##_params}});

#define DEF(name) \
    auto name##_q = q("v_" #name); \
    vctx.decls.intrinsics_insert({name##_q, {(void *)v_##name##_intrinsic, (void *)uintptr_t(name##_q)}});

    DEF_U(initialize, 2)
    DEF_U(terminate, 2)

    DEF_U(copy, 3)
    DEF_U(move, 3)

    DEF_U(empty, 1)
    DEF_U(kind, 1)

    DEF(std_any_get_value)
    DEF(std_any_get_pointer)
    DEF(std_any_set_value)
    DEF(std_any_set_pointer)

    DEF_U(make_list_nil, 1)         //- Sic !!!
    DEF_U(make_list, 0)             //- Sic !!!

    DEF_U(list_append, 4)
    DEF_U(list_get_size, 1)
    DEF_U(list_get_item, 2)

    DEF_U(list_concat, 3)
    DEF_U(list_insert, 5)
    DEF_U(list_erase, 4)

    DEF_U(make_map, 1)
    DEF_U(map_get_size, 1)
    DEF_U(map_find, 2)
    DEF_U(map_insert, 4)
    DEF_U(map_erase, 3)

    DEF_U(ast_make_generic, 3)
    DEF_U(ast_generic_get_vtable, 1)
    DEF_U(ast_generic_get_object, 1)

#undef DEF
#undef DEF_U
#undef DEF2

    //-----------------------------------------------------------------
#define DEF(ctype, name) \
    static_assert((sizeof(ctype) % sizeof(intptr_t)) == 0); \
    v_type_t *name##_content_type = vctx.make_array_type(vctx.intptr_t_type, sizeof(ctype)/sizeof(intptr_t)); \
    auto name##_q = q("v_" #name "_t"); \
    auto name##_type = vctx.make_struct_type(name##_q); \
    name##_type->set_body(&name##_content_type, 1, false); \
    vctx.initialize_type(name##_q, name##_type);

    DEF(std::any, std_any)
    DEF(std::string, std_string)

    DEF(util_list_t, util_list)
    DEF(util_map_t, util_map)

#undef DEF
}

//---------------------------------------------------------------------
void static_terminate(void)
{
}


//---------------------------------------------------------------------
}   //- namespace utility



//---------------------------------------------------------------------
//- !!!
//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION


//---------------------------------------------------------------------
utility::overloaded_intrinsic_t
v_util_lookup_overload(v_quark_t quark, v_type_t *type, void **paux)
{
    return  utility::lookup_overload(quark, type, paux);
}

utility::lookup_overload_t
v_util_get_lookup_overload_hook(void **paux)
{
    return  utility::get_lookup_overload_hook(paux);
}

void
v_util_set_lookup_overload_hook(utility::lookup_overload_t fun, void *aux)
{
    utility::set_lookup_overload_hook(fun, aux);
}


//---------------------------------------------------------------------
VOIDC_DEFINE_INITIALIZE_IMPL(std::any, v_util_initialize_std_any_impl)
VOIDC_DEFINE_TERMINATE_IMPL(std::any, v_util_terminate_std_any_impl)
VOIDC_DEFINE_COPY_IMPL(std::any, v_util_copy_std_any_impl)
VOIDC_DEFINE_MOVE_IMPL(std::any, v_util_move_std_any_impl)

bool v_util_empty_std_any_impl(const std::any *ptr)
{
    return !ptr->has_value();
}

//---------------------------------------------------------------------
VOIDC_DEFINE_INITIALIZE_IMPL(std::string, v_util_initialize_std_string_impl)
VOIDC_DEFINE_TERMINATE_IMPL(std::string, v_util_terminate_std_string_impl)
VOIDC_DEFINE_COPY_IMPL(std::string, v_util_copy_std_string_impl)
VOIDC_DEFINE_MOVE_IMPL(std::string, v_util_move_std_string_impl)

bool v_util_empty_std_string_impl(const std::string *ptr)
{
    return ptr->empty();
}

char *v_std_string_get(std::string *ptr)
{
    return ptr->data();
}

size_t v_std_string_get_size(std::string *ptr)
{
    return ptr->size();
}

char *v_std_string_get_data(std::string *ptr, size_t *len)
{
    *len = ptr->size();

    return ptr->data();
}

void v_std_string_set(std::string *ptr, const char *str)
{
    *ptr = str;
}

void v_std_string_set_data(std::string *ptr, const char *str, size_t len)
{
    *ptr = {str, len};
}

void v_std_string_append(std::string *ptr, const char *str)
{
    ptr->append(str);
}

void v_std_string_append_char(std::string *ptr, char32_t c)
{
    char d[5] = { 0, 0, 0, 0, 0 };

    int r;

    if (c <= 0x7FF)
    {
        if (c <= 0x7F)  r = 0;
        else            r = 1;
    }
    else
    {
        if (c <= 0xFFFF)  r = 2;
        else              r = 3;
    }

    if (r == 0)
    {
        d[0] = c & 0x7F;
    }
    else
    {
        for (int j = 0; j < r; ++j)
        {
            d[r-j] = 0x80 | (c & 0x3F);

            c >>= 6;
        }

        d[0] = (0x1E << (6-r)) | (c & (0x3F >> r));
    }

    ptr->append(d, r+1);            //- Sic!
}

void v_std_string_append_number(std::string *ptr, intptr_t n)
{
    *ptr += std::to_string(n);
}

//---------------------------------------------------------------------
#define DEF_VAR(c_type, type_tag) \
VOIDC_DEFINE_STD_ANY_GET_VALUE_IMPL(c_type, v_util_std_any_get_value_##type_tag##_impl) \
VOIDC_DEFINE_STD_ANY_SET_VALUE_IMPL(c_type, v_util_std_any_set_value_##type_tag##_impl)

#define DEF_PTR(c_type, type_tag) \
VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(c_type, v_util_std_any_get_pointer_##type_tag##_impl) \
VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(c_type, v_util_std_any_set_pointer_##type_tag##_impl)

#define DEF(c_type, type_tag) \
DEF_VAR(c_type, type_tag) \
DEF_PTR(c_type, type_tag)

DEF(bool,     bool)
DEF(int8_t,   int8_t)
DEF(uint8_t,  uint8_t)
DEF(int16_t,  int16_t)
DEF(uint16_t, uint16_t)
DEF(int32_t,  int32_t)
DEF(uint32_t, uint32_t)
DEF(int64_t,  int64_t)
DEF(uint64_t, uint64_t)

DEF_PTR(std::string, std_string)

#define DEF_INTPTR_ARRAY(n) \
struct intptr_tx##n { intptr_t _[n]; }; \
DEF_PTR(intptr_tx##n, intptr_tx##n)

DEF_INTPTR_ARRAY(2)
DEF_INTPTR_ARRAY(3)
DEF_INTPTR_ARRAY(4)
DEF_INTPTR_ARRAY(5)
DEF_INTPTR_ARRAY(6)
DEF_INTPTR_ARRAY(7)
DEF_INTPTR_ARRAY(8)

#undef DEF_INTPTR_ARRAY

#undef DEF
#undef DEF_PTR
#undef DEF_VAR


VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
using namespace utility;


//---------------------------------------------------------------------
//- v_util_list_t ...
//---------------------------------------------------------------------
extern "C"
{
VOIDC_DLLEXPORT_BEGIN_FUNCTION

//---------------------------------------------------------------------
void
v_util_make_list_nil_util_list_impl(util_list_t *ret)
{
    (*ret) = std::make_shared<const util_list_data_t>();
}

void
v_util_make_list_util_list_impl(util_list_t *ret, const std::any *items, size_t count)
{
    (*ret) = std::make_shared<const util_list_data_t>(items, items+count);
}

//---------------------------------------------------------------------
void
v_util_list_append_util_list_impl(util_list_t *ret, const util_list_t *list, const std::any *items, size_t count)
{
    (*ret) = std::make_shared<const util_list_data_t>(**list + util_list_data_t(items, items+count));
}

size_t
v_util_list_get_size_util_list_impl(const util_list_t *list)
{
    return (*list)->size();
}

const std::any *
v_util_list_get_item_util_list_impl(const util_list_t *list, size_t idx)
{
    return &(**list)[idx];
}

//---------------------------------------------------------------------
void
v_util_list_concat_util_list_impl(util_list_t *ret, const util_list_t *lhs, const util_list_t *rhs)
{
    (*ret) = std::make_shared<const util_list_data_t>(**lhs + **rhs);
}

void
v_util_list_insert_util_list_impl(util_list_t *ret, const util_list_t *list, size_t pos, const std::any *items, size_t count)
{
    auto v = util_list_data_t(items, items+count);

    (*ret) = std::make_shared<const util_list_data_t>((*list)->insert(pos, v));
}

void
v_util_list_erase_util_list_impl(util_list_t *ret, const util_list_t *list, size_t pos, size_t count)
{
    (*ret) = std::make_shared<const util_list_data_t>((*list)->erase(pos, pos+count));
}

//---------------------------------------------------------------------

VOIDC_DLLEXPORT_END
}   //- extern "C"


//---------------------------------------------------------------------
//- v_util_map_t ...
//---------------------------------------------------------------------
extern "C"
{
VOIDC_DLLEXPORT_BEGIN_FUNCTION

//---------------------------------------------------------------------
void
v_util_make_map_util_map_impl(util_map_t *ret)
{
    (*ret) = std::make_shared<const util_map_data_t>();
}

size_t
v_util_map_get_size_util_map_impl(const util_map_t *map)
{
    return (*map)->size();
}

const std::any *
v_util_map_find_util_map_impl(const util_map_t *map, intptr_t key)
{
    return (*map)->find(key);
}

void
v_util_map_insert_util_map_impl(util_map_t *ret, const util_map_t *map, intptr_t key, const std::any *val)
{
    (*ret) = std::make_shared<const util_map_data_t>((*map)->insert({key, *val}));
}

void
v_util_map_erase_util_map_impl(util_map_t *ret, const util_map_t *map, intptr_t key)
{
    (*ret) = std::make_shared<const util_map_data_t>((*map)->erase(key));
}

//---------------------------------------------------------------------

VOIDC_DLLEXPORT_END
}   //- extern "C"


//---------------------------------------------------------------------
extern "C"
{
VOIDC_DLLEXPORT_BEGIN_FUNCTION

//---------------------------------------------------------------------
#define DEF(name) \
    VOIDC_DEFINE_INITIALIZE_IMPL(util_##name##_t, v_util_initialize_util_##name##_impl) \
    VOIDC_DEFINE_TERMINATE_IMPL(util_##name##_t, v_util_terminate_util_##name##_impl) \
    VOIDC_DEFINE_COPY_IMPL(util_##name##_t, v_util_copy_util_##name##_impl) \
    VOIDC_DEFINE_MOVE_IMPL(util_##name##_t, v_util_move_util_##name##_impl) \
    VOIDC_DEFINE_EMPTY_IMPL(util_##name##_t, v_util_empty_util_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(util_##name##_t, v_util_std_any_get_pointer_util_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(util_##name##_t, v_util_std_any_set_pointer_util_##name##_impl)

DEF(list)
DEF(map)

#undef DEF

//---------------------------------------------------------------------

VOIDC_DLLEXPORT_END
}   //- extern "C"


