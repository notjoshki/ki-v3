# Ki v3

Ki is a low-level programming language written from scratch. It's completely independent from [libc](https://en.wikipedia.org/wiki/C_standard_library) by using its own self-hosted libraries, and independent from [llvm](https://en.wikipedia.org/wiki/LLVM) by using a custom backend. It aims to be a fun, non-production improvement on C.

> [!WARNING]
> Ki is still in early development, expect breaking bugs and missing features.

## Notable Features

- Modules
- Default values
- Inferred types
- Decorators
- Builtin strings
- Builtin markdown generator
- Builtin IR string representation generator
- Blazingly-fast compile times

## Installation

```console
$ git clone https://github.com/notjoshki/ki-v3.git
$ cd ki-v3
$ sudo make install
```

## Examples

Hello world:

```rs
main() {
    print("Hello, world!\n");
}
```

Print the alphabet in ascending order:

```rs
main() {
    for character := 'A'..='Z' {
        print_char(character);
    }

    print("\n");
}
```

Greet the user:

```rs
main() {
    name := input("What is your name? ");
    print_format("Hello, %\n", name);
    delete_string(name);
}
```

You can find more advanced examples [here](./examples/), or see the [compiler tests](./tests/).

## Usage

```
ki <command> [options...] <file>
```

| Command | Description |
| --- | --- |
| ```build``` | Produce an executable |
| ```document``` | Produce a markdown file |
| ```ir``` | Produce an IR file |
| ```object``` | Produce an object file |
| ```source``` | Produce an assembly file |

| Option | Description |
| --- | --- |
| ```--help``` | Show this information |
| ```--version``` | Show the compiler version |
| ```-as-flags <...>``` | Specify flags to pass during assembling |
| ```-doc-exported``` | Show only exported symbols in documentation |
| ```-freestanding``` | Don't use the standard library or initialize the heap |
| ```-g``` | Build with debugging information |
| ```-ir-explicit``` | Generate IR in explicit form |
| ```-ld <path>``` | Specify the linker to use in compilation |
| ```-ld-flags <...>``` | Specify flags to pass during linkage |
| ```-o <name>``` | Specify the output filename |
| ```-unopt``` | Disable optimization |

## License

Ki is distributed under the [BSD 3-Clause](./LICENSE) license.