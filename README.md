# vyPal's C Compiler (VCC)

This is my attempt at writing a quite basic compiler in C, for a small subset of
the C language.

## Plans

My primary goal is to get this compiler to a state, in which it will be able to
compile itself. Will this happen? Who knows. However that is why am keeping the
use of external libraries and functions from them minimal.

Current TODO list:

- haven't had a chance to think this far into the future

## How to use

To use the compiler, you first need to compile it. I haven't setup and build
script for that, so I just do it manually:

```bash
gcc main.c parser.c compiler.c utils.c -o vcc
```

And that's about it, you can now run the command and provide a input file
and optionally the name for the output file that the compiler will generate:

```bash
./vcc examples/test.c -o out.s
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
- [ ] Defining function arguments
- [x] Variable definition
- [x] Assignment to variables
- [x] Basic arithmetic (`+ - * / %`)
- [x] Return void
- [x] Return value
