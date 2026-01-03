//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_visitor.h"

#include "voidc_util.h"
#include "voidc_types.h"
#include "voidc_target.h"
#include "voidc_compiler.h"

#include <cstdio>
#include <cassert>


//-----------------------------------------------------------------
//- ...
//-----------------------------------------------------------------
static void
visitor_visit_default(void *, const visitor_t *vis, const ast_base_t *ast)
{
    auto q = (*ast)->tag();

//  printf("visit: %p, %s\n", &vis, v_quark_to_string(q));

    auto &[void_fun, aux] = (*vis)->void_methods.at(q);

//  printf("visit: %p, %s, %p, %p\n", &vis, v_quark_to_string(q), void_fun, aux);

    typedef void (*FunT)(void *, const visitor_t *, const ast_base_t *);

    reinterpret_cast<FunT>(void_fun)(aux, vis, ast);
}

//-----------------------------------------------------------------
//- ...
//-----------------------------------------------------------------
voidc_visitor_data_t::voidc_visitor_data_t()
  : visit_fun(visitor_visit_default),
    visit_aux(0)
{}

//-----------------------------------------------------------------
void
voidc_visitor_data_t::static_initialize(void)
{
    static_assert((sizeof(visitor_t) % sizeof(intptr_t)) == 0);

    auto &vctx = *voidc_global_ctx_t::voidc;

    v_type_t *content_type = vctx.make_array_type(vctx.intptr_t_type, sizeof(visitor_t)/sizeof(intptr_t));

    auto q = v_quark_from_string("voidc_visitor_t");

    auto visitor_type = vctx.make_struct_type(q);

    visitor_type->set_body(&content_type, 1, false);

    vctx.initialize_type(q, visitor_type);
}

//-----------------------------------------------------------------
void
voidc_visitor_data_t::static_terminate(void)
{
}


//-----------------------------------------------------------------
//- ...
//-----------------------------------------------------------------
visitor_visit_t
voidc_visitor_data_t::get_visit_hook(void **paux) const
{
    if (paux) *paux = visit_aux;

    return visit_fun;
}

//---------------------------------------------------------------------
voidc_visitor_data_t
voidc_visitor_data_t::set_visit_hook(visitor_visit_t fun, void *aux) const
{
    return  voidc_visitor_data_t(void_methods, fun, aux);
}


//---------------------------------------------------------------------
//- !!!
//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION

//---------------------------------------------------------------------
VOIDC_DEFINE_INITIALIZE_IMPL(visitor_t, voidc_initialize_visitor_impl)
VOIDC_DEFINE_TERMINATE_IMPL(visitor_t, voidc_terminate_visitor_impl)
VOIDC_DEFINE_COPY_IMPL(visitor_t, voidc_copy_visitor_impl)
VOIDC_DEFINE_MOVE_IMPL(visitor_t, voidc_move_visitor_impl)
VOIDC_DEFINE_EMPTY_IMPL(visitor_t, voidc_empty_visitor_impl)
VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(visitor_t, voidc_std_any_get_pointer_visitor_impl)
VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(visitor_t, voidc_std_any_set_pointer_visitor_impl)


//---------------------------------------------------------------------
void
voidc_make_visitor(visitor_t *ret)
{
    *ret = std::make_shared<const voidc_visitor_data_t>();
}


//---------------------------------------------------------------------
typedef void (*visitor_method_t)(void *, const visitor_t *, const ast_base_t *);

visitor_method_t
voidc_visitor_get_method(const visitor_t *ptr, v_quark_t quark, void **aux_ptr)
{
    if (auto *vm = (*ptr)->void_methods.find(quark))
    {
        auto [void_fun, aux] = *vm;

        if (aux_ptr)  *aux_ptr = aux;

        return visitor_method_t(void_fun);
    }
    else
    {
        return nullptr;
    }
}

void
voidc_visitor_set_method(visitor_t *dst, const visitor_t *src, v_quark_t quark, visitor_method_t fun, void *aux)
{
    auto visitor = (*src)->set_void_method(quark, (void *)fun, aux);

    *dst = std::make_shared<const voidc_visitor_data_t>(visitor);
}


//---------------------------------------------------------------------
visitor_visit_t
voidc_visitor_get_visit_hook(visitor_t *vis, void **paux)
{
    return (*vis)->get_visit_hook(paux);
}

void
voidc_visitor_set_visit_hook(visitor_t *dst, const visitor_t *src, visitor_visit_t fun, void *aux)
{
    auto visitor = (*src)->set_visit_hook(fun, aux);

    *dst = std::make_shared<const voidc_visitor_data_t>(visitor);
}


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


