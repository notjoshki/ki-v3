# Introduction to the Ki Programming Language

I'm not good at writing, so I'll try to keep this guide short, but hopefully good enough at pushing you in the right direction to start using Ki. It's a very simple language, so if you're used to C then it should be very easy to get the hang of. 

By the way, it's pronounced like "sky" but without the S ```(/kaɪ/)```, not "key".

## Compiler Dependencies

Ki only supports Linux on x86_64 computers at the moment, but you can run it in a VM if you don't have those specs. The philosophy of the Ki compiler is to use as little dependencies as possible, but there are still a few you will need before you build and use the compiler:

- gcc - GNU C Compiler
- make - GNU Make
- nasm - Netwide Assembler

These will need to be installed and in your $PATH before continuing. They can all usually be installed via your distribution's package manager, as these dependencies are quite widely used.

## Building the Compiler

Now you should be ready to build the compiler from source. If you don't already, you should have ```git``` installed to fetch the source code from github, or you could download the repository as a tar archive.

Follow the below commands to use git to fetch the source code from github and build the compiler using the makefile:

```
$ git clone https://github.com/notjoshki/ki-v3.git
$ cd ki-v3
$ sudo make install
```

This will install the compiler executable into ```/usr/local/bin/``` and the library files into ```/usr/local/share/ki/```.

Test to make sure it installed properly by running ```ki --version``` which should print the current release tag of the compiler.

## Hello World!

Now that you have the compiler installed and working properly, it's time for sacred hello world ritual. Create a file called ```main.ki``` and copy the following code into it:

```rs
main() {
    print("Hello, World!\n");
}
```

By default, every Ki file imports the standard library automatically, though this can be disabled with a compiler option if you desired.

Now let's save the file and go back to the command line where we can compile and run the program. The Ki compiler has a few commands and options to choose from which you can view with ```ki --help```, though for this example we will just need the ```build``` command. Simply pass the command you want to use as the **first** argument and pass the file to compile as the **last** argument to the compiler, like so: ```ki build main.ki```.

Upon running that command and it executing blazingly-fast you should notice a new file called ```a.out``` in the current directory, that is the default name for the built executable if you don't specify an output filename. Go ahead and run the executable, and lo and behold you just ran Ki code on your computer, well done.

