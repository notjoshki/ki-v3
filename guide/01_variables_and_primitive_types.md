# Variables and Primitive Types

Variables are declared in the ```name: type``` format, and a starting value is optional.

```rs
name: type = value;
```

Ki supports inferred types which you can achieve by just leaving out the type. These do require a starting value.

```rs
name := value;
```

Pointers are declared by adding an ampersand before the type name. You can have any number of ampersands.

```rs
name: &type = value;
```

Arrays are declared with the array size after the type name, and currently the array size must be a constant integer. The only value they can be initialized with is an array initializer.

```rs
name: type[32];
```

Here is a list of the primitive Ki data types.

| Type | Description | Size |
| --- | --- | --- |
| ```void``` | No type or data | 0 bytes, 8 bytes as a pointer |
| ```bool``` | Boolean | 1 byte |
| ```char``` | Unsigned character | 1 byte |
| ```i8``` | 8 bit signed integer | 1 byte |
| ```i16``` | 16 bit signed integer | 2 bytes |
| ```i32``` | 32 bit signed integer | 4 bytes |
| ```i64``` | 64 bit signed integer | 8 bytes |
| ```u8``` | 8 bit unsigned integer | 1 byte |
| ```u16``` | 16 bit unsigned integer | 2 bytes |
| ```u32``` | 32 bit unsigned integer | 4 bytes |
| ```u64``` | 64 bit unsigned integer | 8 bytes |
| ```f32``` | 32 bit floating-point | 4 bytes |
| ```f64``` | 64 bit floating-point | 8 bytes |
| ```isize``` | Signed largest size integer | Architecture specific |
| ```usize``` | Unsigned largest size integer | Architecture specific |
