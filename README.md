# vyPal's C Compiler (VCC)

This is my attempt at writing a quite basic compiler in C, for a small subset of
the C language.

![wakatime](https://wakatime.com/badge/user/f464de95-0614-429a-a5db-86630ea3fed3/project/fded8be9-6a20-463f-b99a-793b3f9c9d04.svg)

## Mirrors

| Host | Details |
| ---- | ------- |
| [My personal git server](https://git.vypal.me/FromScratch/Compiler) | Primary server, part of future collection of projects written from scratch |
| [GitHub](https://github.com/vyPal/VCC) | GitHub mirror - primarily for exposure and to accept Issues/PRs |

## Plans

My primary goal is to get this compiler to a state, in which it will be able to
compile itself. Will this happen? Who knows. However that is why am keeping the
use of external libraries and functions from them minimal.

Current TODO list:

- haven't had a chance to think this far into the future

## How to use

To use the compiler, you first need to compile it. I've finally setup a Makefile
so building is now much easier:

```bash
make
```

And that's about it, you can now run the command and provide a input file
and optionally the name for the output file that the compiler will generate:

```bash
./build/vcc examples/test.c -o out.s
```

Or it is also possible to combine the building and running into one command:

```bash
make run examples/test.c -o out.s
```

Once you have the generated assembly file, you must assemble and link it.
The assembly is intended for use with `nasm` and `ld`. You can link it like this:

```bash
nasm -f elf64 out.s -o test.o && ld test.o -o test
```

After this, you will finally have an executable file for x86_64 Linux!

```bash
./test
```

## What works

- [x] Function definitions
- [x] Defining function arguments
- [x] Variable definition
- [x] Assignment to variables
- [x] Basic arithmetic (`+ - * / %`)
- [x] Return void
- [x] Return value
- [x] Basic dead code elimination
