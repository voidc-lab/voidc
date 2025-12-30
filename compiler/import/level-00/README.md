## Level 0.0 - The "Starter".

Subsystems in some logical order...


### Parsing.

- Parsers - building blocks for Grammars: Parsers, Actions and Arguments.

  - [vpeg_parser.void](vpeg_parser.void) - Void declarations.

- Grammar: container for parsers, actions and values.

  - [vpeg_grammar.void](vpeg_grammar.void) - Void declarations.

- Context, i.e. parsing state: grammar, input stream, buffer, memo, etc.

  - [vpeg_context.void](vpeg_context.void) - Void declarations/implementations.


### "Compilation" visitors...

- AST hierarchy...

  - [voidc_ast.void](voidc_ast.void) - Void declarations.

- Visitor: container for "methods" etc.

  - [voidc_visitor.void](voidc_visitor.void) - Void declarations.


### Compilation "environment"...

- Type system: types and Type Context.

  - [voidc_types.void](voidc_types.void) - Void declarations.

- Compilation contexts.

  - [voidc_target.void](voidc_target.void) - Void declarations.


### Some utility...

- Quarks. Kinda "atoms" in spirit of GTK's Glib...

  - [voidc_quark.void](voidc_quark.void) - Void declarations.

- Utility...

  - [voidc_util.void](voidc_util.void) - Void declarations/implementation.


### Whole Level 0.0 import

- [\_\_import\_\_.void](__import__.void) - ...


---

See also: [../../stage0](../../stage0) ...


