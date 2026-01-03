//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "vpeg_context.h"

#include "voidc_target.h"
#include "voidc_util.h"

#include <llvm-c/Core.h>
#include <llvm-c/Support.h>


//---------------------------------------------------------------------
namespace vpeg
{


//-----------------------------------------------------------------
context_t context_data_t::current_ctx;


//-----------------------------------------------------------------
context_data_t::context_data_t(context_fgetc_fun_t fun, void *data, const grammar_t &_grammar)
  : grammar(_grammar),
    fgetc_fun(fun),
    fgetc_fun_data(data)
{}


//-----------------------------------------------------------------
context_data_t::context_data_t(FILE *input, const grammar_t &_grammar)
  : context_data_t(reinterpret_cast<context_fgetc_fun_t>(std::fgetc), input, _grammar)
{}


//-----------------------------------------------------------------
void context_data_t::static_initialize(void)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    v_type_t *content_type = vctx.make_array_type(vctx.intptr_t_type, sizeof(parser_t)/sizeof(intptr_t));

#define DEF(name) \
    static_assert(sizeof(parser_t) == sizeof(name##_t)); \
    auto name##_q = v_quark_from_string("v_peg_" #name "_t"); \
    auto name##_type = vctx.make_struct_type(name##_q); \
    name##_type->set_body(&content_type, 1, false); \
    vctx.initialize_type(name##_q, name##_type);

    DEF(context)

#undef DEF
}

//-----------------------------------------------------------------
void context_data_t::static_terminate(void)
{
}


//-----------------------------------------------------------------
std::string context_data_t::take_string(size_t from, size_t to) const
{
    auto src = buffer.begin() + from;

    size_t count = to - from;

    size_t len = 0;

    auto s = src;

    for (size_t i=0; i<count; ++i)
    {
        char32_t c = *s++;

        if (c <= 0x7FF)
        {
            if (c <= 0x7F)  len += 1;
            else            len += 2;
        }
        else
        {
            if (c <= 0xFFFF)  len += 3;
            else              len += 4;
        }
    }

    std::string dst(len, ' ');

    char *d = dst.data();

    s = src;

    for (size_t i=0; i<count; ++i)
    {
        char32_t c = *s++;

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

        d += r + 1;
    }

    return dst;
}


//-----------------------------------------------------------------
size_t context_data_t::get_line_column(size_t pos, size_t *column) const
{
    auto it = newlines.upper_bound(pos);

    it = std::prev(it);

    assert(it != newlines.end());

    auto &[lpos, lnum] = *it;

    if (column) *column = pos - lpos + 1;

    return  lnum + 1;
}


//-----------------------------------------------------------------
char32_t context_data_t::read_character(void)
{
    //- First, obtain a utf-8 codepoint

    int c_eof = fgetc_fun(fgetc_fun_data);

    if (c_eof == EOF) return char32_t(-1);      //- Sic!

    uint8_t c0 = uint8_t(c_eof);

    int n;

    uint32_t r;

    if (c0 < 0xE0)
    {
        if (c0 < 0xC0)  { r = c0;           n = 0; }
        else            { r = (c0 & 0x1F);  n = 1; }
    }
    else
    {
        if (c0 < 0xF0)  { r = (c0 & 0x0F);  n = 2; }
        else            { r = (c0 & 0x07);  n = 3; }
    }

    for(; n; --n)
    {
        c0 = uint8_t(fgetc_fun(fgetc_fun_data));

        r = (r << 6) | (c0 & 0x3F);
    }

    //- Now, check for EOL

    if (r == U'\n')
    {
        newlines[buffer.size()+1] = current_line++;

        cr_flag = false;
    }
    else
    {
        if (cr_flag)
        {
            newlines[buffer.size()] = current_line++;
        }

        cr_flag = (r == U'\r');
    }

    //- ...

    return r;
}


//---------------------------------------------------------------------
}   //- namespace vpeg


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
extern "C"
{

using namespace vpeg;

//---------------------------------------------------------------------
VOIDC_DLLEXPORT_BEGIN_FUNCTION

//-----------------------------------------------------------------
#define DEF(name) \
    VOIDC_DEFINE_INITIALIZE_IMPL(name##_t, v_peg_initialize_##name##_impl) \
    VOIDC_DEFINE_TERMINATE_IMPL(name##_t, v_peg_terminate_##name##_impl) \
    VOIDC_DEFINE_COPY_IMPL(name##_t, v_peg_copy_##name##_impl) \
    VOIDC_DEFINE_MOVE_IMPL(name##_t, v_peg_move_##name##_impl) \
    VOIDC_DEFINE_EMPTY_IMPL(name##_t, v_peg_empty_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(name##_t, v_peg_std_any_get_pointer_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(name##_t, v_peg_std_any_set_pointer_##name##_impl)

    DEF(context)

#undef DEF


//-----------------------------------------------------------------
context_t *v_peg_get_context(void)
{
    return  &context_data_t::current_ctx;
}


//-----------------------------------------------------------------
void v_peg_make_context(context_t *ret, context_fgetc_fun_t fun, void *data, const grammar_t *grm)
{
    *ret = std::make_shared<context_data_t>(fun, data, *grm);
}


//-----------------------------------------------------------------
void v_peg_parse(std::any *ret, v_quark_t q)
{
    auto &ctx = context_data_t::current_ctx;

    *ret = ctx->grammar->parse(ctx->grammar, q, ctx);
}

//-----------------------------------------------------------------
void v_peg_memo_clear(void)
{
    auto &ctx = context_data_t::current_ctx;

    ctx->memo.clear();
}


//---------------------------------------------------------------------
grammar_t *v_peg_get_grammar(void)
{
    if (!context_data_t::current_ctx)   return nullptr;         //- Sic !!!

    return  &context_data_t::current_ctx->grammar;
}

void v_peg_take_string(std::string *ret, size_t from, size_t to)
{
    *ret = context_data_t::current_ctx->take_string(from, to);
}

size_t v_peg_get_line_column(size_t pos, size_t *column)
{
    return  context_data_t::current_ctx->get_line_column(pos, column);
}

size_t v_peg_get_position(void)
{
    return context_data_t::current_ctx->get_position();
}


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


