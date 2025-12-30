
# The Void Programming Language Tutorial

**Disclaimer:** this tutorial assumes the reader has a rather good knowledge of C/C++,
understands basic notions of LLVM-C API, and in general "has an idea of how compilers work"...


### The "Hello, world" example:

```
{   v_import("printf.void"); }      // Import declaration of C's "printf"

{   printf("Hello, world\n"); }     // Just call it...
```

Void's source text is a sequence of so-called "units".
In example above you can see two of them: the first with `v_import`, and the second with `printf`.
Each unit is (usually) enclosed in (*magic*) curly braces and can be thought of as a function body in a "C"-like syntax.

Units are processed by the `voidc` compiler sequentially, one by one.
For each unit, the following operations are performed, in order:

  1. Source text is *parsed* down to the enclosing `}`, resulting in the AST.
  2. The AST is *compiled* to the object file (in memory) which contains the "unit action" function.
  3. This "unit action" is *executed* in compiler's environment by LLVM's JIT.

I.e. *parsing* of the next unit will start right after the previous one has been *executed*...

In the "Hello, world" example above the first unit calls the compiler's intrinsic function `v_import`.
This function receives the name of the source file "printf.void" (from the Voidc's root directory)
and "imports" it into the "current scope". The "printf.void" file "gives out" only the declaration
of C's `printf` function. But that's enough for the second unit which calls this function with sacramental
greeting message as an argument.

**Essential Note:** it is very important to understand the consequences of this
*parse-compile-execute* architecture of the Voidc's "mainloop"...
It's the key to ~enormous~ extensibility of the Void as a language.
Each unit can change compiler's state into "something completely different"(c)...
This feature intendend to be seen as the ~back~door to the compiler.
You just open the curly brace and enter the compiler as your workshop...
This is why these curly braces are *magic* :wink:.


### The extended "Hello, world" example

Let's try to dig a bit deeper and see how it's possible to declare `printf`
"from scratch", without any *imports*...

```
// Unit 1: declaration of the v_pointer_type
{
    typ0 = v_alloca(v_type_ptr, 2);     // Allocate "array" for two types
    typ1 = v_getelementptr(typ0, 1);    // Get pointer to the second element

    v_store(v_type_ptr, typ0);          // Argument 1: element type
    v_store(unsigned,   typ1);          // Argument 2: address space

    ft = v_function_type(v_type_ptr,    // Return type
                         typ0,          // Argument types
                         2,             // Number of arguments
                         false);        // VarArgs? (not)

    v_add_symbol("v_pointer_type",      // Symbol name  (function name)
                 ft,                    // Symbol type  (function type)
                 0);                    // Symbol value (none - just declaration)
}

// Unit 2: declaration of the printf
{
    typ = v_alloca(v_type_ptr);

    char_ptr = v_pointer_type(char, 0);

    v_store(char_ptr, typ);             // Format string

    ft = v_function_type(int, typ, 1, true);
    v_add_symbol("printf", ft, 0);
}

// Unit 3: call the printf
{
    printf("Hello, world\n");
}
```

First of all, it must be mentioned that when `voidc` starts to read the source file,
it begins with "minimal" internal environment. Only very few names are pre-declared.
Everything else, that we wish to use, must be declared "by hands" (or by imports)...

In the case of `printf` we have almost all necessary tools accessible, except one -
the function `v_pointer_type` (to build "pointer to char" type).
So, the first unit makes exactly this thing: it declares the `v_pointer_type` function...

As is often the case in programming, it is best to read it *sdrawkcab*:

- The last statement of the unit calls the function `v_add_symbol` to "declare" the `v_pointer_type` function.
  The term "symbol" here denotes a symbol in the terminology of (JIT's) linker.

- The last but one statement builds the type of the `v_pointer_type` function.

- The previous pair of statements (containing `v_store`) form the list of argument types for the `v_pointer_type` function.

- And "finally", the first couple of statements prepare the memory (in stack) for the list of argument types.

Many important details have been omitted for brevity...

The second unit, in similiar way as the first one, declares the `printf` function.
Note the use of the `v_pointer_type` function.

The third unit just calls the `printf`...


## Language syntax (and semantics).

In fact, Void as a language *has no* fixed/constant/static syntax/semantics...
Instead, it has a minimalist "starter language" and a set of "language development" tools.
Then the language is *developed*, and this development is organized into so-called "levels".

For the moment (Nov 2023) there are four levels:

  - [Level 0.0](../compiler/import/level-00) - "Starter Language" and compiler API.
  - [Level 0.1](../compiler/import/level-01) - Control flow, grammars, expressions and declarations/definitions.
  - [Level 0.2](../compiler/import/level-02) - "C on steroids"...
  - [Level 0.3](../compiler/import/level-03) - "C++ for poors"...

All these levels together form the so-called "Mainline Language".

First we'll go over the Starter Language a little. Just to feel "how it started".
Then we'll take a closer look at the Mainline Language. To find out "how it's going"...


## The Starter Language.

### Syntax (and a bit of semantics).

- The `voidc` awaits source files properly encoded in UTF-8.
- Parser works at a Unicode Code Point "granularity".
- Whitespaces are: `' '`, `'\t'`, `'\n'` and `'\r'` (in C's notation).
- Comments are BCPL's `//` till the end of line.
- Whitespaces and comments mostly ignored (just delimiters, like in C).

Abstract syntax can be expressed in EBNF-like notation:

```EBNF
unit = "{" {stmt} "}"

stmt = [[ident "="] expr] ";"

expr = prim {"(" [expr {"," expr}] ")"}

prim = ident | integer | string | char
```

- Identifiers are C's: `[a-zA-Z_][a-zA-Z_0-9]*`, case-sensitive.
- Integers only decimal, optionaly signed, without leading zeros.
- Strings and chars similiar to C's, with some "restrictions":
  - Strings are NOT "auto-concatenable".
  - Escapes limited only to `\t`, `\n`, `\r`, `\'`, `\"` and `\\`.
  - Any other valid non-null Unicode Code Points are *allowed*.

So, as you can see, the Starter Language is rather limited:

- There are no keywords.
- There are no operators.
- There is no arithmetic.
- There is no flow control.
- There is no preprocessor.
- There are no floating-point, hexadecimal, octal or binary literals.
- Etc, etc, etc ...

How the hell it is possible to program in it?

Well... To get an idea you can check the ["hello_jit.void"](../hello_jit.void) example.

In Void you *must* "write program to write program"(c)...

All the Starter Language really allows is to call functions. Sequentially.

```
printf("Hello!\n");
printf("Hello again!\n");
printf("Hello one more time!\n");
```

Function calls can be nested and "curried" (like in C).

```
foo(bar("Hi", 42), 'Ы')(1, 2, 3)(-7);
```

What a function returns can be "named" by an identifier.

```
n = strlen("Some string");
```

These identifiers can be used in subsequent statements.

```
C = 'Ъ';

printf("Cyrillic Capital Letter Hard Sign: %d\n", C);
```

Identifiers also can be "shadowed" by subsequent "renamings" (like in Rust).

```
u = "1";
u = atof(u);

v = u;

v = fma(v, u, v);
v = fma(v, u, v);
v = fma(v, u, v);
v = fma(v, u, v);

printf("v: %g\n", v);
```

Please note, in contrast to C, in the Starter Language:

- The identifier to the left of the `=` symbol does NOT denote a *variable*.
- The `=` symbol does NOT denote an assignment.

In fact, the semantics of `=` in Void is almost the same as in LLVM IR (with respect to "shadowing").

Informally, it's possible to think of `=` as a kind of *definition* operator.
It just "sticks" a label with the identifier on the left to the value received on the right...

Also this is very similar to `<-` from the monadic "do" notation (in Haskell).


### Not all functions are the same...

Let's look at [The extended "Hello, world" example](#the-extended-hello-world-example).
In it's code there are several "function calls".
Some of them actually call real functions (in the "C" language sense).
But others do "something different"...

Identifiers `v_alloca`, `v_getelementptr` and `v_store` denote so called **"compile-time intrinsic functions"**.
Such functions actually called during *compilation* phase.
They take arguments in the form of AST and their purpose is (usually) to generate code...

As for the mentioned *"ct-intrinsics"*, their semantics correspond to the LLVM IR instructions of similar name.

There are number of "predefined" compile-time intrinsics, and it's entirely possible to create your own.
The main advantage of such functions is that they can be *polymorphic* in the broadest sense.
They can interpret their arguments "by their own rules".
They can generate code "in their own way" and/or change the state of the compiler "at their discretion"...

*Ct-intrinsics* are the most widely used "language development" tools mentioned above.


### Void's Reserved Names

Identifiers starting with `v_` and `voidc_` are reserved for the needs
of the `voidc` compiler itself and the (future) standard library...

In addition, careless use of external identifiers starting with `_`, `LLVM`, etc.
can lead to collisions with the C/C++ and LLVM libraries.


### The Void's Types.

Starter Language's typing can be described as *static* and *implicit*.
This means that the identifier to the left of the `=` takes the type
*implicitly* determined by the expression on the right.
This type does not change during identifier's "lifetime" (with respect to "shadowing").

The Void has it's own type system, which is "mapped" to LLVM's one and can be seen as an *extension* of it...

The Starter Language *has no* specific syntax for types.
Therefore, we'll describe Void's type system "through the lens" of the compiler's types API.
This API is designed to be similar (in spirit) to the LLVM-C API associated with LLVM's types.
The implementation of Void's types is also very similar to LLVM's.

As you can probably guessed, the name of the type of the Void's types representation is `v_type_ptr`.
Like `LLVMTypeRef`, `v_type_ptr` looks like a pointer to some opaque structure.


#### Integer types.

Void's integer types are all LLVM's ones with additional "attribute" of *signedness*.

Yes! You can use integers of any bitwidth accepted by LLVM (i.e. 1 - 2<sup>23</sup>).
And they can also be signed or unsigned.

API in "C" syntax:

```C
v_type_ptr v_int_type(unsigned width);          // Signed
v_type_ptr v_uint_type(unsigned width);         // Unsigned
```

There are number of predefined integral types which correspond to tagret's "C" integral types.
Some of them: `char`, `short`, `int`, `unsigned`, `long`, `intptr_t` etc...

Predefined type `bool` is the `v_uint_type(1)`. Named constants `false` and `true` have obvious type and values...


#### Floating point types.

```C
v_type_ptr v_f16_type();            // LLVM's half
v_type_ptr v_f32_type();            // LLVM's float     - C's
v_type_ptr v_f64_type();            // LLVM's double    - C's
v_type_ptr v_f128_type();           // LLVM's fp128
```


#### Pointer types.

```C
v_type_ptr v_pointer_type(v_type_ptr elem, unsigned adsp);
```

These pointer types are similar to C's with respect to additional parameter of LLVM's "address space"...


#### Reference types.

```C
v_type_ptr v_reference_type(v_type_ptr elem, unsigned adsp);
```

These references are semantically alike to C++'s `&` ones (with respect to LLVM's "address space").

Reference can be seen as a pointer in a "superhero costume"...
The "superpower" of reference is ability to *unreference* pointed value "on demand".


#### Array types.

```C
v_type_ptr v_array_type(v_type_ptr elem, uint64_t length);
```

Ordinary LLVM's (and C's) arrays...


#### Vector types.

```C
v_type_ptr v_vector_type(v_type_ptr elem, unsigned size);           // ...
v_type_ptr v_svector_type(v_type_ptr elem, unsigned size);          // Scalable
```

LLVM's vectors...


#### Structure types.

```C
v_type_ptr v_struct_type_named(const char *name);                           // Named (opaque)

v_type_ptr v_struct_type(v_type_ptr *elts, unsigned count, bool packed);    // Unnamed

void v_type_struct_set_body(v_type_ptr typ, v_type_ptr *elts, unsigned count, bool packed);
```

Similar to LLVM's struct types. Fields are unnamed, indexed by numbers...


#### Function types.

```C
v_type_ptr v_function_type(v_type_ptr ret, v_type_ptr *par, unsigned count, bool vararg);
```

Similar to LLVM's (and C's) function types...


#### Void type.

```C
v_type_ptr v_void_type();               // As is...
```

This type also predefined as `void`...


### Some notes on data/types representation ...

- `'ю'` - character literals are of type `char32_t` which is the `v_uint_type(32)`.

- String literal represented as a (constant) *value* of type 'array of `char`', encoded in UTF-8, null-terminated.

```
str = "Hello world!\n";         // ... v_array_type(char, 14)
```

- Such strings can be *promoted* to pointers "on demand" (like in C):

```
str = "world";

printf("Hello %s!\n", str);
```

- There's some kind of (C-like) *promotion* of arithmetic types...

```
printf("sqrt(2) = %g\n", sqrt(2));          // C's "double sqrt(double)"
```

## The Mainline Language.

The "Mainline Language" is obtained by applying a sequence of extensions,
grouped in the form of so-called "levels". Each subsequent extension builds on the previous ones.

You can imagine this development as a passage of a computer game.
The main character is, in fact, our “Language”.
At the beginning, he has the “rank” of “Starter” and can do almost nothing.
But during the game, our hero goes through a whole series of “adventures”
and acquires new skills and abilities.
And at some point it reaches the “rank” of “Mainline”...

In addition to “levels” there will also be “versions” and they should not be confused.
Versions refer to the "game" as a whole.
The “game” itself will “evolve” from version to version.
The levels will change (slightly) and new levels will be added...

There are no versions in our project yet, but at some point they will inevitably appear...


### How to "turn on" Mainline Language.

The simplest (but not the only) way is to place these two units at the beginning of the source file:

```
{ v_import("mainline.void"); }      // Import the Mainline Language
{ v_enable_mainline(); }            // "Enable" it
```

The first unit imports all the necessary tools for the Mainline Language to work,
but the current language **does not change**.
The import process will compile quite a lot of code for the first time,
which can take quite a long time.
But subsequent imports will be significantly faster thanks to "pre-*compilation*" (Python style)...

The second unit calls the `v_enable_mainline` function, imported from `mainline.void`.
This function "enables" all the tools of the Mainline Language:

- Changes the grammar (and parser).
- Adds new “node types” to the AST hierarchy.
- "Teaches" the compiler to work with these new types of AST nodes.

As a result, immediately after the second unit (right after its `}`)
the “new compiler” of Mainline Language starts working, which allows you to use all the new features...


### The *most* important innovations:

- A new kind of units containing a set of definitions/declarations.
- A new kind of “statements” - definitions and declarations.

Example (assuming Mainline Language is already enabled):


```
// ...
// Unit of definitions/declarations:

double = float(64);             // Kinda, "typedef"

ceil: (double) ~> double;       // Declarations...
sqrt: (double) ~> double;

printf: (*const char, ...) ~> int;          // Good old printf...

S = 1000;                       // Size of sieve - constant, int type

sieve: &bool[S] := 0;           // Mutable array of bool, zero initialized

// "Usual" unit:
{
    sieve[2] := true;           // Assignment ...

    for (i: &int := 3; i < S; i += 2)   sieve[i] := true;

    R = (ceil(sqrt(S)) : int);      // ... cast ...

    for (i: &int := 3; i < R; i += 2)
    {
        if (!sieve[i])  continue;

        for (k: &int := i*i, i2 = 2*i; k < S; k += i2)
        {
            sieve[k] := false;
        }
    }

    for (i: &int := 0; i < S; ++i)
    {
        if (sieve[i])  printf("%d\n", i);
    }
}
```

In this example you can see two units:

- The first, in a new format, contains definitions and declarations.
- The second, in familiar braces, contains many new constructions...

New kind units usually occupy all the space between two "traditional" units
and must contain at least one definition or declaration.
Let's see what we have there:

- Definition of the type `double`, in a familiar style. And yes, we now have type expression syntax!
- Declarations of some C functions. In a "mathematical" style.
The `~>` operator "hints" that these are *imperative* functions.
Parentheses around the parameter list are mandatory.
- Definition of an integer constant. The default type is `int`.
If you need something else, please indicate explicitly, for example: `Sz: size_t = 65536;`
- Definition of a **mutable** variable. The type must be *reference*. The `:=` operator means initialization.
If you omit the initialization and leave only the type, you get a *declaration*...

All the above definitions/declarations are *global* and are visible in all subsequent units.
The order is important! Every identifier must be defined or declared before it can be used.

Now let's see what's interesting in the second unit:

- Expressions in C style with some features:

    - The assignment operator symbol looks like `:=` (same as initialization).
    - The cast operator looks like `( <value> : <type> )`. Parentheses are mandatory.
    - For other details of the expressions, see below...

- C-style flow control constructs:

    - `for` loop with some C++ style tricks. Quite familiar `if`...
    - `continue` ...
    - More details on flow control below...


### Definitions and declarations.

These "statements" are of two kinds:

#### *Definitions*.

Usually contains `=` or `:=` operators.

##### Defining *constants*.

Syntax - `<name> : <type> = <value> ;` or the familiar `<name> = <expression> ;`:

- `<name>` can be an identifier or a so-called *special identifier*.
- `<type>` and `<value>` are usually *expressions*.
- `<expression>` can denote a *type*.

Example:

```
U = 'U';                                    // U : char32_t

s = "Hello World";                          // s : char[12]

f = s[6] == U+2;                            // Same as  f = true;

ss: (*const char)[] = { "foo", s+6 };       // ss : (*const char)[2]

tt = ss[f];                                 // tt : *const char

{ printf("Hello %s!\n", tt); }              // ...
```

##### Defining *variables*.

Syntax - `<name> : <type> := <value> ;`:

- `<name>` is the usual identifier of a variable located in memory.
- `<type>` is an expression that must denote a *reference* type.
- `<value>` is an expression that specifies the initial value. Special (universal) cases:

    - `undef` - the initial value is undefined.
    - `0` - zero initialization.

Example:

```
i: &int := 1;                               // Just int

j: &int[5] := { i+0, i+1, i+2, i+3, i+4 };  // Array of ints

k: &(*int)[3] := { j+0, j+1, j+2 };         // Array of pointers

f = j[2] == 3;                              // Almost the same as  f = true;

g = k[2] == &j[2];                          // Almost the same as  g = true;
```

##### Defining *functions*.

Syntax - `<name> : <type> { <body> }`:

- `<name>` can be an identifier or a so-called *special identifier*.
- `<type>` is an "expression" that must denote a *function* *proto*-type.
- `<body>` - list of statements...

Example:

```
multiply: (a: int, b: int) ~> int           // By value
{
    return  a * b;                          // ...
}
```

##### Defining struct/union *types* with named fields.

Syntax (in EBNF-like notation):

```EBNF
... = ("struct"|"union") [identifier] ['{' {[identifier ':'] expression ';'} '}'] ';'
```

Structures and unions are in many ways similar to C ones. See details below.

Example:

```
struct FILE;            // Opaque structure

struct Coords
{
    x: double;
    y: double;
};

byte = uint(8);

union IntRep
{
    i: int;
    r: byte[int.sizeof];
};

{   ir: &IntRep := undef;

    ir.i := 0xDEADBEEF;

    for (i: &int := 0; i < int.sizeof; ++i)
    {
        printf("%02X ", ir.r[i]);
    }

    printf("\n");
}
```

#### *Declarations*.

General syntax - `<name> : <type> ;`:

- `<name>` can be an identifier or a so-called *special identifier*.
- `<type>` is a type expression.


Depending on what `<type>` is, different cases are possible...

##### *Function* declarations.

`<type>` must be a function type.

Example:

```
qsort: (ptr: *void, cn: size_t, sz: size_t, cmp: *((*const void, *const void) ~> int)) ~> void;

compare: (a: *const void, b: *const void) ~> int
{
    a = *(a: *const int);
    b = *(b: *const int);

    return  a - b;
}

{   ints: &int[] := { 0, 4, 2, 3, 1 };

    qsort(ints, 5, int.sizeof, compare);

    for (i: &int := 0; i < 5; ++i)  printf("%d ", ints[i]);

    printf("\n");
}
```

##### *Variable* declarations.

`<type>` must be a reference type.

Example:

```
struct FILE;                    // Definition of opaque structure type

stdin:  &*FILE;                 // Declarations of C's stdio streams
stdout: &*FILE;                 // ...
stderr: &*FILE;                 // ...

fprintf: (*FILE, *const char, ...) ~> int;

(_.printf()) = fprintf;         // Definition of a "special identifier" for fprintf.

{   fprintf(stdout, "Hello!\n");

    stdout.printf("Hello %s...\n", "again");
}
```

##### *Constant* declarations.

For now (Dec 2023) this is a *very* experimental case...

...


### Expressions.














...


######  To be continued...
