# FlipRot
FlipRot is minimalistic programming language with only 2 data manipulation operations (flip and rot)

The main idea behind this project is to define a programming language with a minimalistic instruction set that defines most instructions though compile time macros and subroutines 

The source code of the languare consists of actions separated by whitespaces, all code words that are not a keyword for the main language of the compiler instructions are interpreted as identifers that can be extended from labels or macros (identifers that are used before a definition can only be replaced by labels)

## Core Language
The main language constists of 7 actions.
### LOAD_INT
loads an (64-bit) integer-constant into the main register. 
All integers in the source code are implicitly converted to LOAD_INT instructions,
integers cstarting with 0x are interpredted as hexadecimal

### SWAP
swaps the main and secondary register

### LOAD
replaces the value in the main register with the value in memeory at the corresponding adress

### STORE
places the value in the main register at the memory adress in the secondary register

### JUMPIF
if the lowest bit of the value in the main register is one the program
jumps to the address in the secondary register and 
the secondary register is set to the address after this instruction
otherwise no operation is performed

The behaviour of jump is undefined if the target addess is not set as a label

### ROT
rotates the the value in the main register by one bit to the right

Examples:
```
2 rot
```
main register is 1

```
42 rot rot
```
main register is 0x8000 0000 0000 000A

### FILP
flips the lowest bit of the value in the main register

Examples:
```
2 flip
```
main register is 3

```
1 flip
```
main register is 0

## IO
when reading and writing from special memory addresses IO-operations are prefored
### Char-IO
the adress 0xffffffffffffffff (2^64-1) is reserved for character IO,
currently only ascii characters are suppored, 
it is planed to support in/output of all unicode-characters 

- loading from this address reads a character from the console and moves the char-id in the main register
- storing to this address writes the character with the id in the main register to the console

### Int-IO
the adress 0xfffffffffffffffe (2^64-2) is currently used for direct IO of (64bit) integers,
direct IO of numbers may be removed later

- loading from this address reads a 64bit integer from the console and moves it to the main register
- storing to this address writes the value of the main register (as hexadecimal number) to the console


## Compile Time Macros

### Comments
Comments start with '#__' and end with '#end'

Example:
```
code 
#__ this is a comment #end more code
```

### lables
Labels are defined by a '#label' followed by an identifer, 
when derecerenced, a lable points to the codeposition after the label definition

Example:
```
jumpif skip
#label loop
rot
jumpif loop
#label skip
```
jumps to skip if the lowest bit of the main register is one, 
otherwise the main register is rotated until the lowest bit is a zero

### macros
Macros start with '#def' followed by an identifer and end with '#end'
every usage of the macro-identifer is replaced with the code-block between the identifer and end

It is currently not allowed to define macros within macros, label in macros can be defined, but have to be undefined (#undef)
to reuse the macro

Example:

```
#def rot2 rot rot #end
4 rot2
```
is reduced to 
```
4 rot rot
```
which leads to a 1 in the main register

### Undefining lables and macros
using '#undef' followed by a identifiery removes the macro or label with that identifer
```
#label target
#undef target
target swap
1 jumpif
#label target
```
jumps to the second definition of target instead of the first one
	

