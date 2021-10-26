/*
 * Heap.h
 *
 * An emulation of a continuous block of memory organized as a sorted array of arrays
 *
 *  Created on: 26.10.2021
 *      Author: bsoelch
 */

#ifndef HEAP_H_
#define HEAP_H_

#include "Structs.h"

Heap createHeap(size_t initSize);
void freeHeap(Heap* heap);

uint64_t heapRead(Heap heap, uint64_t addr,ErrorCode* err);
ErrorCode heapWrite(Heap heap,uint64_t addr,uint64_t data);

//XXX block read/write
//XXX? memmove


ErrorCode heapEnsureCap(Heap* heap  ,uint64_t minSize);


#endif /* HEAP_H_ */
