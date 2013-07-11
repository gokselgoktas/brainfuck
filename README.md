# Brainfuck

A primitively-optimizing [Brainfuck](http://esolangs.org/wiki/brainfuck)
compiler & interpreter

## What on Earth is Brainfuck?

Brainfuck is a rather famous esoteric programming language, invented by
[Urban M&uuml;ller](http://esolangs.org/wiki/Urban_Müller) in 1993. It
operates on an array of cells, also referred to as the *tape*. A pointer is
used to address into the tape and perform read and write operations on the
cells of the tape.

Brainfuck provides eight commands in total. These are:

|Command|Action                                                             |
|:-----:|-------------------------------------------------------------------|
|  `>`  |Move the pointer one cell to the right                             |
|  `<`  |Move the pointer one cell to the left                              |
|  `+`  |Increment the cell under the pointer                               |
|  `-`  |Decrement the cell under the pointer                               |
|  `.`  |Output the cell under the pointer as a character                   |
|  `,`  |Input a character and store it in the cell under the pointer       |
|  `[`  |Jump past the matching `]` if the cell under the pointer is zero   |
|  `]`  |Return to the matching `[` if the cell under the pointer is nonzero|

These eight cleverly thought-out commands are enough to make Brainfuck a
[Turing-complete](http://esolangs.org/wiki/Turing-complete) language. However,
given its adorable syntax and crudity, this Turing-completeness also qualifies
Brainfuck as an unfortunate
[Turing tar-pit](http://esolangs.org/wiki/Turing_tarpit).

### Say, *Hello World*

Saying __*Hello World*__ in Brainfuck is no easy feat. It looks something like
this:

```brainfuck
>+++++++++[<++++++++>-]<.>+++++++[<++++>-]<+.+++++++..+++.>>>++++++++[<++++>-]<
.>>>++++++++++[<+++++++++>-]<---.<<<<.+++.------.--------.>>+.
```

As you can see, although it is perfectly capable of producing that famous and
ever-so-loved message, the syntax of the code to achieve this task might creep
you out.  Fear not, though&hellip; Fear not!

## Brainfuck, the compiler

Now that Brainfuck, the language is clarified, let's focus on this project.
This is a somewhat optimizing compiler and interpreter suite written in
[C](http://en.wikipedia.org/wiki/C_\(programming_language\)).

The compiler works by first reading in some Brainfuck source code from a
specified stream. It then parses this source code and constructs an
intermediate representation of the program. Then, it applies a bunch of simple
and straightforward optimizations to this intermediate representation. The
result is a vector of instructions ready to be interpreted or converted and
emitted in some other format (e.g. as C or
[LLVM](http://en.wikipedia.org/wiki/LLVM) bytecode).

In addition to these the compiler also supports outputting the internal
intermediate representation of the code it is working on in a human-friendly
form.

### Invoking the compiler

The most straightforward way of invoking the compiler would be to simply give
it a source file to chew on:

```bash
brainfuck filename
```

Executing the compiler like this, with a filename alone will result in that
file being treated as the source code of interest. The compiler will then try
to compile the source code and interpret the resulting byte code.

In addition to this simple use case the compiler supports some options. Here's
the help screen for reference:

```
    dP                         oo          .8888b                   dP
    88                                     88   "                   88
    88d888b. 88d888b. .d8888b. dP 88d888b. 88aaa  dP    dP .d8888b. 88  .dP
    88'  `88 88'  `88 88'  `88 88 88'  `88 88     88    88 88'  `"" 88888"
    88.  .88 88       88.  .88 88 88    88 88     88.  .88 88.  ... 88  `8b.
    88Y8888' dP       `88888P8 dP dP    dP dP     `88888P' `88888P' dP   `YP

Authored in 2013.  See README for a list of contributors.
Released into the public domain.

Usage:
        ./bin/brainfuck [--cdehuxz] <input>

Options:
        --                          read input from stdin
        -c [filename=`brainfuck.c`] generate and emit C code
        -d                          print disassembly
        -e                          explain source code
        -h                          display this help screen
        -u                          disable optimizations
        -v                          display version information
        -x                          disable interpretation
        -z <length=`30000`>         set tape length
```

The `[]` brackets indicate an optional block of argument. You can omit these
at will. For example, If you'd like to emit C code into the default file
`brainfuck.c` you can specify the `-c` option while omitting its argument.

On the other hand, `<>` brackets stand for an obligatory argument block and
they cannot be omitted; although if the input is read from `stdin` there is no
need to specify a separate source code file as `<input>`.

## License

The author of this software hates viral software licenses (hi, GPL) and really
annoying stuff like software patents. Therefor, this project is placed in the
public domain.

Any being (not just humans) is free to copy, modify, publish, use, compile,
sell or distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any means.

> I don't know how E.T. could make a living out of selling a crude Brainfuck
> compiler but if that's what it wants, it's free to do so.

See [`brainfuck.c`](src/brainfuck.c) for the full text of the *license*.

## Contributors

Here's a list of people that have officially donated more than 0 presses of
keyboard keys to this project:

* Goksel Goktas (author)
