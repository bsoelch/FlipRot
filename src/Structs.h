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
	char* chars;
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
	END,//end of block
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
}Program;

typedef Program Macro;

void freeMacro(Macro m);

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

#endif /* STRUCTS_H_ */
