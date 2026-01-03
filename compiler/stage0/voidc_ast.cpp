//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_ast.h"

#include "voidc_util.h"
#include "voidc_target.h"
#include "voidc_visitor.h"

#include <typeindex>


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
#define AST_DEFINE_INTERFACE_CLASSES(DEF) \
    DEF(base) \
    DEF(unit) \
    DEF(stmt) \
    DEF(stmt_list) \
    DEF(expr) \
    DEF(expr_list) \
    DEF(generic_list)


//---------------------------------------------------------------------
template<class T, class F>
static inline
std::pair<const std::type_index, std::function<const ast_base_t *(const std::any *)>>
to_any_visitor(F const &f)
{
    return
    {
        std::type_index(typeid(T)),
        [g = f](const std::any *a)
        {
            return  g(std::any_cast<T>(a));
        }
    };
}

#define DEF(name) \
to_any_visitor<ast_##name##_t>([](const ast_##name##_t *x) { return reinterpret_cast<const ast_base_t *>(x); }),

static
std::unordered_map<std::type_index, std::function<const ast_base_t *(const std::any *)>>
any_visitor
{
AST_DEFINE_INTERFACE_CLASSES(DEF)
};

#undef DEF


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
extern "C"
{

//---------------------------------------------------------------------
VOIDC_DLLEXPORT_BEGIN_FUNCTION

//---------------------------------------------------------------------
#define DEF(name) \
    VOIDC_DEFINE_INITIALIZE_IMPL(ast_##name##_t, v_ast_initialize_##name##_impl) \
    VOIDC_DEFINE_TERMINATE_IMPL(ast_##name##_t, v_ast_terminate_##name##_impl) \
    VOIDC_DEFINE_COPY_IMPL(ast_##name##_t, v_ast_copy_##name##_impl) \
    VOIDC_DEFINE_MOVE_IMPL(ast_##name##_t, v_ast_move_##name##_impl) \
    VOIDC_DEFINE_EMPTY_IMPL(ast_##name##_t, v_ast_empty_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(ast_##name##_t, v_ast_std_any_get_pointer_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(ast_##name##_t, v_ast_std_any_set_pointer_##name##_impl)

AST_DEFINE_INTERFACE_CLASSES(DEF)

#undef DEF


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
const ast_base_t *
v_ast_std_any_get_base(const std::any *a)
{
    if (const auto it = any_visitor.find(std::type_index(a->type()));
        it != any_visitor.cend())
    {
        auto *ast = it->second(a);

        if (ast && *ast)  return ast;
    }

    return nullptr;
}


//---------------------------------------------------------------------
//- Properties ...
//---------------------------------------------------------------------
void
v_ast_set_property(const ast_base_t *ast, v_quark_t key, const std::any *val)
{
    if (val)
    {
        (*ast)->properties[key] = *val;
    }
    else
    {
        (*ast)->properties.erase(key);
    }
}

const std::any *
v_ast_get_property(const ast_base_t *ast, v_quark_t key)
{
    auto &props = (*ast)->properties;

    auto it = props.find(key);

    if (it != props.end())  return &it->second;
    else                    return nullptr;
}


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void
v_ast_make_unit(ast_unit_t *ret, const ast_stmt_list_t *stmt_list, int line, int column)
{
    *ret = std::make_shared<const ast_unit_data_t>(*stmt_list, line, column);
}

const ast_stmt_list_t *
v_ast_unit_get_stmt_list(const ast_unit_t *ptr)
{
    auto &r = static_cast<const ast_unit_data_t &>(**ptr);

    return &r.stmt_list;
}

int
v_ast_unit_get_line(const ast_unit_t *ptr)
{
    auto &r = static_cast<const ast_unit_data_t &>(**ptr);

    return r.line;
}

int
v_ast_unit_get_column(const ast_unit_t *ptr)
{
    auto &r = static_cast<const ast_unit_data_t &>(**ptr);

    return r.column;
}

//---------------------------------------------------------------------
void
v_ast_make_stmt_q(ast_stmt_t *ret, v_quark_t qvar, const ast_expr_t *expr)
{
    *ret = std::make_shared<const ast_stmt_data_t>(qvar, *expr);
}

void
v_ast_make_stmt(ast_stmt_t *ret, const char *var, const ast_expr_t *expr)
{
    v_ast_make_stmt_q(ret, v_quark_from_string(var), expr);
}

v_quark_t
v_ast_stmt_get_name_q(const ast_stmt_t *ptr)
{
    auto &r = static_cast<const ast_stmt_data_t &>(**ptr);

    return r.name;
}

const char *
v_ast_stmt_get_name(const ast_stmt_t *ptr)
{
    return v_quark_to_string(v_ast_stmt_get_name_q(ptr));
}

const ast_expr_t *
v_ast_stmt_get_expr(const ast_stmt_t *ptr)
{
    auto &r = static_cast<const ast_stmt_data_t &>(**ptr);

    return &r.expr;
}

//---------------------------------------------------------------------
void
v_ast_make_expr_call(ast_expr_t *ret, const ast_expr_t *fun, const ast_expr_list_t *list)
{
    *ret = std::make_shared<const ast_expr_call_data_t>(*fun, *list);
}

const ast_expr_t *
v_ast_expr_call_get_fun_expr(const ast_expr_t *ptr)
{
    auto &r = static_cast<const ast_expr_call_data_t &>(**ptr);

    return &r.fun_expr;
}

const ast_expr_list_t *
v_ast_expr_call_get_arg_list(const ast_expr_t *ptr)
{
    auto &r = static_cast<const ast_expr_call_data_t &>(**ptr);

    return &r.arg_list;
}

//---------------------------------------------------------------------
void
v_ast_make_expr_identifier_q(ast_expr_t *ret, v_quark_t qname)
{
    *ret = std::make_shared<const ast_expr_identifier_data_t>(qname);
}

void
v_ast_make_expr_identifier(ast_expr_t *ret, const char *name)
{
    v_ast_make_expr_identifier_q(ret, v_quark_from_string(name));
}

v_quark_t
v_ast_expr_identifier_get_name_q(const ast_expr_t *ptr)
{
    auto &r = static_cast<const ast_expr_identifier_data_t &>(**ptr);

    return r.name;
}

const char *
v_ast_expr_identifier_get_name(const ast_expr_t *ptr)
{
    return v_quark_to_string(v_ast_expr_identifier_get_name_q(ptr));
}

//---------------------------------------------------------------------
void
v_ast_make_expr_integer(ast_expr_t *ret, intptr_t number)
{
    *ret = std::make_shared<const ast_expr_integer_data_t>(number);
}

intptr_t
v_ast_expr_integer_get_number(const ast_expr_t *ptr)
{
    auto &r = static_cast<const ast_expr_integer_data_t &>(**ptr);

    return r.number;
}

//---------------------------------------------------------------------
void
v_ast_make_expr_string(ast_expr_t *ret, const char *string)
{
    *ret = std::make_shared<const ast_expr_string_data_t>(string);
}

void
v_ast_make_expr_string_data(ast_expr_t *ret, const char *string, size_t size)
{
    *ret = std::make_shared<const ast_expr_string_data_t>(std::string{string, size});
}

const char *
v_ast_expr_string_get_string(const ast_expr_t *ptr)
{
    auto &r = static_cast<const ast_expr_string_data_t &>(**ptr);

    return r.string.c_str();
}

const char *
v_ast_expr_string_get_string_data(const ast_expr_t *ptr, size_t *psize)
{
    auto &r = static_cast<const ast_expr_string_data_t &>(**ptr);

    *psize = r.string.size();

    return r.string.c_str();
}

//---------------------------------------------------------------------
void
v_ast_make_expr_char(ast_expr_t *ret, char32_t c)
{
    *ret = std::make_shared<const ast_expr_char_data_t>(c);
}

char32_t
v_ast_expr_char_get_char(const ast_expr_t *ptr)
{
    auto &r = static_cast<const ast_expr_char_data_t &>(**ptr);

    return r.char_;
}


//---------------------------------------------------------------------
void
v_ast_make_expr_compiled(ast_expr_t *ret, v_type_t *t, LLVMValueRef v)
{
    *ret = std::make_shared<const ast_expr_compiled_data_t>(t, v);
}

v_type_t *
v_ast_expr_compiled_get_type(const ast_expr_t *ptr)
{
    auto &r = static_cast<const ast_expr_compiled_data_t &>(**ptr);

    return (v_type_t *)r.void_type;
}

LLVMValueRef
v_ast_expr_compiled_get_value(const ast_expr_t *ptr)
{
    auto &r = static_cast<const ast_expr_compiled_data_t &>(**ptr);

    return (LLVMValueRef)r.void_value;
}


//---------------------------------------------------------------------
//- Generics ...
//---------------------------------------------------------------------
#define AST_DEFINE_MAKE_GENERIC_IMPL(ast_sptr_t, name_, fun_name) \
void fun_name(ast_sptr_t *ret, const ast_generic_vtable_t *vtab, size_t size) \
{ \
    *ret = std::make_shared<const ast_##name_##generic_data_t>(vtab, size); \
}

#define AST_DEFINE_GENERIC_GET_VTABLE_IMPL(ast_sptr_t, name_, fun_name) \
const ast_generic_vtable_t *fun_name(const ast_sptr_t *ptr) \
{ \
    auto &r = static_cast<const ast_##name_##generic_data_t &>(**ptr); \
    return r.vtable; \
}

#define AST_DEFINE_GENERIC_GET_OBJECT_IMPL(ast_sptr_t, name_, fun_name) \
void *fun_name(const ast_sptr_t *ptr) \
{ \
    auto &r = static_cast<const ast_##name_##generic_data_t &>(**ptr); \
    return r.object; \
}


#define AST_DEFINE_GENERIC_MKGVGO_IMPL(ast_sptr_t, name_) \
\
    AST_DEFINE_MAKE_GENERIC_IMPL(ast_sptr_t, name_, v_ast_make_##name_##generic_impl) \
    AST_DEFINE_GENERIC_GET_VTABLE_IMPL(ast_sptr_t, name_, v_ast_##name_##generic_get_vtable_impl) \
    AST_DEFINE_GENERIC_GET_OBJECT_IMPL(ast_sptr_t, name_, v_ast_##name_##generic_get_object_impl)


//---------------------------------------------------------------------
AST_DEFINE_GENERIC_MKGVGO_IMPL(ast_base_t,)

AST_DEFINE_GENERIC_MKGVGO_IMPL(ast_unit_t, unit_)
AST_DEFINE_GENERIC_MKGVGO_IMPL(ast_stmt_t, stmt_)
AST_DEFINE_GENERIC_MKGVGO_IMPL(ast_expr_t, expr_)


//---------------------------------------------------------------------
//- Visitors ...
//---------------------------------------------------------------------
v_quark_t
v_ast_base_get_tag(const ast_base_t *ptr)
{
    return (*ptr)->tag();
}


//---------------------------------------------------------------------
#define DEF(name) \
v_quark_t v_ast_##name##_tag;

    DEFINE_AST_TAGS(DEF)

#undef DEF


//---------------------------------------------------------------------
void
v_ast_accept_visitor(const ast_base_t *obj, const visitor_t *vis)
{
    voidc_visitor_data_t::visit(*vis, *obj);                //- Sic!!!
}


//---------------------------------------------------------------------
//- Lists ...
//---------------------------------------------------------------------
#define AST_DEFINE_LIST_APPEND_IMPL(list_data_t, fun_name) \
void fun_name(std::shared_ptr<const list_data_t> *ret, \
              const std::shared_ptr<const list_data_t> *list, \
              const std::shared_ptr<const list_data_t::item_t> *items, size_t count) \
{ \
    (*ret) = std::make_shared<const list_data_t>(*list, items, count); \
}

#define AST_DEFINE_LIST_GET_SIZE_IMPL(list_data_t, fun_name) \
size_t fun_name(const std::shared_ptr<const list_data_t> *list) \
{ \
    return (*list)->data.size(); \
}

#define AST_DEFINE_LIST_GET_ITEM_IMPL(list_data_t, fun_name) \
const std::shared_ptr<const list_data_t::item_t> * \
fun_name(const std::shared_ptr<const list_data_t> *list, size_t idx) \
{ \
    return &(*list)->data[idx]; \
}


#define AST_DEFINE_LIST_AGSGI_IMPL(list_data_t, name) \
\
    AST_DEFINE_LIST_APPEND_IMPL(list_data_t, v_ast_list_append_##name##_impl) \
    AST_DEFINE_LIST_GET_SIZE_IMPL(list_data_t, v_ast_list_get_size_##name##_impl) \
    AST_DEFINE_LIST_GET_ITEM_IMPL(list_data_t, v_ast_list_get_item_##name##_impl)


//---------------------------------------------------------------------
void
v_ast_make_list_nil_stmt_list_impl(ast_stmt_list_t *ret)
{
    (*ret) = std::make_shared<const ast_stmt_list_data_t>();
}

void
v_ast_make_list_stmt_list_impl(ast_stmt_list_t *ret,
                               const ast_stmt_t *items, size_t count)
{
    (*ret) = std::make_shared<const ast_stmt_list_data_t>(items, count);
}

AST_DEFINE_LIST_AGSGI_IMPL(ast_stmt_list_data_t, stmt_list)


//---------------------------------------------------------------------
void
v_ast_make_list_nil_expr_list_impl(ast_expr_list_t *ret)
{
    (*ret) = std::make_shared<const ast_expr_list_data_t>();
}

void
v_ast_make_list_expr_list_impl(ast_expr_list_t *ret,
                               const ast_expr_t *items, size_t count)
{
    (*ret) = std::make_shared<const ast_expr_list_data_t>(items, count);
}

AST_DEFINE_LIST_AGSGI_IMPL(ast_expr_list_data_t, expr_list)


//---------------------------------------------------------------------
void
v_ast_make_list_nil_generic_list_impl(ast_generic_list_t *ret, v_quark_t tag)
{
    (*ret) = std::make_shared<const ast_generic_list_data_t>(tag);
}

void
v_ast_make_list_generic_list_impl(ast_generic_list_t *ret, v_quark_t tag,
                                  const ast_base_t *items, size_t count)
{
    (*ret) = std::make_shared<const ast_generic_list_data_t>(tag, items, count);
}

AST_DEFINE_LIST_AGSGI_IMPL(ast_generic_list_data_t, generic_list)


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END


//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void
v_ast_static_initialize(void)
{
    static_assert((sizeof(ast_base_t) % sizeof(intptr_t)) == 0);

    auto &vctx = *voidc_global_ctx_t::voidc;

    v_type_t *content_type = vctx.make_array_type(vctx.intptr_t_type, sizeof(ast_base_t)/sizeof(intptr_t));

#define DEF(name) \
    static_assert(sizeof(ast_base_t) == sizeof(ast_##name##_t)); \
    auto name##_q = v_quark_from_string("v_ast_" #name "_t"); \
    auto name##_type = vctx.make_struct_type(name##_q); \
    name##_type->set_body(&content_type, 1, false); \
    vctx.initialize_type(name##_q, name##_type);

    DEF(base)

    DEF(unit)

    DEF(stmt)
    DEF(stmt_list)

    DEF(expr)
    DEF(expr_list)

    DEF(generic_list)

#undef DEF

#define DEF(name) \
    v_ast_##name##_tag = v_quark_from_string(#name);

    DEFINE_AST_TAGS(DEF)

#undef DEF
}


//---------------------------------------------------------------------
void
v_ast_static_terminate(void)
{
}


