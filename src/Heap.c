/*
 * Heap.c
 *
 *  Created on: 26.10.2021
 *      Author: bsoelch
 */
#include "Heap.h"
#include <stdlib.h>
#include <assert.h>

//initial capacity of the sections array
static const size_t INIT_CAP_HEAP=16;
//minimal size of a heap section
static const size_t MIN_SECTION_SIZE=0x1000;
//maximal size of a heap section
static const size_t MAX_SECTION_SIZE=0x10000000;

/*creates a Heap with an initial Memory size of initSize
 * if memory allocation fails Heap.section will be NULL*/
Heap createHeap(size_t initSize){
	Heap create;
	create.len=0;
	create.cap=INIT_CAP_HEAP;
	create.sections=malloc(INIT_CAP_HEAP*sizeof(HeapSection));
	if(!create.sections){
		create.cap=0;
		return create;
	}
	heapEnsureCap(&create,initSize);
	return create;
}
void freeHeap(Heap* heap){
	if(heap->sections){
		while(heap->len>0){
			free(heap->sections[--heap->len].data);
		}
		free(heap->sections);
		heap->sections=NULL;
		heap->cap=0;
	}
}

static size_t sectionIndex(Heap heap, uint64_t addr){
	size_t min=0;
	size_t max=heap.len;
	size_t index;
	do{
		index=(min+max)/2;
		if(addr<heap.sections[index].start){
			max=index;
		}else if(addr>=(heap.sections[index].start+heap.sections[index].len)){
			min=index;
		}else{
			return index;
		}
	}while(max>min);
	return SIZE_MAX;
}

uint64_t heapRead(Heap heap, uint64_t addr, ErrorCode* err){
	size_t index=sectionIndex(heap,addr);
	if(index<SIZE_MAX){
		*err=NO_ERR;
		HeapSection initSection = heap.sections[index];
		if(index+sizeof(uint64_t)<=initSection.start+initSection.len){
			return *(uint64_t*)(initSection.data+(addr - heap.sections[index].start));
		}else{
			//TODO heap access across boundary
			*err=ERR_HEAP_ILLEGAL_ACCESS;
			return 0;
		}
	}
	*err=ERR_HEAP_ILLEGAL_ACCESS;
	return 0;
}
ErrorCode heapWrite(Heap heap, uint64_t addr, uint64_t data){
	size_t index=sectionIndex(heap,addr);
	if(index<SIZE_MAX){
		HeapSection initSection = heap.sections[index];
		if(index+sizeof(uint64_t)<=initSection.start+initSection.len){
			*((uint64_t*)(initSection.data+(addr - heap.sections[index].start)))=data;
			return NO_ERR;
		}else{
			//TODO heap access across boundary
			return ERR_HEAP_ILLEGAL_ACCESS;
		}
	}
	return ERR_HEAP_ILLEGAL_ACCESS;
}

static ErrorCode createSection(Heap* heap, uint64_t size){
	if(heap->cap<=heap->len){
		HeapSection* tmp=realloc(heap->sections,2*heap->cap*sizeof(HeapSection));
		if(!tmp){
			return ERR_HEAP_OUT_OF_MEMORY;
		}
		heap->sections=tmp;
		heap->cap*=2;
	}
	heap->sections[heap->len].data=malloc(size);
	if(heap->sections[heap->len].data){
		if(heap->len==0){
			heap->sections[heap->len].start=0;
		}else{
			heap->sections[heap->len].start=heap->sections[heap->len-1].start+
					heap->sections[heap->len-1].len;
		}
		heap->sections[heap->len].len=size;
		return NO_ERR;
	}
	return ERR_HEAP_OUT_OF_MEMORY;
}

/**updates that heap ensuring that the memory capacity is at least minSize
 * return ERR_HEAP_OUT_OF_MEMORY of growing the heap is not possible*/
ErrorCode heapEnsureCap(Heap* heap, uint64_t minSize){
	if(minSize>MEM_STACK_START){
		//heap cannot grow up to stack
		return ERR_HEAP_OUT_OF_MEMORY;
	}//no else
	if(minSize%sizeof(uint64_t)!=0){
		//size has to be a multiple of sizeof(uint64_t)
		minSize=((minSize/sizeof(uint64_t))+1)*sizeof(uint64_t);
	}
	ErrorCode ret;
	if(heap->len>0){
		HeapSection last=heap->sections[heap->len-1];
		if(minSize<last.start+last.len){
			//unlink unused nodes
			while(minSize<last.start){
				assert(heap->len>0);
				free(last.data);
				heap->len--;
				last=heap->sections[heap->len-1];
			}
			return NO_ERR;
		}
		minSize-=last.start+last.len;
	}
	while(minSize>MAX_SECTION_SIZE){
		ret=createSection(heap,MAX_SECTION_SIZE);
		if(ret){
			return ret;
		}
		minSize-=MAX_SECTION_SIZE;
	}
	if(minSize<MIN_SECTION_SIZE){
		minSize=MIN_SECTION_SIZE;
	}
	ret=createSection(heap,minSize);
	if(ret){
		return ret;
	}
	return NO_ERR;
}


