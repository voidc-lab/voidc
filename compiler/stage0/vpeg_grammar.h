//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VPEG_GRAMMAR_H
#define VPEG_GRAMMAR_H

#include "voidc_quark.h"
#include "vpeg_parser.h"

#include <utility>

#include <immer/map.hpp>


//---------------------------------------------------------------------
namespace vpeg
{

class grammar_data_t;

typedef std::shared_ptr<const grammar_data_t> grammar_t;

extern "C"
{
    typedef void (*grammar_action_fun_t)(std::any *ret, void *aux, const std::any *args, size_t count);

    typedef void (*grammar_parse_t)(void *aux, std::any *ret, grammar_t *grm, v_quark_t q, context_t *ctx);
}


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class grammar_data_t
{
public:
    using parsers_map_t = immer::map<v_quark_t, std::pair<parser_t, bool>>;
    using actions_map_t = immer::map<v_quark_t, std::pair<grammar_action_fun_t, void *>>;
    using values_map_t  = immer::map<v_quark_t, std::any>;

public:
    grammar_data_t();
    ~grammar_data_t() = default;

public:
    grammar_data_t(const grammar_data_t &gr)
      : _parsers(gr.parsers),
        _actions(gr.actions),
        _values(gr.values),
        parse_fun(gr.parse_fun),
        parse_aux(gr.parse_aux)
    {}

    grammar_data_t &operator=(const grammar_data_t &gr)
    {
        _parsers  = gr.parsers;
        _actions  = gr.actions;
        _values   = gr.values;
        parse_fun = gr.parse_fun;
        parse_aux = gr.parse_aux;

        return *this;
    }

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    grammar_data_t set_parser(v_quark_t q_name, const parser_t &parser, bool leftrec=false) const
    {
        return  grammar_data_t(_parsers.set(q_name, {parser, leftrec}), actions, values, parse_fun, parse_aux);
    }

    grammar_data_t set_parser(const char *name, const parser_t &parser, bool leftrec=false) const
    {
        return  set_parser(v_quark_from_string(name), parser, leftrec);
    }

    grammar_data_t set_action(v_quark_t q_name, grammar_action_fun_t fun, void *aux=nullptr) const
    {
        return  grammar_data_t(parsers, _actions.set(q_name, {fun, aux}), values, parse_fun, parse_aux);
    }

    grammar_data_t set_action(const char *name, grammar_action_fun_t fun, void *aux=nullptr) const
    {
        return  set_action(v_quark_from_string(name), fun, aux);
    }

    grammar_data_t set_value(v_quark_t q_name, const std::any &val) const
    {
        return  grammar_data_t(parsers, actions, _values.set(q_name, val), parse_fun, parse_aux);
    }

    grammar_data_t set_value(const char *name, const std::any &val) const
    {
        return  set_value(v_quark_from_string(name), val);
    }

public:
    grammar_data_t erase_parser(v_quark_t q_name) const
    {
        return  grammar_data_t(_parsers.erase(q_name), actions, values, parse_fun, parse_aux);
    }

    grammar_data_t erase_parser(const char *name) const
    {
        return  erase_parser(v_quark_from_string(name));
    }

    grammar_data_t erase_action(v_quark_t q_name) const
    {
        return  grammar_data_t(parsers, _actions.erase(q_name), values, parse_fun, parse_aux);
    }

    grammar_data_t erase_action(const char *name) const
    {
        return  erase_action(v_quark_from_string(name));
    }

    grammar_data_t erase_value(v_quark_t q_name) const
    {
        return  grammar_data_t(parsers, actions, _values.erase(q_name), parse_fun, parse_aux);
    }

    grammar_data_t erase_value(const char *name) const
    {
        return  erase_value(v_quark_from_string(name));
    }

public:
    grammar_parse_t get_parse_hook(void **paux) const;
    grammar_data_t  set_parse_hook(grammar_parse_t fun, void *aux) const;

    static
    std::any parse(grammar_t &grm, v_quark_t q, context_t &ctx)
    {
        std::any ret;

        grm->parse_fun(grm->parse_aux, &ret, &grm, q, &ctx);

        return ret;
    }

public:
    const parsers_map_t &parsers = _parsers;
    const actions_map_t &actions = _actions;
    const values_map_t  &values  = _values;

private:
    parsers_map_t _parsers;
    actions_map_t _actions;
    values_map_t  _values;

private:
    grammar_parse_t parse_fun;
    void           *parse_aux;

private:
    explicit grammar_data_t(const parsers_map_t &p, const actions_map_t &a, const values_map_t &v,
                            grammar_parse_t fun, void *aux)
      : _parsers(p),
        _actions(a),
        _values(v),
        parse_fun(fun),
        parse_aux(aux)
    {}
};


//---------------------------------------------------------------------
}   //- namespace vpeg


#endif      //- VPEG_GRAMMAR_H

