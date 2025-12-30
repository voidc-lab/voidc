## Level 0.0 - The "Starter".

Subsystems in some logical order...


### Parsing.

- Parsers - building blocks for Grammars: Parsers, Actions and Arguments.

  - [vpeg_parser.h](vpeg_parser.h) - Declarations of parsers, actions and arguments.
  - [vpeg_parser.cpp](vpeg_parser.cpp) - Implementation.

- Grammar: container for parsers, actions and values.

  - [vpeg_grammar.h](vpeg_grammar.h) - Declaration of Grammar.
  - [vpeg_grammar.cpp](vpeg_grammar.cpp) - Implementation.

- Context, i.e. parsing state: grammar, input stream, buffer, memo, etc.

  - [vpeg_context.h](vpeg_context.h) - Declaration of Context.
  - [vpeg_context.cpp](vpeg_context.cpp) - Implementation.

- Initial grammar for the "Starter Language".

  - [vpeg_voidc.h](vpeg_voidc.h) - Declaration...
  - [vpeg_voidc.cpp](vpeg_voidc.cpp) - Implementation.


### "Compilation" visitors...

- AST hierarchy...

  - [voidc_ast.h](voidc_ast.h) - Declaration.
  - [voidc_ast.cpp](voidc_ast.cpp) - Implementation.

- Visitor: container for "methods" etc.

  - [voidc_visitor.h](voidc_visitor.h) - Declaration.
  - [voidc_visitor.cpp](voidc_visitor.cpp) - Implementation.

- Initial compiler for the "Starter Language".

  - [voidc_compiler.h](voidc_compiler.h) - Declaration...
  - [voidc_compiler.cpp](voidc_compiler.cpp) - Implementation.


### Compilation "environment"...

- Type system: types and Type Context.

  - [voidc_types.h](voidc_types.h) - Declaration.
  - [voidc_types.cpp](voidc_types.cpp) - Implementation.

- Compilation contexts.

  - [voidc_target.h](voidc_target.h) - Declaration.
  - [voidc_target.cpp](voidc_target.cpp) - Implementation.


### Some utility...

- Quarks. Kinda "atoms" in spirit of GTK's Glib...

  - [voidc_quark.h](voidc_quark.h) - Declaration of Quarks.
  - [voidc_quark.cpp](voidc_quark.cpp) - Implementation.

- Utility...

  - [voidc_util.h](voidc_util.h) - Declaration.
  - [voidc_util.cpp](voidc_util.cpp) - Implementation.

- Windoze...

  - [voidc_dllexport.h](voidc_dllexport.h) - Declaration.

- Some stdio wrappers...

  - [voidc_stdio.h](voidc_stdio.h) - Declaration.
  - [voidc_stdio.cpp](voidc_stdio.cpp) - Implementation.


### Main...

- Importing and "Main Loop"...

  - [voidc_main.cpp](voidc_main.cpp) - Implementation.


---

See also: [../import/level-00](../import/level-00)


