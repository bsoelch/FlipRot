/*
 * Mapable.h
 *
 *  Created on: 18.10.2021
 *      Author: bsoelch
 */

#ifndef STRUCTS_H_
#define STRUCTS_H_

#include <stdint.h>


typedef struct{
	size_t len;
	char* chars;//the only reason chars is not const is to allow the use in free
}String;

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
	END_DEF,//end of block
}ActionType;

typedef struct{
	ActionType type;
	union{
		int64_t asInt;
		String asString;
	}data;
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

typedef struct{
	uint64_t regA;
	uint64_t regB;

	size_t memCap;
	uint64_t* mem;
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

#define NO_ERR 0
#define ERR_MEM -1
#define ERR_IO -2
#define ERR_FILE_NOT_FOUND -3
#define ERR_MAPABLE_TYPE -4

#define ERR_EXPANSION_OVERFLOW 1
#define ERR_UNRESOLVED_MACRO 2
#define ERR_INVALID_IDENTIFER 3
#define ERR_UNEXPECTED_MACRO_DEF 4
#define ERR_UNEXPECTED_END 5
#define ERR_UNFINISHED_IDENTIFER 6
#define ERR_UNFINISHED_MACRO 7
#define ERR_UNFINISHED_COMMENT 8
#define ERR_UNRESOLVED_LABEL 9
#define ERR_MACRO_REDEF 10

#endif /* STRUCTS_H_ */
