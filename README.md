![Black Square](./doc/img/black_square.png)

# The Void Programming Language

*<p align='right'>"Programming language is not a templum, it's a workshop." (c)...</p>*

Void is an open-source, highly extensible programming language, minimalistic and low-level in its base.
It uses LLVM for code generation and can be seen as a thin (well, in some sense) shell around it.
Extensibility of Void is practically limitless (any syntax/semantics),
restricted only by decidability and ~your money~ imagination.


Here is the author's blog dedicated to this project: [Void Blog](https://github.com/Dmitry-Borodkin/void_blog).


#### Hello world:

```C
{   v_import("printf.void"); }      // Import declaration of C's "printf"

{   printf("Hello, world\n"); }     // Just call it...
```

See more details in [tutorial](doc/tutorial.md).
(Spoiler: it's all about those *magic curly braces*... :wink:)


#### Main architecture features:

  - Starting from minimalistic language "kernel", which is:
    - Compatible with "C" by key types and calling conventions;
    - Able to call (almost) arbitrary C-functions (from libc, LLVM, whatever);
    - Has some built-in "module importing" system (with pre-*compilation* support);
    - Has some predefined tools for further "bootstrapping".

  - Continuing with exposing compiler's internal API(s):
    - Extensible parser based on PEG-machinery;
    - Extensible AST;
    - Extensible compiler based on "visitor pattern";
    - Exposing essential parts of LLVM-C API (Core, TargetMachine, etc.).

  - Developing "basic" language features:
    - Ability to create (C-compatible) functions;
    - Basic set of control flow constructions: "if then else", "block", "loop", "switch", "defer";
    - Comfortable "syntactic sugar" for grammar development;
    - C-like expressions (with some additional "twists");
    - Finally, some "normal" syntax for function's and variable's definitions/declarations.

  - Extending the language:
    - Comprehensive numeric literals;
    - Sophisticated control flow constuctions: "while", "for", extended "if then else" and "switch";
    - Conditional compilation;
    - Aggregate "initializations";
    - Simple compiler's handlers;
    - "Simple" overloading of operators;
    - "Projections" (functions with syntax `a.something`);
    - Simple structures with named fields;
    - "Mangling" (fancy function names to help overloading/projections);
    - Function inlining;
    - Unions (like in C).

  - Advanced language features:
    - Objects ("simplified" OOP);
    - Coercions ("single", "projective" and "injective");
    - Namespaces (regular and parameterized);
    - Some support for generic types;
    - Something like "AST literals";
    - Macros (which are also templates);
    - ...

  - ...


For the moment (Sep 2024), language seems to be ready just for some early experiments...

#### Yet another example:

```
{ v_import("mainline.void"); }      // Import "mainline" language
{ v_enable_mainline(); }            // "Enable" it

//---------------------------------------------------------------------
num_t = uint(1024);                 // 1024 bits, unsigned

check_fruits: (a: &num_t, b: &num_t, c: &num_t) ~> bool     // By reference
{
    ab = a + b;     ac = a + c;     bc = b + c;

    return  a*ab*ac + b*ab*bc + c*ac*bc == 4*ab*ac*bc;
}

//---------------------------------------------------------------------
// Constants

A: num_t = 154476802108746166441951315019919837485664325669565431700026634898253202035277999;
B: num_t =  36875131794129999827197811565225474825492979968971970996283137471637224634055579;
C: num_t =   4373612677928697257861252602371390152816537558161613618621437993378423467772036;

//---------------------------------------------------------------------
{   printf: (*const char, ...) ~> int;              // Declaration

    if (check_fruits(A, B, C))  printf("\nOK\n");
    else                        printf("\nFail\n");
}
```

**Note:** Void is *not* intended to be seen as a complete/finished/whatever programming language in usual sense...
In fact, it is merely a toolkit/workshop/boilerplate/etc. for development of programming languages *per se*,
and for development of *itself* particularly.

#### One more example:

```
{ v_import("mainline.void"); }
{ v_enable_mainline(); }

//---------------------------------------------------------------------
double = float(64);                 // Kinda, "typedef"

sqrt: (double) ~> double;
ceil: (double) ~> double;
log:  (double) ~> double;

//---------------------------------------------------------------------
N = 10001;                                      // For N-th prime ...

S = (N*(log(N) + log(log(N))) : int) + 1;       // ... should be enough

sieve = new bool[S];    defer delete[] sieve;

//---------------------------------------------------------------------
{   memset: (s: *void, c: int, n: size_t) ~> *void;

    memset(sieve, 0, S);

    sieve[2] := true;

    for (i: &int := 3; i < S; i += 2)
    {
        sieve[i] := true;
    }

    R = (ceil(sqrt(S)) : int);

    for (i: &int := 3; i < R; i += 2)
    {
        if (sieve[i])
        {
            for (k: &int := i*i, i2 = 2*i; k < S; k += i2)
            {
                sieve[k] := false;
            }
        }
    }
}

//---------------------------------------------------------------------
{   n: &int := 1;
    p: &int := 3;

    for (; p < S; p += 2)
    {
        if (sieve[p])
        {
            ++n;

            if (n == N) break;
        }
    }

    printf: (*const char, ...) ~> int;

    if (n == N) printf("Found: %d\n", p);
    else        printf("Not found\n");
}
```

#### Nearest TODO(s):

  - Documenting everything written so far;

  - Preparing for next developments:

    - Standard library (strings, smart pointers, containers, etc.);
    - *Protocols*:
      - "Variants and matching",
      - ...
    - "Bootstrapping"...

  - ...



## Installation

The `voidc` is "tested" (well, in some sense) under Linux and Windows (MSYS2/Mingw64) environments.

#### Dependencies:

  - LLVM/Clang (v.15 or later);
  - C++ library [immer](https://github.com/arximboldi/immer) (should be "visible" for include...).

#### Build:

Try scripts (CMake needed)...

```bash
$ ./mk_config

$ ./mk_build
```


## Usage

Currently (Sep 2024) `voidc` works mainly from it's source directory.

You can try some of:

```bash
$ build/voidc hello.void

$ build/voidc hello_jit.void
```

You should see the sacramental "Hello, world!" messages with some LLVM's printouts...

BTW, first time it can take a while due to long (idk, 10-30 sec) compilation of huge `llvm-c/Core.void`.

After that, try:

```bash
$ build/voidc compiler/test/level-03/macros_test.void
```

This "script" (with imports) uses almost all language features developed so far.
And yes, first-time compilation of it can take even longer...


## Contributing
Of course, contributions are **welcome**!
But I must warn you that at this point I am a newbie
to things like GitHub, project/community maintaining, etc.

So, for any friendly help (on these topics especially) I'll be greatly appreciated!

## License
[LGPL-3.0](https://www.gnu.org/licenses/lgpl-3.0.txt)

