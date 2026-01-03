//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_VISITOR_H
#define VOIDC_VISITOR_H

#include "voidc_ast.h"

#include <utility>
#include <string>

#include <immer/map.hpp>


//---------------------------------------------------------------------
class voidc_visitor_data_t;

typedef std::shared_ptr<const voidc_visitor_data_t> visitor_t;

extern "C"
{
    typedef void (*visitor_visit_t)(void *ctx, const visitor_t *vis, const ast_base_t *ast);
}


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class voidc_visitor_data_t
{
public:
    using void_methods_map_t = immer::map<v_quark_t, std::pair<void *, void *>>;

public:
    voidc_visitor_data_t();
    ~voidc_visitor_data_t() = default;

public:
    voidc_visitor_data_t(const voidc_visitor_data_t &vis)
      : _void_methods(vis.void_methods),
        visit_fun(vis.visit_fun),
        visit_aux(vis.visit_aux)
    {}

    voidc_visitor_data_t &operator=(const voidc_visitor_data_t &vis)
    {
        _void_methods = vis.void_methods;
        visit_fun = vis.visit_fun;
        visit_aux = vis.visit_aux;

        return *this;
    }

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    voidc_visitor_data_t set_void_method(v_quark_t q, void *void_fun, void *aux=nullptr) const
    {
        return  voidc_visitor_data_t(void_methods.set(q, {void_fun, aux}), visit_fun, visit_aux);
    }

public:
    const void_methods_map_t &void_methods = _void_methods;

public:
    visitor_visit_t      get_visit_hook(void **paux) const;
    voidc_visitor_data_t set_visit_hook(visitor_visit_t fun, void *aux) const;

    static
    void visit(const visitor_t &vis, const ast_base_t &ast)
    {
        vis->visit_fun(vis->visit_aux, &vis, &ast);
    }

private:
    void_methods_map_t _void_methods;

private:
    visitor_visit_t visit_fun;
    void           *visit_aux;

private:
    voidc_visitor_data_t(const void_methods_map_t &vm, visitor_visit_t fun, void *aux)
      : _void_methods(vm),
        visit_fun(fun),
        visit_aux(aux)
    {}
};


#endif  //- VOIDC_VISITOR_H
