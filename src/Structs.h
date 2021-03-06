/*
 * Mapable.h
 *
 *  Created on: 18.10.2021
 *      Author: bsoelch
 */

#ifndef STRUCTS_H_
#define STRUCTS_H_

#include <stdint.h>
#include <stdbool.h>
#ifndef _WIN32
#include <stddef.h>
#endif

//only use 48bits for mem-addresses
#define MAX_MEM_ADDR         0xffffffffffffULL

#define SYS_REG_COUNT                     8ULL
#define SYS_REG_MASK                      7ULL
//System interaction calls
#define CALL_RESIZE_HEAP  0
#define CALL_RESIZE_STACK 1
//CALL_MEM_MOVE
#define CALL_READ         2
#define CALL_WRITE        3
//CALL_OPEN   path, path_len, flags -> fd
//CALL_CLOSE  fd

//Typedefs with readable names for sys-registers
#define REG_HEAP_MIN sysReg[1]
#define REG_IO_FD sysReg[1]
#define REG_IO_TARGET sysReg[2]
#define REG_IO_COUNT sysReg[3]
#define PTR_REG_IO_COUNT sysReg+3
#define REG_COUNT_MASK 0x8


#define LABEL_BITS 		   32
#define MAX_LABEL  0xffffffff

typedef struct{
	size_t len;
	char* chars;//the only reason chars is not const is to allow the use in free
}String;

typedef enum{
NO_ERR=0,
ERR_BREAKPOINT,
ERR_BREAK_STEP,
ERR_BREAK_AT,

ERR_MEM,
ERR_IO,
ERR_FILE_NOT_FOUND,

ERR_EXPANSION_OVERFLOW,
ERR_IF_STACK_OVERFLOW,
ERR_UNRESOLVED_MACRO,
ERR_INVALID_IDENTIFER,
ERR_INVALID_ACTION,
ERR_UNEXPECTED_MACRO_DEF,
ERR_UNEXPECTED_MACRO_END,
ERR_UNFINISHED_IF,
ERR_UNEXPECTED_ELSE,
ERR_UNEXPECTED_ENDIF,
ERR_UNFINISHED_IDENTIFER,
ERR_UNFINISHED_MACRO,
ERR_UNFINISHED_COMMENT,
ERR_UNRESOLVED_LABEL,
ERR_LABEL_REDEF,
ERR_MACRO_REDEF,
ERR_RESET_OVERWRITE,//two consecutive reset instructions
ERR_LABEL_OVERFLOW,//label overflows max allowed value

ERR_HEAP_ILLEGAL_ACCESS,
ERR_HEAP_OUT_OF_MEMORY,
}ErrorCode;

typedef struct CodePosImpl CodePos;

struct CodePosImpl{
	String file;
	size_t line;
	size_t posInLine;
	CodePos* at;
};

typedef struct{
	ErrorCode errCode;
	CodePos pos;
}ErrorInfo;


typedef enum{
	INVALID=-1,
	NOP,//no op
	RESET,//sets A to 0
	FLIP,//flips the lowest bit in A
	ROT,//rotates the bits of A 1 to the right
	LROT,//rotates the bits of A 1 to the left
	SWAP,//swaps A and B
	JUMPIF,//swaps B with the current code position if A&1 is true
	LOAD,//replaces A with mem[A]
	STORE,//stores A in mem[B]
	SYSTEM,//performs a system interaction action
	//debug commands
	BREAKPOINT,
	//compiler commands
	COMMENT_START,//start of comment
	IFDEF,//conditional statements on labels
	IFNDEF,
	ELSE,
	ENDIF,
	UNDEF,//remove label/macro
	LABEL,//unresolved label
	INCLUDE,//unresolved include statement
	LABEL_DEF,//label definition (only in macros)
	MACRO_START,//start of macro-block
	MACRO_END,//end of block
}ActionType;

typedef struct{
	ActionType type;
	union{
		int64_t asInt;
		String asString;
	}data;
	CodePos* at;
} Action;

typedef struct{
	size_t len;
	size_t cap;
	Action* actions;
	uint64_t flags;
}Program;

#define PROG_FLAG_LABEL 0x1
typedef Program Macro;

void freeMacro(Macro* m);

typedef struct{
	size_t len;
	size_t cap;
	size_t* data;
}PosArray;

//for information on Heap functionality see Heap.h
typedef struct HeapSectionImpl HeapSection;
struct HeapSectionImpl{
	uint64_t start;
	size_t len;
	char* data;
};
typedef struct{
	size_t cap;
	size_t len;
	HeapSection* sections;
}Heap;

typedef struct{
	uint64_t ip;
	bool jumped;

	uint64_t regA;
	uint64_t regB;

	Heap heap;
	uint64_t stackStart;
	Heap stack;
	//registers of system calls,
	//ordered in revere order sysReg[0] has the highest id
	//sysReg[0] has the same id as MEM_SYS_CALL
	uint64_t sysReg [SYS_REG_COUNT];
}ProgState;

typedef struct MapImpl HashMap;
typedef enum{
	MAPABLE_NONE,
	MAPABLE_POS,
	MAPABLE_POSARRAY,
	MAPABLE_MACRO
}MapableType;
typedef struct{
	MapableType type;
	union{
		size_t asPos;
		Macro asMacro;
		PosArray asPosArray;
	}value;
}Mapable;

typedef struct{
	bool isError;
	union{
		Action action;
		ErrorInfo error;
	}as;
}ActionOrError;


typedef enum{
	DISPLAY_BYTE,
	DISPLAY_CHAR,
	DISPLAY_INT16,
	DISPLAY_INT32,
	DISPLAY_INT64
}MemDisplayMode;
typedef struct MemDisplayImpl MemDisplay;

struct MemDisplayImpl{
	String label;
	bool first;
	MemDisplayMode mode;//1,2,4,8
	uint64_t addr;
	size_t count;
	MemDisplay* next;
};

typedef struct{
	size_t maxSteps;
	HashMap* breakFlips;
	//XXX breakAt ...
	bool showSysRegs;
	MemDisplay* memDisplays;
}DebugInfo;

#endif /* STRUCTS_H_ */
