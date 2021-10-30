# FlipRot
FlipRot is minimalistic programming language with only 2 data manipulation operations (flip and rot)

The main idea behind this project is to define a programming language with a minimalistic instruction set that defines most instructions though compile time macros and subroutines 

The source code of the language consists of actions separated by whitespaces,
 all code words that are not a keyword for the main language or a compiler instructions 
 are interpreted as identifiers that can be extended from labels or macros 
 (identifiers that are used before a definition can only be replaced by labels)

## Core Language
The main language consists of 7 actions.
### LOAD_INT
loads an (64-bit) integer-constant into the main register. 
All integers in the source code are implicitly converted to LOAD_INT instructions,
integers starting with 0x are interpreted as hexadecimal

### SWAP
swaps the main and secondary register

### LOAD
replaces the value in the main register with the value in memory 
at the corresponding address

### STORE
places the value in the main register at the memory address in the secondary register

### JUMPIF
if the lowest bit of the value in the main register is one the program
jumps to the address in the secondary register and 
the secondary register is set to the address after this instruction
otherwise no operation is performed

The behavior of jump is undefined if the target address is not set through a label

### ROT
rotates the value in the main register by one bit to the right

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

### SYS
The sys keyword allows a limited the interaction with the operating system 
(mainly IO), sys functions similar to store 
but is restricted to specifc system registers 

the action of sys depends on the value of the secondary register.

If it is nonzero, the value in the main register is written to sys-register 
given by the secondary register. 
If the system register contained an output value, that value is moved to regA

IF the secondary register is 0 a "sys-call" (no direct connection to linux syscalls) 
is performed using the system-registers as arguments.

Currently the supported CALL_IDs are:

#### CALL_RESIZE_HEAP:
(Value: 0)
	
Ensures that the heap-size is at least the value in REG_SYS_1
	
#### CALL_READ:
(Value: 1)
	
Reads REG_SYS_3 bytes from the file REG_SYS_1 to the memory starting at REG_SYS_2 
    
#### CALL_WRITE:
(Value: 2)
	
Writes REG_SYS_3 bytes to the file REG_SYS_1 reading from memory starting at REG_SYS_2 


## Compile Time Macros

### Comments
Comments start with '#_ ' and end with '_#'

Example:

```
code 
#_ this is a comment _# more code
```

### labels
Labels are defined by a '#label' followed by an identifier, 
when dereferenced, a label points to the code position after the label definition

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

### include
Include includes a file in the source-code,
 file names with whitespaces have to be surrounded by " "
Files are searched relative to the /lib folder
 (located in the same folder as the executable), 
if the file does not exist in lib it is interpreted as an absolute path
Paths starting with . are interpreted relative to the current directory
If the given path does not have a file extension, 
the default file extension (.frs) is added

Examples:

```
include stack
include "text file.txt"
include .examples\hello_world
```
Includes:
- stack.frs from the standard library
- examples\hello_world.frs in the local directory
And tries to include
- text file.txt in the root directory

### macros
Macros start with '#def' followed by an identifier and end with '#enddef'
every usage of the macro-identifier is replaced with the code-block between the identifier and end

It is currently not allowed to define macros within macros, label in macros can be defined, but have to be undefined (#undef)
to reuse the macro

Example:

```
#def rot2 rot rot #enddef
4 rot2
```
is reduced to 

```
4 rot rot
```
which leads to a 1 in the main register

### Undefining labels and macros
using '#undef' followed by a identifier removes the macro or label with that identifier

```
#label target
#undef target
target swap
1 jumpif
#label target
```
jumps to the second definition of target instead of the first one

### ifdef, ifndef, else, endif

 #ifdef and #ifndef allow to activate/deactivate code sections depending 
on if a specific label is defined.

```
#ifndef file_lock

#ifdef label
#_ label is defined _#
#else
#_ label is not defined _#
#endif

#def file_lock #enddef
#endif
```


## Memory
The program starts with 16kB of memory at the top of the addressable space 
(0x000000000000-0xffffffffffff)
which are reserved for the [stack](lib/stack.frs).

Through [CALL_RESIZE_HEAP](https://github.com/bsoelch/FlipRot#sys) more memory can be dynamically 
allocated at the lower end of the address space 


