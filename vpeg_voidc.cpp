#include "vpeg_voidc.h"
#include "vpeg_context.h"

#include "voidc_ast.h"


//---------------------------------------------------------------------
namespace vpeg
{

//---------------------------------------------------------------------
extern "C"
{

static void
mk_unit(std::any *ret, const std::any *args, size_t)
{
    auto p = std::any_cast<ast_stmt_list_ptr_t>(args+0);

    if (!p)
    {
        static const ast_stmt_list_ptr_t nil;

        p = &nil;
    }

    auto pos = std::any_cast<size_t>(args[1]);

    size_t line;
    size_t column;

    context_t::current_ctx->get_line_column(pos, line, column);

    ast_unit_ptr_t ptr = std::make_shared<const ast_unit_t>(*p, line+1, column+1);

    *ret = ptr;
}

static void
mk_stmt_list(std::any *ret, const std::any *args, size_t)
{
    auto plst = std::any_cast<ast_stmt_list_ptr_t>(args+0);

    if (!plst)
    {
        static const ast_stmt_list_ptr_t nil;

        plst = &nil;
    }

    auto item = std::any_cast<ast_stmt_ptr_t>(args[1]);

    *ret = std::make_shared<const ast_stmt_list_t>(*plst, item);          //- Sic!
}

static void
mk_stmt(std::any *ret, const std::any *args, size_t)
{
    std::string s = "";

    if (auto p = std::any_cast<const std::string>(args+0)) { s = *p; }

    auto c = std::any_cast<ast_call_ptr_t>(args[1]);

    ast_stmt_ptr_t ptr = std::make_shared<const ast_stmt_t>(s, c);

    *ret = ptr;
}

static void
mk_call(std::any *ret, const std::any *args, size_t)
{
    auto f = std::any_cast<const std::string>(args[0]);

    auto a = std::any_cast<ast_arg_list_ptr_t>(args+1);

    if (!a)
    {
        static const ast_arg_list_ptr_t nil;

        a = &nil;
    }

    ast_call_ptr_t ptr = std::make_shared<const ast_call_t>(f, *a);

    *ret = ptr;
}

static void
mk_arg_list(std::any *ret, const std::any *args, size_t)
{
    auto plst = std::any_cast<ast_arg_list_ptr_t>(args+0);

    if (!plst)
    {
        static const ast_arg_list_ptr_t nil;

        plst = &nil;
    }

    auto item = std::any_cast<ast_argument_ptr_t>(args[1]);

    *ret = std::make_shared<const ast_arg_list_t>(*plst, item);       //- Sic!
}

static void
mk_arg_identifier(std::any *ret, const std::any *args, size_t)
{
    auto n = std::any_cast<const std::string>(args[0]);

    ast_argument_ptr_t ptr = std::make_shared<const ast_arg_identifier_t>(n);

    *ret = ptr;
}

static void
mk_arg_integer(std::any *ret, const std::any *args, size_t)
{
    auto n = std::any_cast<intptr_t>(args[0]);

    ast_argument_ptr_t ptr = std::make_shared<const ast_arg_integer_t>(n);

    *ret = ptr;
}

static void
mk_arg_string(std::any *ret, const std::any *args, size_t)
{
    auto s = std::any_cast<const std::string>(args[0]);

    ast_argument_ptr_t ptr = std::make_shared<const ast_arg_string_t>(s);

    *ret = ptr;
}

static void
mk_arg_char(std::any *ret, const std::any *args, size_t)
{
    auto p = std::any_cast<const std::string>(args[0]);

    auto get_character = [](const char * &str)
    {
        uint8_t c0 = *((const uint8_t * &)str)++;

        int n;

        char32_t r;

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
            c0 = *((const uint8_t * &)str)++;

            r = (r << 6) | (c0 & 0x3F);
        }

        return r;
    };

    auto src = p.c_str();

    auto c = get_character(src);

    if (c == U'\\')
    {
        c = get_character(src);

        switch(c)
        {
        case U'n':  c = U'\n'; break;
        case U'r':  c = U'\r'; break;
        case U't':  c = U'\t'; break;
        }
    }

    ast_argument_ptr_t ptr = std::make_shared<const ast_arg_char_t>(c);

    *ret = ptr;
}

static void
mk_neg_integer(std::any *ret, const std::any *args, size_t)
{
    auto n = std::any_cast<intptr_t>(args[0]);

    *ret = -n;
}

static void
mk_dec_integer(std::any *ret, const std::any *args, size_t)
{
    auto n = std::any_cast<intptr_t>(args[0]);

    intptr_t d = std::any_cast<char32_t>(args[1]) - U'0';

//  printf("dec_int: %d %d\n", (int)n, (int)d);

    if (n)  d += 10*n;

    *ret = d;
}

static void
mk_EOF(std::any *ret, const std::any *args, size_t)
{
    //- Just a placeholder...

    *ret = true;
}


//---------------------------------------------------------------------
}   //- extern "C"

//---------------------------------------------------------------------
}   //- namespace vpeg


//---------------------------------------------------------------------
vpeg::grammar_t make_voidc_grammar(void)
{
    using namespace vpeg;

    grammar_t gr;

#define DEF(name) gr = gr.set_action(#name, name);

    DEF(mk_unit)
    DEF(mk_stmt_list)
    DEF(mk_stmt)
    DEF(mk_call)
    DEF(mk_arg_list)
    DEF(mk_arg_identifier)
    DEF(mk_arg_integer)
    DEF(mk_arg_string)
    DEF(mk_arg_char)
    DEF(mk_neg_integer)
    DEF(mk_dec_integer)
    DEF(mk_EOF)

#undef DEF

    //-------------------------------------------------------------
#define DEF(name) auto ip_##name = mk_identifier_parser(#name);

    DEF(stmt_list)
    DEF(stmt_list_lr)
    DEF(stmt)
    DEF(call)
    DEF(arg_list)
    DEF(arg_list_lr)
    DEF(arg)

    DEF(identifier)
    DEF(ident_start)
    DEF(ident_cont)
    DEF(integer)
    DEF(dec_integer)
    DEF(dec_digit)
    DEF(string)
    DEF(str_char)
    DEF(char)
    DEF(character)

    DEF(_)
    DEF(comment)
    DEF(space)
    DEF(EOL)

#undef DEF

    //-------------------------------------------------------------
    //- unit <- _ <'{'> _ l:stmt_list _ '}'     { mk_unit(l, $1s) }
    //-       / _ !.                            { mk_EOF() }

    gr = gr.set_parser("unit",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            ip__,
            mk_catch_string_parser(mk_character_parser('{')),
            ip__,
            mk_catch_variable_parser("l", ip_stmt_list),
            ip__,
            mk_character_parser('}'),

            mk_action_parser(
                mk_call_action("mk_unit",
                {
                    mk_identifier_argument("l"),
                    mk_backref_argument(1, backref_argument_t::bk_start)
                })
            )
        }),
        mk_sequence_parser(
        {
            ip__,
            mk_not_parser(mk_dot_parser()),

            mk_action_parser(
                mk_call_action("mk_EOF", {})
            )
        })
    }));

    //-------------------------------------------------------------
    //- stmt_list <- l:stmt_list_lr     { l }
    //-            /                    { 0 }

    gr = gr.set_parser("stmt_list",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("l", ip_stmt_list_lr),

            mk_action_parser(
                mk_return_action(mk_identifier_argument("l"))
            )
        }),
        mk_action_parser(
            mk_return_action(mk_integer_argument(0))
        )
    }));

    //-------------------------------------------------------------
    //- stmt_list_lr <- l:stmt_list_lr _ s:stmt     { mk_stmt_list(l, s) }
    //-               / s:stmt                      { mk_stmt_list(0, s) }

    gr = gr.set_parser("stmt_list_lr",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("l", ip_stmt_list_lr),
            ip__,
            mk_catch_variable_parser("s", ip_stmt),

            mk_action_parser(
                mk_call_action("mk_stmt_list",
                {
                    mk_identifier_argument("l"),
                    mk_identifier_argument("s")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("s", ip_stmt),

            mk_action_parser(
                mk_call_action("mk_stmt_list",
                {
                    mk_integer_argument(0),
                    mk_identifier_argument("s")
                })
            )
        })
    }), true);      //- Left recursion!

    //-------------------------------------------------------------
    //- stmt <- v:identifier _ '=' _ c:call     { mk_stmt(v, c) }
    //-       / c:call                          { mk_stmt(0, c) }

    gr = gr.set_parser("stmt",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("v", ip_identifier),
            ip__,
            mk_character_parser('='),
            ip__,
            mk_catch_variable_parser("c", ip_call),

            mk_action_parser(
                mk_call_action("mk_stmt",
                {
                    mk_identifier_argument("v"),
                    mk_identifier_argument("c")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("c", ip_call),

            mk_action_parser(
                mk_call_action("mk_stmt",
                {
                    mk_integer_argument(0),
                    mk_identifier_argument("c")
                })
            )
        })
    }));

    //-------------------------------------------------------------
    //- call <- f:identifier _ '(' _ a:arg_list _ ')' _ ';'     { mk_call(f, a) }

    gr = gr.set_parser("call",
    mk_sequence_parser(
    {
        mk_catch_variable_parser("f", ip_identifier),
        ip__,
        mk_character_parser('('),
        ip__,
        mk_catch_variable_parser("a", ip_arg_list),
        ip__,
        mk_character_parser(')'),
        ip__,
        mk_character_parser(';'),

        mk_action_parser(
            mk_call_action("mk_call",
            {
                mk_identifier_argument("f"),
                mk_identifier_argument("a")
            })
        )
    }));

    //-------------------------------------------------------------
    //- arg_list <- l:arg_list_lr   { l }
    //-           /                 { 0 }

    gr = gr.set_parser("arg_list",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("l", ip_arg_list_lr),

            mk_action_parser(
                mk_return_action(mk_identifier_argument("l"))
            )
        }),
        mk_action_parser(
            mk_return_action(mk_integer_argument(0))
        )
    }));

    //-------------------------------------------------------------
    //- arg_list_lr <- l:arg_list_lr _ a:arg    { mk_arg_list(l, a) }
    //-              / a:arg                    { mk_arg_list(0, a) }

    gr = gr.set_parser("arg_list_lr",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("l", ip_arg_list_lr),
            ip__,
            mk_character_parser(','),
            ip__,
            mk_catch_variable_parser("a", ip_arg),

            mk_action_parser(
                mk_call_action("mk_arg_list",
                {
                    mk_identifier_argument("l"),
                    mk_identifier_argument("a")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("a", ip_arg),

            mk_action_parser(
                mk_call_action("mk_arg_list",
                {
                    mk_integer_argument(0),
                    mk_identifier_argument("a")
                })
            )
        })
    }), true);      //- Left recursion!

    //-------------------------------------------------------------
    //- arg <- i:identifier     { mk_arg_identifier(i) }
    //-      / n:integer        { mk_arg_integer(n) }
    //-      / s:string         { mk_arg_string(s) }
    //-      / c:char           { mk_arg_char(c) }

    gr = gr.set_parser("arg",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("i", ip_identifier),

            mk_action_parser(
                mk_call_action("mk_arg_identifier",
                {
                    mk_identifier_argument("i"),
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("n", ip_integer),

            mk_action_parser(
                mk_call_action("mk_arg_integer",
                {
                    mk_identifier_argument("n"),
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("s", ip_string),

            mk_action_parser(
                mk_call_action("mk_arg_string",
                {
                    mk_identifier_argument("s"),
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("c", ip_char),

            mk_action_parser(
                mk_call_action("mk_arg_char",
                {
                    mk_identifier_argument("c"),
                })
            )
        })
    }));


    //=============================================================
    //- identifier <- ident_start ident_cont*       { $0 }

    gr = gr.set_parser("identifier",
    mk_sequence_parser(
    {
        mk_sequence_parser(
        {
            ip_ident_start,
            mk_star_parser(ip_ident_cont)
        }),

        mk_action_parser(
            mk_return_action(mk_backref_argument(0))
        )
    }));

    //-------------------------------------------------------------
    //- ident_start <- [a-zA-Z_]

    gr = gr.set_parser("ident_start",
    mk_class_parser({{'a','z'}, {'A','Z'}, {'_','_'}}));

    //-------------------------------------------------------------
    //- ident_cont <- ident_start / dec_digit

    gr = gr.set_parser("ident_cont",
    mk_choice_parser(
    {
        ip_ident_start,
        ip_dec_digit
    }));


    //-------------------------------------------------------------
    //- integer <- n:dec_integer        { n }
    //-          / '-' n:dec_integer    { mk_neg_integer(n) }
    //-          / '0'                  { 0 }

    gr = gr.set_parser("integer",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("n", ip_dec_integer),

            mk_action_parser(
                mk_return_action(mk_identifier_argument("n"))
            )
        }),
        mk_sequence_parser(
        {
            mk_character_parser('-'),
            mk_catch_variable_parser("n", ip_dec_integer),

            mk_action_parser(
                mk_call_action("mk_neg_integer",
                {
                    mk_identifier_argument("n")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_character_parser('0'),

            mk_action_parser(
                mk_return_action(mk_integer_argument(0))
            )
        })
    }));

    //-------------------------------------------------------------
    //- dec_integer <- n:dec_integer d:dec_digit        { mk_dec_integer(n, d) }
    //-              / d:[1-9]                          { mk_dec_integer(0, d) }

    gr = gr.set_parser("dec_integer",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("n", ip_dec_integer),
            mk_catch_variable_parser("d", ip_dec_digit),

            mk_action_parser(
                mk_call_action("mk_dec_integer",
                {
                    mk_identifier_argument("n"),
                    mk_identifier_argument("d")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("d",
                mk_class_parser({{'1','9'}})
            ),

            mk_action_parser(
                mk_call_action("mk_dec_integer",
                {
                    mk_integer_argument(0),
                    mk_identifier_argument("d")
                })
            )
        })
    }), true);      //- Left recursion!

    //-------------------------------------------------------------
    //- dec_digit <- d:[0-9]        { d }

    gr = gr.set_parser("dec_digit",
    mk_sequence_parser(
    {
        mk_catch_variable_parser("d",
            mk_class_parser({{'0','9'}})
        ),

        mk_action_parser(
            mk_return_action(mk_identifier_argument("d"))
        )
    }));


    //-------------------------------------------------------------
    //- string <- '\"' <str_char*> '\"'     { $1 }

    gr = gr.set_parser("string",
    mk_sequence_parser(
    {
        mk_character_parser('\"'),
        mk_catch_string_parser(
            mk_star_parser(ip_str_char)
        ),
        mk_character_parser('\"'),

        mk_action_parser(
            mk_return_action(mk_backref_argument(1))
        )
    }));

    //-------------------------------------------------------------
    //- str_char <- ![\n\r\t'"] character

    gr = gr.set_parser("str_char",
    mk_sequence_parser(
    {
        mk_not_parser(
            mk_class_parser({{'\n','\n'},{'\r','\r'},{'\t','\t'},{'\'','\''},{'\"','\"'}})
        ),
        ip_character
    }));

    //-------------------------------------------------------------
    //- char <- '\'' <str_char> '\''      { $1 }

    gr = gr.set_parser("char",
    mk_sequence_parser(
    {
        mk_character_parser('\''),
        mk_catch_string_parser(ip_str_char),
        mk_character_parser('\''),

        mk_action_parser(
            mk_return_action(mk_backref_argument(1))
        )
    }));

    //-------------------------------------------------------------
    //- character <- '\\' [nrt'"\\]
    //-            / !'\\' .

    gr = gr.set_parser("character",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_character_parser('\\'),
            mk_class_parser({{'n','n'},{'r','r'},{'t','t'},{'\'','\''},{'\"','\"'},{'\\','\\'}})
        }),
        mk_sequence_parser(
        {
            mk_not_parser(mk_character_parser('\\')),
            mk_dot_parser()
        }),
    }));

    //-------------------------------------------------------------
    //- _ <- space / comment

    gr = gr.set_parser("_",
    mk_star_parser(
        mk_choice_parser({ip_space, ip_comment})
    ));

    //-------------------------------------------------------------
    //- comment <- "//" (!EOL .)* EOL

    gr = gr.set_parser("comment",
    mk_sequence_parser(
    {
        mk_literal_parser("//"),
        mk_star_parser(
            mk_sequence_parser(
            {
                mk_not_parser(ip_EOL),
                mk_dot_parser()
            })
        ),
        ip_EOL
    }));

    //-------------------------------------------------------------
    //- space <- ' ' / '\t' / EOL

    gr = gr.set_parser("space",
    mk_choice_parser(
    {
        mk_character_parser(' '),
        mk_character_parser('\t'),
        ip_EOL
    }));

    //-------------------------------------------------------------
    //- EOL <- "\r\n" / '\n' / '\r'

    gr = gr.set_parser("EOL",
    mk_choice_parser(
    {
        mk_literal_parser("\r\n"),
        mk_character_parser('\n'),
        mk_character_parser('\r')
    }));


    //=============================================================
    return gr;
}



