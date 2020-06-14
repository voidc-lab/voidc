#include "voidc_ast.h"

#include "voidc_util.h"

#include <cassert>


//----------------------------------------------------------------------
static
char get_raw_character(const char * &p)
{
    assert(*p);

    char c = *p++;

    if (c == '\\')
    {
        c = *p++;

        switch(c)
        {
        case 'n':   c = '\n'; break;
        case 'r':   c = '\r'; break;
        case 't':   c = '\t'; break;
        }
    }

    return  c;
}

//----------------------------------------------------------------------
static inline
std::string make_arg_string(const std::string &vstr)
{
    const char *str = vstr.c_str();

    size_t len = 0;

    for (auto p=str; *p; ++p)
    {
        if (*p == '\\') ++p;        //- ?!?!?

        ++len;
    }

    std::string ret(len, ' ');

    char *dst = ret.data();

    for (int i=0; i<len; ++i) dst[i] = get_raw_character(str);

    return ret;
}

ast_arg_string_t::ast_arg_string_t(const std::string &vstr)
  : string(make_arg_string(vstr))
{}


//----------------------------------------------------------------------
extern "C"
{

//---------------------------------------------------------------------
VOIDC_DLLEXPORT_BEGIN_FUNCTION

//-----------------------------------------------------------------
#define DEF(name) \
    VOIDC_DEFINE_INITIALIZE_IMPL(ast_##name##_ptr_t, v_ast_initialize_##name##_impl) \
    VOIDC_DEFINE_RESET_IMPL(ast_##name##_ptr_t, v_ast_reset_##name##_impl) \
    VOIDC_DEFINE_COPY_IMPL(ast_##name##_ptr_t, v_ast_copy_##name##_impl) \
    VOIDC_DEFINE_MOVE_IMPL(ast_##name##_ptr_t, v_ast_move_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(ast_##name##_ptr_t, v_ast_std_any_get_pointer_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(ast_##name##_ptr_t, v_ast_std_any_set_pointer_##name##_impl)

    DEF(base)
    DEF(base_list)

    DEF(unit)
    DEF(stmt_list)
    DEF(stmt)
    DEF(call)
    DEF(arg_list)
    DEF(argument)

    DEF(generic)

#undef DEF


//----------------------------------------------------------------------
//- ...
//----------------------------------------------------------------------
void v_ast_make_unit(ast_unit_ptr_t *ret, const ast_stmt_list_ptr_t *stmt_list, int line, int column)
{
    *ret = std::make_shared<const ast_unit_t>(*stmt_list, line, column);
}

void v_ast_unit_get_stmt_list(const ast_unit_ptr_t *ptr, ast_stmt_list_ptr_t *list)
{
    auto pp = std::dynamic_pointer_cast<const ast_unit_t>(*ptr);
    assert(pp);

    *list = pp->stmt_list;
}

int v_ast_unit_get_line(const ast_unit_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const ast_unit_t>(*ptr);
    assert(pp);

    return pp->line;
}

int v_ast_unit_get_column(const ast_unit_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const ast_unit_t>(*ptr);
    assert(pp);

    return pp->column;
}

//------------------------------------------------------------------
void v_ast_make_stmt_list(ast_stmt_list_ptr_t *ret, const ast_stmt_list_ptr_t *list, const ast_stmt_ptr_t *stmt)
{
    *ret = std::make_shared<const ast_stmt_list_t>(*list, *stmt);
}

int v_ast_stmt_list_get_size(const ast_stmt_list_ptr_t *ptr)
{
    return (*ptr)->data.size();
}

void v_ast_stmt_list_get_stmt(const ast_stmt_list_ptr_t *ptr, int i, ast_stmt_ptr_t *ret)
{
    *ret = (*ptr)->data[i];
}

//------------------------------------------------------------------
void v_ast_make_stmt(ast_stmt_ptr_t *ret, const std::string *var, const ast_call_ptr_t *call)
{
    *ret = std::make_shared<const ast_stmt_t>(*var, *call);
}

void v_ast_stmt_get_var_name(const ast_stmt_ptr_t *ptr, std::string *var)
{
    auto pp = std::dynamic_pointer_cast<const ast_stmt_t>(*ptr);
    assert(pp);

    *var = pp->var_name;
}

void v_ast_stmt_get_call(const ast_stmt_ptr_t *ptr, ast_call_ptr_t *call)
{
    auto pp = std::dynamic_pointer_cast<const ast_stmt_t>(*ptr);
    assert(pp);

    *call = pp->call;
}

//------------------------------------------------------------------
void v_ast_make_call(ast_call_ptr_t *ret, const std::string *fun, const ast_arg_list_ptr_t *list)
{
    *ret = std::make_shared<const ast_call_t>(*fun, *list);
}

void v_ast_call_get_fun_name(const ast_call_ptr_t *ptr, std::string *fun)
{
    auto pp = std::dynamic_pointer_cast<const ast_call_t>(*ptr);
    assert(pp);

    *fun = pp->fun_name;
}

void v_ast_call_get_arg_list(const ast_call_ptr_t *ptr, ast_arg_list_ptr_t *list)
{
    auto pp = std::dynamic_pointer_cast<const ast_call_t>(*ptr);
    assert(pp);

    *list = pp->arg_list;
}

//------------------------------------------------------------------
void v_ast_make_arg_list(ast_arg_list_ptr_t *ret, const ast_argument_ptr_t *list, int count)
{
    *ret = std::make_shared<const ast_arg_list_t>(list, count);
}

int v_ast_arg_list_get_count(const ast_arg_list_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const ast_arg_list_t>(*ptr);
    assert(pp);

    return  int(pp->data.size());
}

void v_ast_arg_list_get_args(const ast_arg_list_ptr_t *ptr, ast_argument_ptr_t *ret)
{
    auto pp = std::dynamic_pointer_cast<const ast_arg_list_t>(*ptr);
    assert(pp);

    std::copy(pp->data.begin(), pp->data.end(), ret);
}

//------------------------------------------------------------------
void v_ast_make_arg_identifier(ast_argument_ptr_t *ret, const std::string *name)
{
    *ret = std::make_shared<const ast_arg_identifier_t>(*name);
}

void v_ast_arg_identifier_get_name(const ast_argument_ptr_t *ptr, std::string *name)
{
    auto pp = std::dynamic_pointer_cast<const ast_arg_identifier_t>(*ptr);
    assert(pp);

    *name = pp->name;
}

//------------------------------------------------------------------
void v_ast_make_arg_integer(ast_argument_ptr_t *ret, intptr_t number)
{
    *ret = std::make_shared<const ast_arg_integer_t>(number);
}

intptr_t v_ast_arg_integer_get_number(const ast_argument_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const ast_arg_integer_t>(*ptr);
    assert(pp);

    return  pp->number;
}

//------------------------------------------------------------------
void v_ast_make_arg_string(ast_argument_ptr_t *ret, const std::string *string)
{
    *ret = std::make_shared<const ast_arg_string_t>(*string);
}

void v_ast_arg_string_get_string(const ast_argument_ptr_t *ptr, std::string *string)
{
    auto pp = std::dynamic_pointer_cast<const ast_arg_string_t>(*ptr);
    assert(pp);

    *string = pp->string;
}

//------------------------------------------------------------------
void v_ast_make_arg_char(ast_argument_ptr_t *ret, char32_t c)
{
    *ret = std::make_shared<const ast_arg_char_t>(c);
}

char32_t v_ast_arg_char_get_char(const ast_argument_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const ast_arg_char_t>(*ptr);
    assert(pp);

    return  pp->c;
}


//------------------------------------------------------------------
//- Generics ...
//------------------------------------------------------------------
void v_ast_make_generic(ast_generic_ptr_t *ret, const ast_generic_vtable *vtab, void *obj)
{
    *ret = std::make_shared<const ast_generic_t>(vtab, obj);
}

//--------------------------------------------------------------
void v_ast_make_unit_generic(ast_unit_ptr_t *ret, const ast_generic_ptr_t *gen)
{
    *ret = std::make_shared<const ast_unit_generic_t>(*gen);
}

void v_ast_make_stmt_generic(ast_stmt_ptr_t *ret, const ast_generic_ptr_t *gen)
{
    *ret = std::make_shared<const ast_stmt_generic_t>(*gen);
}

void v_ast_make_call_generic(ast_call_ptr_t *ret, const ast_generic_ptr_t *gen)
{
    *ret = std::make_shared<const ast_call_generic_t>(*gen);
}

void v_ast_make_argument_generic(ast_argument_ptr_t *ret, const ast_generic_ptr_t *gen)
{
    *ret = std::make_shared<const ast_argument_generic_t>(*gen);
}

//------------------------------------------------------------------
const ast_generic_vtable *
v_ast_generic_get_vtable(const ast_generic_ptr_t *ptr)
{
    return (*ptr)->vtable;
}

const void *
v_ast_generic_get_object(const ast_generic_ptr_t *ptr)
{
    return (*ptr)->object;
}


//------------------------------------------------------------------
//- Casts ...
//------------------------------------------------------------------
#define DEF_UPCAST(name) \
void v_ast_upcast_##name##_impl(ast_base_ptr_t *ret, const ast_##name##_ptr_t *ptr) \
{ \
    *ret = std::static_pointer_cast<const ast_base_t>(*ptr); \
}

#define DEF_DOWNCAST(name) \
void v_ast_downcast_##name##_impl(const ast_base_ptr_t *ptr, ast_##name##_ptr_t *ret) \
{ \
    *ret = std::dynamic_pointer_cast<const ast_##name##_t>(*ptr); \
}

#define DEF(name) \
    DEF_UPCAST(name) \
    DEF_DOWNCAST(name)

    DEF(base)           //- ?...
    DEF(base_list)

    DEF(unit)
    DEF(stmt_list)
    DEF(stmt)
    DEF(call)
    DEF(arg_list)
    DEF(argument)

    DEF(generic)

#undef DEF

#undef DEF_DOWNCAST
#undef DEF_UPCAST


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END


//----------------------------------------------------------------------
}   //- extern "C"


//----------------------------------------------------------------------
//- ...
//----------------------------------------------------------------------
void v_ast_static_initialize(void)
{
    static_assert((sizeof(ast_base_ptr_t) % sizeof(char *)) == 0);

    auto char_ptr_type = LLVMPointerType(compile_ctx_t::char_type, 0);

    auto content_type = LLVMArrayType(char_ptr_type, sizeof(ast_base_ptr_t)/sizeof(char *));

    auto gctx = LLVMGetGlobalContext();

#define DEF(name) \
    static_assert(sizeof(ast_base_ptr_t) == sizeof(ast_##name##_ptr_t)); \
    auto opaque_##name##_ptr_type = LLVMStructCreateNamed(gctx, "struct.v_ast_opaque_" #name "_ptr"); \
    LLVMStructSetBody(opaque_##name##_ptr_type, &content_type, 1, false); \
    v_add_symbol("v_ast_opaque_" #name "_ptr", compile_ctx_t::LLVMOpaqueType_type, (void *)opaque_##name##_ptr_type);

    DEF(base)
    DEF(base_list)

    DEF(unit)
    DEF(stmt_list)
    DEF(stmt)
    DEF(call)
    DEF(arg_list)
    DEF(argument)

    DEF(generic)

#undef DEF

}


//----------------------------------------------------------------------
void v_ast_static_terminate(void)
{
}


