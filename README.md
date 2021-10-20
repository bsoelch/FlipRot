# MacroLang
MacroLang is minimalistic programming language with compile-time macro system

The main idea behind this project is to define a programming language through compile time macros, based on a minimalistic instruction subset.

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
jumps to the code position in the secondary register if the lowest bit of the value in the main register is one

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

## Compile Time Macros

### Comments
Comments start with '#__' and end with '#end'

Example:
```
code 
#ignore this is a comment #end more code
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
	

