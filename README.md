# Ki v3

Ki is a low-level programming language written from scratch. It's completely independant from [libc](https://en.wikipedia.org/wiki/C_standard_library) and [llvm](https://en.wikipedia.org/wiki/LLVM); using a custom backend and its own self-hosted libraries. It aims to be a fun, non-production improvement on C.

> [!WARNING]
> Ki is still in early development, expect breaking bugs and missing features.

## Notable Features

- Modules
- Default values
- Inferred types
- Decorators
- Builtin strings
- Builtin documentation generator
- Automatic pointer dereferencing

## Installation

```console
$ git clone https://github.com/notjoshki/ki-v3.git
$ cd ki-v3
$ make install
```

## Examples

Hello world:

```cs
using ki.basic;

main() {
    print("Hello, world!\n");
}
```

Print the alphabet in ascending order:

```cs
using ki.basic;

main() {
    for character := 'A'..='Z' {
        print_char(character);
    }

    print("\n");
}
```

Greet the user:

```cs
using ki.basic;

BUFFER_CAPACITY: const 32;

main() {
    print("What is your name? ");

    buffer: char[BUFFER_CAPACITY];
    scan(buffer, BUFFER_CAPACITY - 1);

    print("Hello, ");
    println(buffer_to_string(buffer));
}
```

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
| ```-doc-exported``` | Show only exported symbols in documentation |
| ```-freestanding``` | Don't use the standard library or initialize the heap |
| ```-g``` | Build with debugging information |
| ```-ir-explicit``` | Generate IR in explicit form |
| ```-o <name>``` | Specify the output filename |
| ```-unopt``` | Disable optimization |

## License

Ki is distributed under the [BSD 3-Clause](./LICENSE) license.