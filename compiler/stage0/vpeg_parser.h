//---------------------------------------------------------------------
//- Copyright (C) 2020-2026 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VPEG_PARSER_H
#define VPEG_PARSER_H

#include "voidc_quark.h"

#include <string>
#include <memory>
#include <any>

#include <immer/array.hpp>


//---------------------------------------------------------------------
namespace vpeg
{

//---------------------------------------------------------------------
class context_data_t;

typedef std::shared_ptr<context_data_t> context_t;


//---------------------------------------------------------------------
//- "Data" base class...
//---------------------------------------------------------------------
template<typename K>
struct data_t : public K
{
    virtual ~data_t() = default;

    virtual typename K::kind_t kind(void) const = 0;
};

//---------------------------------------------------------------------
template<typename T, typename T::kind_t tag>
struct data_tag_t : public T
{
    enum { kind_tag = tag };

    typename T::kind_t kind(void) const override
    {
        return tag;
    }
};


//---------------------------------------------------------------------
//- Parser base class
//---------------------------------------------------------------------
struct parser_kind_t
{
    enum kind_t
    {
        k_choice,
        k_sequence,

        k_and,
        k_not,
        k_question,
        k_star,
        k_plus,
        k_catch_variable,
        k_catch_string,

        k_identifier,
        k_backref,
        k_action,
        k_literal,
        k_character,
        k_class,
        k_dot,
    };
};

//---------------------------------------------------------------------
struct parser_data_t : public data_t<parser_kind_t>
{
    virtual std::any parse(context_t &ctx) const = 0;

public:
    virtual size_t parsers_count(void) const { return 0; }

    virtual const std::shared_ptr<const parser_data_t> *get_parsers(void) const { return nullptr; }
};

typedef std::shared_ptr<const parser_data_t> parser_t;

template<parser_data_t::kind_t tag>
using parser_tag_t = data_tag_t<parser_data_t, tag>;


//---------------------------------------------------------------------
//- Action base class
//---------------------------------------------------------------------
struct action_kind_t
{
    enum kind_t
    {
        k_call,
        k_return,
    };
};

//---------------------------------------------------------------------
struct action_data_t : public data_t<action_kind_t>
{
    virtual std::any act(context_t &ctx) const = 0;
};

typedef std::shared_ptr<const action_data_t> action_t;

template<action_data_t::kind_t tag>
using action_tag_t = data_tag_t<action_data_t, tag>;


//---------------------------------------------------------------------
//- Argument base class
//---------------------------------------------------------------------
struct argument_kind_t
{
    enum kind_t
    {
        k_identifier,
        k_backref,
        k_integer,
        k_literal,
        k_character,
    };
};

//---------------------------------------------------------------------
struct argument_data_t : public data_t<argument_kind_t>
{
    virtual std::any value(context_t &ctx) const = 0;
};

typedef std::shared_ptr<const argument_data_t> argument_t;

template<argument_data_t::kind_t tag>
using argument_tag_t = data_tag_t<argument_data_t, tag>;


//---------------------------------------------------------------------
//- Parsers...
//---------------------------------------------------------------------
template<parser_data_t::kind_t tag>
class parser_array_t : public parser_tag_t<tag>
{
protected:
    explicit parser_array_t(const std::initializer_list<parser_t> &list)
      : array(list)
    {}

    parser_array_t(const parser_t *list, size_t count)
      : array(list, list+count)
    {}

    parser_array_t(const parser_array_t<tag> &head, const parser_t &tail)
      : array(head.array.push_back(tail))
    {}

public:
    const immer::array<parser_t> array;

public:
    size_t parsers_count(void) const override
    {
        return array.size();
    }

    const parser_t *get_parsers(void) const override
    {
        return array.data();
    }
};


//---------------------------------------------------------------------
class choice_parser_data_t : public parser_array_t<parser_data_t::k_choice>
{
public:
    explicit choice_parser_data_t(const std::initializer_list<parser_t> &list)
      : parser_array_t<k_choice>(list)
    {}

    choice_parser_data_t(const parser_t *list, size_t count)
      : parser_array_t<k_choice>(list, count)
    {}

    choice_parser_data_t(const choice_parser_data_t &head, const parser_t &tail)
      : parser_array_t<k_choice>(head, tail)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<const choice_parser_data_t>
mk_choice_parser(const std::initializer_list<parser_t> &list)
{
    return std::make_shared<const choice_parser_data_t>(list);
}

inline
std::shared_ptr<const choice_parser_data_t>
mk_choice_parser(const parser_t *list, size_t count)
{
    return std::make_shared<const choice_parser_data_t>(list, count);
}

inline
std::shared_ptr<const choice_parser_data_t>
mk_choice_parser(const choice_parser_data_t &head, const parser_t &tail)
{
    return std::make_shared<const choice_parser_data_t>(head, tail);
}


//---------------------------------------------------------------------
class sequence_parser_data_t : public parser_array_t<parser_data_t::k_sequence>
{
public:
    explicit sequence_parser_data_t(const std::initializer_list<parser_t> &list)
      : parser_array_t<k_sequence>(list)
    {}

    sequence_parser_data_t(const parser_t *list, size_t count)
      : parser_array_t<k_sequence>(list, count)
    {}

    sequence_parser_data_t(const sequence_parser_data_t &head, const parser_t &tail)
      : parser_array_t<k_sequence>(head, tail)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<const sequence_parser_data_t>
mk_sequence_parser(const std::initializer_list<parser_t> &list)
{
    return std::make_shared<const sequence_parser_data_t>(list);
}

inline
std::shared_ptr<const sequence_parser_data_t>
mk_sequence_parser(const parser_t *list, size_t count)
{
    return std::make_shared<const sequence_parser_data_t>(list, count);
}

inline
std::shared_ptr<const sequence_parser_data_t>
mk_sequence_parser(const sequence_parser_data_t &head, const parser_t &tail)
{
    return std::make_shared<const sequence_parser_data_t>(head, tail);
}


//---------------------------------------------------------------------
template<parser_data_t::kind_t tag>
class parser_unary_t : public parser_tag_t<tag>
{
protected:
    explicit parser_unary_t(const parser_t &_parser)
      : parser(_parser)
    {}

public:
    const parser_t parser;

public:
    size_t parsers_count(void) const override { return 1; }

    const parser_t *get_parsers(void) const override { return &parser; }
};


//---------------------------------------------------------------------
class and_parser_data_t : public parser_unary_t<parser_data_t::k_and>
{
public:
    explicit and_parser_data_t(const parser_t &_parser)
      : parser_unary_t<k_and>(_parser)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<const and_parser_data_t>
mk_and_parser(const parser_t &_parser)
{
    return std::make_shared<const and_parser_data_t>(_parser);
}

//---------------------------------------------------------------------
class not_parser_data_t : public parser_unary_t<parser_data_t::k_not>
{
public:
    explicit not_parser_data_t(const parser_t &_parser)
      : parser_unary_t<k_not>(_parser)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<const not_parser_data_t>
mk_not_parser(const parser_t &_parser)
{
    return std::make_shared<const not_parser_data_t>(_parser);
}

//---------------------------------------------------------------------
class question_parser_data_t : public parser_unary_t<parser_data_t::k_question>
{
public:
    explicit question_parser_data_t(const parser_t &_parser)
      : parser_unary_t<k_question>(_parser)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<const question_parser_data_t>
mk_question_parser(const parser_t &_parser)
{
    return std::make_shared<const question_parser_data_t>(_parser);
}

//---------------------------------------------------------------------
class star_parser_data_t : public parser_unary_t<parser_data_t::k_star>
{
public:
    explicit star_parser_data_t(const parser_t &_parser)
      : parser_unary_t<k_star>(_parser)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<const star_parser_data_t>
mk_star_parser(const parser_t &_parser)
{
    return std::make_shared<const star_parser_data_t>(_parser);
}

//---------------------------------------------------------------------
class plus_parser_data_t : public parser_unary_t<parser_data_t::k_plus>
{
public:
    explicit plus_parser_data_t(const parser_t &_parser)
      : parser_unary_t<k_plus>(_parser)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<const plus_parser_data_t>
mk_plus_parser(const parser_t &_parser)
{
    return std::make_shared<const plus_parser_data_t>(_parser);
}

//---------------------------------------------------------------------
class catch_variable_parser_data_t : public parser_unary_t<parser_data_t::k_catch_variable>
{
public:
    catch_variable_parser_data_t(const char *name, const parser_t &parser)
      : parser_unary_t<k_catch_variable>(parser),
        q_name(v_quark_from_string(name))
    {}

public:
    std::any parse(context_t &ctx) const override;

public:
    const v_quark_t q_name;
};

inline
std::shared_ptr<const catch_variable_parser_data_t>
mk_catch_variable_parser(const char *name, const parser_t &parser)
{
    return std::make_shared<const catch_variable_parser_data_t>(name, parser);
}

//---------------------------------------------------------------------
class catch_string_parser_data_t : public parser_unary_t<parser_data_t::k_catch_string>
{
public:
    explicit catch_string_parser_data_t(const parser_t &_parser)
      : parser_unary_t<k_catch_string>(_parser)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<const catch_string_parser_data_t>
mk_catch_string_parser(const parser_t &_parser)
{
    return std::make_shared<const catch_string_parser_data_t>(_parser);
}


//---------------------------------------------------------------------
class identifier_parser_data_t : public parser_tag_t<parser_data_t::k_identifier>
{
public:
    explicit identifier_parser_data_t(const char *ident)
      : q_ident(v_quark_from_string(ident))
    {}

public:
    std::any parse(context_t &ctx) const override;

public:
    const v_quark_t q_ident;
};

inline
std::shared_ptr<const identifier_parser_data_t>
mk_identifier_parser(const char *ident)
{
    return std::make_shared<const identifier_parser_data_t>(ident);
}

//---------------------------------------------------------------------
class backref_parser_data_t : public parser_tag_t<parser_data_t::k_backref>
{
public:
    explicit backref_parser_data_t(size_t _number)
      : number(_number)
    {}

public:
    std::any parse(context_t &ctx) const override;

public:
    const size_t number;
};

inline
std::shared_ptr<const backref_parser_data_t>
mk_backref_parser(size_t _number)
{
    return std::make_shared<const backref_parser_data_t>(_number);
}

//---------------------------------------------------------------------
class action_parser_data_t : public parser_tag_t<parser_data_t::k_action>
{
public:
    explicit action_parser_data_t(const action_t &_action)
      : action(_action)
    {}

public:
    std::any parse(context_t &ctx) const override;

public:
    const action_t action;
};

inline
std::shared_ptr<const action_parser_data_t>
mk_action_parser(const action_t &_action)
{
    return std::make_shared<const action_parser_data_t>(_action);
}

//---------------------------------------------------------------------
class literal_parser_data_t : public parser_tag_t<parser_data_t::k_literal>
{
public:
    explicit literal_parser_data_t(const std::string &_utf8)
      : utf8(_utf8)
    {}

public:
    std::any parse(context_t &ctx) const override;

public:
    const std::string utf8;
};

inline
std::shared_ptr<const literal_parser_data_t>
mk_literal_parser(const std::string &_utf8)
{
    return std::make_shared<const literal_parser_data_t>(_utf8);
}

//---------------------------------------------------------------------
class character_parser_data_t : public parser_tag_t<parser_data_t::k_character>
{
public:
    explicit character_parser_data_t(char32_t _ucs4)
      : ucs4(_ucs4)
    {}

public:
    std::any parse(context_t &ctx) const override;

public:
    const char32_t ucs4;
};

inline
std::shared_ptr<const character_parser_data_t>
mk_character_parser(char32_t _ucs4)
{
    return std::make_shared<const character_parser_data_t>(_ucs4);
}

//---------------------------------------------------------------------
class class_parser_data_t : public parser_tag_t<parser_data_t::k_class>
{
public:
    using range_t = std::array<char32_t, 2>;

public:
    explicit class_parser_data_t(const std::initializer_list<range_t> &list)
      : ranges(list)
    {}

    class_parser_data_t(const char32_t (*list)[2], size_t count);

public:
    std::any parse(context_t &ctx) const override;

public:
    const immer::array<range_t> ranges;
};

inline
std::shared_ptr<const class_parser_data_t>
mk_class_parser(const std::initializer_list<class_parser_data_t::range_t> &list)
{
    return std::make_shared<const class_parser_data_t>(list);
}

inline
std::shared_ptr<const class_parser_data_t>
mk_class_parser(const char32_t (*list)[2], size_t count)
{
    return std::make_shared<const class_parser_data_t>(list, count);
}

//---------------------------------------------------------------------
class dot_parser_data_t : public parser_tag_t<parser_data_t::k_dot>
{
public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<dot_parser_data_t>
mk_dot_parser(void)
{
    return std::make_shared<dot_parser_data_t>();
}


//---------------------------------------------------------------------
//- Actions...
//---------------------------------------------------------------------
class call_action_data_t : public action_tag_t<action_data_t::k_call>
{
public:
    call_action_data_t(const char *fun, const std::initializer_list<argument_t> &list)
      : q_fun(v_quark_from_string(fun)),
        args(list)
    {}

    call_action_data_t(const char *fun, const argument_t *list, size_t count)
      : q_fun(v_quark_from_string(fun)),
        args(list, list+count)
    {}

public:
    std::any act(context_t &ctx) const override;

public:
    const v_quark_t q_fun;

    const immer::array<argument_t> args;
};

inline
std::shared_ptr<const call_action_data_t>
mk_call_action(const char *fun, const std::initializer_list<argument_t> &list)
{
    return std::make_shared<const call_action_data_t>(fun, list);
}

inline
std::shared_ptr<const call_action_data_t>
mk_call_action(const char *fun, const argument_t *list, size_t count)
{
    return std::make_shared<const call_action_data_t>(fun, list, count);
}

//---------------------------------------------------------------------
class return_action_data_t : public action_tag_t<action_data_t::k_return>
{
public:
    explicit return_action_data_t(const argument_t &_arg)
      : arg(_arg)
    {}

public:
    std::any act(context_t &ctx) const override
    {
        return arg->value(ctx);
    }

public:
    const argument_t arg;
};

inline
std::shared_ptr<const return_action_data_t>
mk_return_action(const argument_t &arg)
{
    return std::make_shared<const return_action_data_t>(arg);
}


//---------------------------------------------------------------------
//- Arguments...
//---------------------------------------------------------------------
class identifier_argument_data_t : public argument_tag_t<argument_data_t::k_identifier>
{
public:
    explicit identifier_argument_data_t(const char *ident)
      : q_ident(v_quark_from_string(ident))
    {}

public:
    std::any value(context_t &ctx) const override;

public:
    const v_quark_t q_ident;
};

inline
std::shared_ptr<const identifier_argument_data_t>
mk_identifier_argument(const char *ident)
{
    return std::make_shared<const identifier_argument_data_t>(ident);
}

//---------------------------------------------------------------------
class backref_argument_data_t : public argument_tag_t<argument_data_t::k_backref>
{
public:
    enum b_kind_t
    {
        bk_string,
        bk_start,
        bk_end,
    };

public:
    explicit backref_argument_data_t(size_t _number, b_kind_t _b_kind=bk_string)
      : number(_number),
        b_kind(_b_kind)
    {}

public:
    std::any value(context_t &ctx) const override;

public:
    const size_t   number;
    const b_kind_t b_kind;
};

inline
std::shared_ptr<const backref_argument_data_t>
mk_backref_argument(size_t number, backref_argument_data_t::b_kind_t b_kind=backref_argument_data_t::bk_string)
{
    return std::make_shared<const backref_argument_data_t>(number, b_kind);
}

//---------------------------------------------------------------------
class integer_argument_data_t : public argument_tag_t<argument_data_t::k_integer>
{
public:
    explicit integer_argument_data_t(intptr_t _number)
      : number(_number)
    {}

public:
    std::any value(context_t &ctx) const override
    {
        return number;
    }

public:
    const intptr_t number;
};

inline
std::shared_ptr<const integer_argument_data_t>
mk_integer_argument(intptr_t number)
{
    return std::make_shared<const integer_argument_data_t>(number);
}

//---------------------------------------------------------------------
class literal_argument_data_t : public argument_tag_t<argument_data_t::k_literal>
{
public:
    explicit literal_argument_data_t(const std::string &_utf8)
      : utf8(_utf8)
    {}

public:
    std::any value(context_t &ctx) const override
    {
        return utf8;
    }

public:
    const std::string utf8;
};

inline
std::shared_ptr<const literal_argument_data_t>
mk_literal_argument(const std::string &utf8)
{
    return std::make_shared<const literal_argument_data_t>(utf8);
}

//---------------------------------------------------------------------
class character_argument_data_t : public argument_tag_t<argument_data_t::k_character>
{
public:
    explicit character_argument_data_t(char32_t _ucs4)
      : ucs4(_ucs4)
    {}

public:
    std::any value(context_t &ctx) const override
    {
        return (uint32_t)ucs4;
    }

public:
    const char32_t ucs4;
};

inline
std::shared_ptr<const character_argument_data_t>
mk_character_argument(char32_t ucs4)
{
    return std::make_shared<const character_argument_data_t>(ucs4);
}


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void parsers_static_initialize(void);
void parsers_static_terminate(void);


//---------------------------------------------------------------------
}   //- namespace vpeg


#endif      //- VPEG_PARSER_H
