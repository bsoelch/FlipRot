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

#define LOW_MEM_SIZE               0x100000ULL
#define HIGH_MEM_SIZE              0x100000ULL

#define MEM_MASK_INVALID 0xffff000000000000ULL
#define MEM_MASK_HIGH        0xfffffff00000ULL
#define MEM_HIGH_START       0xfffffff00000ULL
#define HIGH_ADDR_MASK       0x0000000fffffULL
#define MEM_MASK_LOW         0xfffffff00000ULL
#define MEM_LOW_START        0x000000000000ULL
#define LOW_ADDR_MASK        0x0000000fffffULL
//replace with read bytes
#define CHAR_IO_ADDR     0xffffffffffffffffULL
//LONG_IO only for debug purposes, may be removed later
#define LONG_IO_ADDR     0xfffffffffffffffeULL

//only use 48bits for mem-addresses

typedef struct{
	size_t len;
	char* chars;//the only reason chars is not const is to allow the use in free
}String;

typedef enum{
NO_ERR=0,

ERR_MEM,
ERR_IO,
ERR_FILE_NOT_FOUND,
ERR_MAPABLE_TYPE,

ERR_EXPANSION_OVERFLOW,
ERR_UNRESOLVED_MACRO,
ERR_INVALID_IDENTIFER,
ERR_UNEXPECTED_MACRO_DEF,
ERR_UNEXPECTED_END,
ERR_UNFINISHED_IDENTIFER,
ERR_UNFINISHED_MACRO,
ERR_UNFINISHED_COMMENT,
ERR_UNRESOLVED_LABEL,
ERR_MACRO_REDEF,

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
	LOAD_INT,//load int64 to A
	SWAP,//swaps A and B
	LOAD,//replaces A with mem[A]
	STORE,//stores A in mem[B]
	JUMPIF,//swaps B with the current code position if A&1 is true
	ROT,//rotates the bits of A 1 to the right
	FLIP,//flips the lowest bit in A
	//compiler commands
	COMMENT_START,//start of comment
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
	uint64_t* data;
};
typedef struct{
	size_t cap;
	size_t len;
	HeapSection* sections;
}Heap;

typedef struct{
	uint64_t regA;
	uint64_t regB;

	Heap heap;
	uint64_t* highMem;
}ProgState;

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

#endif /* STRUCTS_H_ */
