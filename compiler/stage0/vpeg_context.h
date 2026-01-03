//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VPEG_CONTEXT_H
#define VPEG_CONTEXT_H

#include "voidc_quark.h"
#include "vpeg_parser.h"
#include "vpeg_grammar.h"

#include <cstdio>
#include <utility>
#include <array>
#include <map>

#include <immer/map.hpp>
#include <immer/vector.hpp>


//---------------------------------------------------------------------
namespace vpeg
{

extern "C" typedef int (*context_fgetc_fun_t)(void *data);


//---------------------------------------------------------------------
//- Parsing context
//---------------------------------------------------------------------
class context_data_t
{
public:
    context_data_t(context_fgetc_fun_t fun, void *data, const grammar_t &_grammar);

    context_data_t(std::FILE *_input, const grammar_t &_grammar);

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    static std::shared_ptr<context_data_t> current_ctx;

public:
    struct variables_t
    {
        grammar_data_t::values_map_t        values;
        immer::vector<std::array<size_t,2>> strings;
    };

    struct state_t
    {
        size_t      position;
        variables_t variables;
    };

public:
    size_t get_position(void) const { return position; }

    state_t get_state(void) const
    {
        return {position, variables};
    }

    void set_state(const state_t &st)
    {
        position  = st.position;
        variables = st.variables;
    }

public:
    char32_t get_character(void)
    {
        auto c = peek_character();

        position += 1;

        return c;
    }

    char32_t peek_character(void)
    {
        if (position == buffer.size())
        {
            buffer = buffer.push_back(read_character());
        }

        return  buffer[position];
    }

public:
    std::string take_string(size_t from, size_t to) const;

public:
    bool expect(char32_t c)
    {
        if (c == peek_character())
        {
            position += 1;      //- Sic!

            return true;
        }

        return false;
    }

public:
    variables_t variables;
    grammar_t   grammar;

public:     //- ?...
    std::map<std::tuple<size_t, v_quark_t>, std::pair<std::any, state_t>> memo;

public:
    size_t get_line_column(size_t pos, size_t *column) const;

    size_t get_buffer_size(void) const { return buffer.size(); }

private:
    const context_fgetc_fun_t fgetc_fun;

    void * const fgetc_fun_data;

private:
    size_t position = 0;

    immer::vector<char32_t> buffer;

private:
    char32_t read_character(void);

private:
    std::map<size_t, size_t> newlines = {{0,0}};

    bool cr_flag = false;

    size_t current_line = 1;
};


//---------------------------------------------------------------------
}   //- namespace vpeg


#endif      //- VPEG_CONTEXT_H

