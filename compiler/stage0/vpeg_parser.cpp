//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "vpeg_parser.h"

#include "vpeg_grammar.h"
#include "vpeg_context.h"
#include "voidc_target.h"
#include "voidc_util.h"

#include <llvm-c/Core.h>

#include <immer/array_transient.hpp>


//---------------------------------------------------------------------
//- Some utility
//---------------------------------------------------------------------
static char32_t
read_utf8_codepoint(const char * &utf8)
{
    if (!*utf8) return (char32_t)0;

    uint8_t c0 = (uint8_t)*utf8++;

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
        c0 = (uint8_t)*utf8++;

        r = (r << 6) | (c0 & 0x3F);
    }

    return (char32_t)r;
}


//---------------------------------------------------------------------
namespace vpeg
{


//---------------------------------------------------------------------
//- Parsers...
//---------------------------------------------------------------------
std::any choice_parser_data_t::parse(context_t &ctx) const
{
//  printf("(%d): choice?\n", (int)ctx->get_position());

    std::any r;

    for (auto &it : array)
    {
        r = it->parse(ctx);

        if (r.has_value())  break;
    }

    return r;
}

//---------------------------------------------------------------------
std::any sequence_parser_data_t::parse(context_t &ctx) const
{
//  printf("(%d): sequence?\n", (int)ctx->get_position());

    auto st = ctx->get_state();

    struct {} const dummy;

    std::any r(dummy);      //- Sic!

    for (auto &it : array)
    {
        r = it->parse(ctx);

        if (!r.has_value())
        {
            ctx->set_state(st);

            break;
        }
    }

    return r;
}


//-------------------------------------------------------------
std::any and_parser_data_t::parse(context_t &ctx) const
{
    auto st = ctx->get_state();

    auto r = parser->parse(ctx);

    ctx->set_state(st);

    return r;       //- ???
}

//-------------------------------------------------------------
std::any not_parser_data_t::parse(context_t &ctx) const
{
    auto st = ctx->get_state();

    auto r = parser->parse(ctx);

    ctx->set_state(st);

    struct {} const dummy;

    if (r.has_value())  r.reset();
    else                r = dummy;

    return r;
}

//-------------------------------------------------------------
std::any question_parser_data_t::parse(context_t &ctx) const
{
    auto r = parser->parse(ctx);

    if (r.has_value())  return r;

    struct {} const dummy;

    return dummy;
}

//-------------------------------------------------------------
std::any star_parser_data_t::parse(context_t &ctx) const
{
    struct {} const dummy;

    std::any ret = dummy;

    for(;;)
    {
        auto r = parser->parse(ctx);

        if (!r.has_value()) break;

        ret = r;
    }

    return ret;
}

//-------------------------------------------------------------
std::any plus_parser_data_t::parse(context_t &ctx) const
{
    std::any ret;

    for(;;)
    {
        auto r = parser->parse(ctx);

        if (!r.has_value()) break;

        ret = r;
    }

    return ret;
}


//-------------------------------------------------------------
std::any catch_variable_parser_data_t::parse(context_t &ctx) const
{
    auto ret = parser->parse(ctx);

    if (ret.has_value())
    {
        auto &vmap = ctx->variables.values;

        vmap = vmap.set(q_name, ret);
    }

    return ret;
}

//-------------------------------------------------------------
std::any catch_string_parser_data_t::parse(context_t &ctx) const
{
    auto pos = ctx->get_position();

    auto ret = parser->parse(ctx);

    if (ret.has_value())
    {
        auto epos = ctx->get_position();

        auto &svec = ctx->variables.strings;

        svec = svec.push_back({pos, epos});
    }

    return ret;         //- ?
}


//-------------------------------------------------------------
std::any identifier_parser_data_t::parse(context_t &ctx) const
{
    return grammar_data_t::parse(ctx->grammar, q_ident, ctx);
}

//-------------------------------------------------------------
std::any backref_parser_data_t::parse(context_t &ctx) const
{
    auto st = ctx->get_state();

    auto v = st.variables.strings[number];

    if (number == 0)  v[1] = st.position;

    auto utf8 = ctx->take_string(v[0], v[1]);

    const char *str = utf8.c_str();

    while (char32_t ucs4 = read_utf8_codepoint(str))
    {
        if (!ctx->expect(ucs4))
        {
            ctx->set_state(st);

            return std::any();
        }
    }

    return utf8;
}

//-------------------------------------------------------------
std::any action_parser_data_t::parse(context_t &ctx) const
{
    return action->act(ctx);
}

//-------------------------------------------------------------
std::any literal_parser_data_t::parse(context_t &ctx) const
{
    auto st = ctx->get_state();

    const char *str = utf8.c_str();

    while (char32_t ucs4 = read_utf8_codepoint(str))
    {
        if (!ctx->expect(ucs4))
        {
            ctx->set_state(st);

            return std::any();
        }
    }

    return utf8;
}

//-------------------------------------------------------------
std::any character_parser_data_t::parse(context_t &ctx) const
{
    if (!ctx->expect(ucs4))  return std::any();

    return (uint32_t)ucs4;
}

//-------------------------------------------------------------
static
immer::array<class_parser_data_t::range_t>
make_ranges_array(const char32_t (*list)[2], size_t count)
{
    auto tr = immer::array<class_parser_data_t::range_t>(count).transient();

    auto tmp = tr.data_mut();

    for (size_t i=0; i<count; ++i)
    {
        tmp[i] = {list[i][0], list[i][1]};
    }

    return tr.persistent();
}

class_parser_data_t::class_parser_data_t(const char32_t (*list)[2], size_t count)
  : ranges(make_ranges_array(list, count))
{}

std::any class_parser_data_t::parse(context_t &ctx) const
{
    auto ucs4 = ctx->peek_character();

    for (auto &it : ranges)
    {
        if (it[0] <= ucs4  &&  ucs4 <= it[1])
        {
            ctx->get_character();

            return (uint32_t)ucs4;
        }
    }

    return std::any();
}

//-------------------------------------------------------------
std::any dot_parser_data_t::parse(context_t &ctx) const
{
    auto ucs4 = ctx->peek_character();

    if (ucs4 == char32_t(-1)) return std::any();

    ctx->get_character();

    return (uint32_t)ucs4;
}


//-----------------------------------------------------------------
std::any call_action_data_t::act(context_t &ctx) const
{
    size_t N = args.size();

    auto a = std::make_unique<std::any[]>(N);

    for (size_t i=0; i<N; ++i)
    {
        a[i] = args[i]->value(ctx);
    }

//  printf("%s\n", v_quark_to_string(q_fun));

    std::any ret;

    auto [fun, aux] = ctx->grammar->actions[q_fun];

#ifndef NDEBUG

    if (!fun)   fprintf(stderr, "grammar action not found: %s\n", v_quark_to_string(q_fun));

    assert(fun && "grammar action not found");

#endif

    fun(&ret, aux, a.get(), N);

    return ret;
}


//-----------------------------------------------------------------
std::any identifier_argument_data_t::value(context_t &ctx) const
{
    return ctx->variables.values.at(q_ident);      //- ?...
}

//-----------------------------------------------------------------
std::any backref_argument_data_t::value(context_t &ctx) const
{
    auto v = ctx->variables.strings[number];

    if (number == 0)  v[1] = ctx->get_position();

    switch(b_kind)
    {
    case bk_string: return ctx->take_string(v[0], v[1]);
    case bk_start:  return v[0];
    case bk_end:    return v[1];
    }

    return std::any();      //- WTF?
}


//-----------------------------------------------------------------
//- ...
//-----------------------------------------------------------------
void parsers_static_initialize(void)
{
    static_assert((sizeof(parser_t) % sizeof(intptr_t)) == 0);

    auto &vctx = *voidc_global_ctx_t::voidc;

    v_type_t *content_type = vctx.make_array_type(vctx.intptr_t_type, sizeof(parser_t)/sizeof(intptr_t));

    auto q = v_quark_from_string;

#define DEF(name) \
    static_assert(sizeof(parser_t) == sizeof(name##_t)); \
    auto name##_q = q("v_peg_" #name "_t"); \
    auto name##_type = vctx.make_struct_type(name##_q); \
    name##_type->set_body(&content_type, 1, false); \
    vctx.initialize_type(name##_q, name##_type);

    DEF(parser)
    DEF(action)
    DEF(argument)

#undef DEF

    auto int_type = vctx.int_type;
    auto int_llvm_type = int_type->llvm_type();

#define DEF(name, kind) \
    auto name##_##kind##_q = q("v_peg_" #name "_kind_" #kind); \
    vctx.decls.constants_insert({name##_##kind##_q, int_type}); \
    vctx.constant_values.insert({name##_##kind##_q, \
        LLVMConstInt(int_llvm_type, name##_data_t::k_##kind, 0)});

    DEF(parser, choice)
    DEF(parser, sequence)
    DEF(parser, and)
    DEF(parser, not)
    DEF(parser, question)
    DEF(parser, star)
    DEF(parser, plus)
    DEF(parser, catch_variable)
    DEF(parser, catch_string)
    DEF(parser, identifier)
    DEF(parser, backref)
    DEF(parser, action)
    DEF(parser, literal)
    DEF(parser, character)
    DEF(parser, class)
    DEF(parser, dot)

    DEF(action, call)
    DEF(action, return)

    DEF(argument, identifier)
    DEF(argument, backref)
    DEF(argument, integer)
    DEF(argument, literal)
    DEF(argument, character)

#undef DEF

#define DEF(kind) \
    auto kind##_q = q("v_peg_backref_argument_kind_" #kind); \
    vctx.decls.constants_insert({kind##_q, int_type}); \
    vctx.constant_values.insert({kind##_q, \
        LLVMConstInt(int_llvm_type, backref_argument_data_t::bk_##kind, 0)});

    DEF(string)
    DEF(start)
    DEF(end)

#undef DEF

}

//-----------------------------------------------------------------
void parsers_static_terminate(void)
{
}


//---------------------------------------------------------------------
//- Some utility ...
//---------------------------------------------------------------------
template <typename T>
int
flatten_parser(const parser_t &from, parser_t * &to)
{
    int count = 0;

    if (from->kind() == T::kind_tag)
    {
        auto &fr = static_cast<const T &>(*from);

        for (auto &item : fr.array)
        {
            count += flatten_parser<T>(item, to);
        }
    }
    else
    {
        count = 1;

        if (to) *to++ = from;
    }

    return count;
}


//---------------------------------------------------------------------
}   //- namespace vpeg


//---------------------------------------------------------------------
//- !!!
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

    DEF(parser)
    DEF(action)
    DEF(argument)

#undef DEF

#define DEF_KIND(ptr_t, fun_name) \
int fun_name(const ptr_t *ptr) \
{ \
    return (*ptr)->kind(); \
}

#define DEF(name) \
    DEF_KIND(name##_t, v_peg_##name##_get_kind)

    DEF(parser)
    DEF(action)
    DEF(argument)

#undef DEF

#undef DEF_KIND


//-----------------------------------------------------------------
int
v_peg_parser_get_parsers_count(const parser_t *ptr)
{
    return int((*ptr)->parsers_count());
}

const parser_t *
v_peg_parser_get_parsers(const parser_t *ptr)
{
    return (*ptr)->get_parsers();
}

//-----------------------------------------------------------------
void
v_peg_make_choice_parser(parser_t *ret, const parser_t *list, int count)
{
    *ret = mk_choice_parser(list, size_t(count));
}

void
v_peg_choice_parser_append(parser_t *ret, const parser_t *head, const parser_t *tail)
{
    auto &h = static_cast<const choice_parser_data_t &>(**head);

    *ret = mk_choice_parser(h, *tail);
}


void
v_peg_make_sequence_parser(parser_t *ret, const parser_t *list, int count)
{
    *ret = mk_sequence_parser(list, size_t(count));
}

void
v_peg_sequence_parser_append(parser_t *ret, const parser_t *head, const parser_t *tail)
{
    auto &h = static_cast<const sequence_parser_data_t &>(**head);

    *ret = mk_sequence_parser(h, *tail);
}


//-----------------------------------------------------------------
void
v_peg_choice_parser_concat(parser_t *ret, const parser_t *head, const parser_t *tail)
{
    int count = 0;

    parser_t *pps = nullptr;

    count += flatten_parser<choice_parser_data_t>(*head, pps);
    count += flatten_parser<choice_parser_data_t>(*tail, pps);

    auto list = std::make_unique<parser_t[]>(count);

    pps = list.get();

    flatten_parser<choice_parser_data_t>(*head, pps);
    flatten_parser<choice_parser_data_t>(*tail, pps);

    *ret = mk_choice_parser(list.get(), count);
}

void
v_peg_sequence_parser_concat(parser_t *ret, const parser_t *head, const parser_t *tail)
{
    int count = 0;

    parser_t *pps = nullptr;

    count += flatten_parser<sequence_parser_data_t>(*head, pps);
    count += flatten_parser<sequence_parser_data_t>(*tail, pps);

    auto list = std::make_unique<parser_t[]>(count);

    pps = list.get();

    flatten_parser<sequence_parser_data_t>(*head, pps);
    flatten_parser<sequence_parser_data_t>(*tail, pps);

    *ret = mk_sequence_parser(list.get(), count);
}


void
v_peg_make_and_parser(parser_t *ret, const parser_t *ptr)
{
    *ret = mk_and_parser(*ptr);
}

void
v_peg_make_not_parser(parser_t *ret, const parser_t *ptr)
{
    *ret = mk_not_parser(*ptr);
}

void
v_peg_make_question_parser(parser_t *ret, const parser_t *ptr)
{
    *ret = mk_question_parser(*ptr);
}

void
v_peg_make_star_parser(parser_t *ret, const parser_t *ptr)
{
    *ret = mk_star_parser(*ptr);
}

void
v_peg_make_plus_parser(parser_t *ret, const parser_t *ptr)
{
    *ret = mk_plus_parser(*ptr);
}

void
v_peg_make_catch_variable_parser(parser_t *ret, const char *name, const parser_t *ptr)
{
    *ret = mk_catch_variable_parser(name, *ptr);
}

const char *
v_peg_catch_variable_parser_get_name(const parser_t *ptr)
{
    auto &r = static_cast<const catch_variable_parser_data_t &>(**ptr);

    return  v_quark_to_string(r.q_name);
}

void
v_peg_make_catch_string_parser(parser_t *ret, const parser_t *ptr)
{
    *ret = mk_catch_string_parser(*ptr);
}


void
v_peg_make_identifier_parser(parser_t *ret, const char *ident)
{
    *ret = mk_identifier_parser(ident);
}

const char *
v_peg_identifier_parser_get_identifier(const parser_t *ptr)
{
    auto &r = static_cast<const identifier_parser_data_t &>(**ptr);

    return  v_quark_to_string(r.q_ident);
}

void
v_peg_make_backref_parser(parser_t *ret, int number)
{
    *ret = mk_backref_parser(size_t(number));
}

int
v_peg_backref_parser_get_number(const parser_t *ptr)
{
    auto &r = static_cast<const backref_parser_data_t &>(**ptr);

    return int(r.number);
}

void
v_peg_make_action_parser(parser_t *ret, const action_t *ptr)
{
    *ret = mk_action_parser(*ptr);
}

const action_t *
v_peg_action_parser_get_action(const parser_t *ptr)
{
    auto &r = static_cast<const action_parser_data_t &>(**ptr);

    return &r.action;
}

void
v_peg_make_literal_parser(parser_t *ret, const char *utf8)
{
    *ret = mk_literal_parser(utf8);
}

const char *
v_peg_literal_parser_get_literal(const parser_t *ptr)
{
    auto &r = static_cast<const literal_parser_data_t &>(**ptr);

    return r.utf8.c_str();
}

void
v_peg_make_character_parser(parser_t *ret, char32_t ucs4)
{
    *ret = mk_character_parser(ucs4);
}

char32_t
v_peg_character_parser_get_character(const parser_t *ptr)
{
    auto &r = static_cast<const character_parser_data_t &>(**ptr);

    return r.ucs4;
}

void
v_peg_make_class_parser(parser_t *ret, const char32_t (*ranges)[2], int count)
{
    *ret = mk_class_parser(ranges, size_t(count));
}

int
v_peg_class_parser_get_ranges_count(const parser_t *ptr)
{
    auto &r = static_cast<const class_parser_data_t &>(**ptr);

    return int(r.ranges.size());
}

const std::array<char32_t, 2> *
v_peg_class_parser_get_ranges(const parser_t *ptr)
{
    auto &r = static_cast<const class_parser_data_t &>(**ptr);

    return r.ranges.data();
}

void
v_peg_make_dot_parser(parser_t *ret)
{
    static const auto p = mk_dot_parser();

    *ret = p;
}


//-----------------------------------------------------------------
void
v_peg_make_call_action(action_t *ret, const char *fun, const argument_t *args, int count)
{
    *ret = mk_call_action(fun, args, size_t(count));
}

const char *
v_peg_call_action_get_function_name(const action_t *ptr)
{
    auto &r = static_cast<const call_action_data_t &>(**ptr);

    return  v_quark_to_string(r.q_fun);
}

int
v_peg_call_action_get_arguments_count(const action_t *ptr)
{
    auto &r = static_cast<const call_action_data_t &>(**ptr);

    return  int(r.args.size());
}

const argument_t *
v_peg_call_action_get_arguments(const action_t *ptr)
{
    auto &r = static_cast<const call_action_data_t &>(**ptr);

    return r.args.data();
}

void
v_peg_make_return_action(action_t *ret, const argument_t *arg)
{
    *ret = mk_return_action(*arg);
}

const argument_t *
v_peg_return_action_get_argument(const action_t *ptr, argument_t *arg)
{
    auto &r = static_cast<const return_action_data_t &>(**ptr);

    return &r.arg;
}


//-----------------------------------------------------------------
void
v_peg_make_identifier_argument(argument_t *ret, const char *ident)
{
    *ret = mk_identifier_argument(ident);
}

const char *
v_peg_identifier_argument_get_identifier(const argument_t *ptr)
{
    auto &r = static_cast<const identifier_argument_data_t &>(**ptr);

    return  v_quark_to_string(r.q_ident);
}

void
v_peg_make_backref_argument(argument_t *ret, int number, int kind)
{
    *ret = mk_backref_argument(size_t(number), backref_argument_data_t::b_kind_t(kind));
}

int
v_peg_backref_argument_get_number(const argument_t *ptr)
{
    auto &r = static_cast<const backref_argument_data_t &>(**ptr);

    return int(r.number);
}

int
v_peg_backref_argument_get_kind(const argument_t *ptr)
{
    auto &r = static_cast<const backref_argument_data_t &>(**ptr);

    return int(r.b_kind);
}

void
v_peg_make_integer_argument(argument_t *ret, intptr_t number)
{
    *ret = mk_integer_argument(number);
}

intptr_t
v_peg_integer_argument_get_number(const argument_t *ptr)
{
    auto &r = static_cast<const integer_argument_data_t &>(**ptr);

    return r.number;
}

void
v_peg_make_literal_argument(argument_t *ret, const char *utf8)
{
    *ret = mk_literal_argument(utf8);
}

const char *
v_peg_literal_argument_get_literal(const argument_t *ptr)
{
    auto &r = static_cast<const literal_argument_data_t &>(**ptr);

    return r.utf8.c_str();
}

void
v_peg_make_character_argument(argument_t *ret, char32_t ucs4)
{
    *ret = mk_character_argument(ucs4);
}

char32_t
v_peg_character_argument_get_character(const argument_t *ptr)
{
    auto &r = static_cast<const character_argument_data_t &>(**ptr);

    return r.ucs4;
}


//---------------------------------------------------------------------
void
v_peg_parser_parse(std::any *ret, const parser_t *parser)
{
    auto &ctx = context_data_t::current_ctx;

    *ret = (*parser)->parse(ctx);
}

void
v_peg_action_act(std::any *ret, const action_t *action)
{
    auto &ctx = context_data_t::current_ctx;

    *ret = (*action)->act(ctx);
}

void
v_peg_argument_value(std::any *ret, const argument_t *argument)
{
    auto &ctx = context_data_t::current_ctx;

    *ret = (*argument)->value(ctx);
}


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END


//---------------------------------------------------------------------
}   //- extern "C"


