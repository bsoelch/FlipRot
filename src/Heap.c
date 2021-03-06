/*
 * Heap.c
 *
 *  Created on: 26.10.2021
 *      Author: bsoelch
 */
#include "Heap.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

//initial capacity of the sections array
static const size_t INIT_CAP_HEAP=16;
//minimal size of a heap section
static const size_t MIN_SECTION_SIZE=0x100;
//maximal size of a heap section
static const size_t MAX_SECTION_SIZE=0x10000000;

/*creates a Heap with an initial Memory size of initSize
 * if memory allocation fails Heap.section will be NULL*/
Heap createHeap(){
	Heap create;
	create.len=0;
	create.cap=INIT_CAP_HEAP;
	create.sections=malloc(INIT_CAP_HEAP*sizeof(HeapSection));
	if(!create.sections){
		create.cap=0;
		return create;
	}
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

uint64_t heapSize(Heap heap){
	if(heap.len>0){
		return heap.sections[heap.len-1].start+heap.sections[heap.len-1].len;
	}
	return 0;
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

ErrorCode heapRead(Heap heap, uint64_t addr, char* res,size_t count){
	size_t index=sectionIndex(heap,addr);
	if(index<heap.len){
		HeapSection initSection = heap.sections[index];
		size_t rem=initSection.len+initSection.start-addr;
		while(rem<count){
			memcpy(res,initSection.data+(addr - initSection.start),rem);
			res+=rem;
			addr+=rem;
			count-=rem;
			index++;
			if(index>=heap.len){
				return ERR_HEAP_ILLEGAL_ACCESS;
			}
			initSection = heap.sections[index];
			rem=initSection.len+initSection.start-addr;
		}
		memcpy(res,initSection.data+(addr - initSection.start),count);
		return NO_ERR;
	}
	return ERR_HEAP_ILLEGAL_ACCESS;
}

/*read with reversed address-space (for stack-section)
 * blocks are interpreted to be in the order
 * [-(N-1),-(N-2),...,0],[-(N+M-1),...,-(N+1),-N],[...,-(N+M)],...
 * */
ErrorCode heapReadReversed(Heap heap, uint64_t addr, char* res,size_t count){
	if(addr<count-1){
		return ERR_HEAP_ILLEGAL_ACCESS;
	}
	size_t index=sectionIndex(heap,addr);
	if(index<heap.len){
		HeapSection initSection = heap.sections[index];
		size_t rem=addr-initSection.start+1;
		while(rem<count){
			memcpy(res,initSection.data+initSection.len-1-(addr-initSection.start),rem);
			res+=rem;
			addr-=rem;
			count-=rem;
			index--;
			if(index<0){
				return ERR_HEAP_ILLEGAL_ACCESS;
			}
			initSection = heap.sections[index];
			rem=addr-initSection.start+1;
		}
		memcpy(res,initSection.data+initSection.len-1-(addr-initSection.start),count);
		return NO_ERR;
	}
	return ERR_HEAP_ILLEGAL_ACCESS;
}

ErrorCode heapWrite(Heap heap, uint64_t addr, char* data,size_t count){
	size_t index=sectionIndex(heap,addr);
	if(index<heap.len){
		HeapSection initSection = heap.sections[index];
		size_t rem=initSection.start+initSection.len-addr;
		while(rem<count){
			memcpy(initSection.data+(addr - initSection.start),data,rem);
			data+=rem;
			addr+=rem;
			count-=rem;
			index++;
			if(index>=heap.len){
				return ERR_HEAP_ILLEGAL_ACCESS;
			}
			initSection = heap.sections[index];
		}
		memcpy(initSection.data+(addr - initSection.start),data,count);
		return NO_ERR;
	}
	return ERR_HEAP_ILLEGAL_ACCESS;
}
/*read with reversed address-space (for stack-section)
 * blocks are interpreted to be in the order
 * [-(N-1),-(N-2),...,0],[-(N+M-1),...,-(N+1),-N],[...,-(N+M)],...
 * */
ErrorCode heapWriteReversed(Heap heap, uint64_t addr, char* data,size_t count){
	if(addr<count-1){
		return ERR_HEAP_ILLEGAL_ACCESS;
	}
	size_t index=sectionIndex(heap,addr);
	if(index<heap.len){
		HeapSection initSection = heap.sections[index];
		size_t rem=addr-initSection.start+1;
		while(rem<count){
			memcpy(initSection.data+initSection.len-1-(addr-initSection.start),data,rem);
			data+=rem;
			addr-=rem;
			count-=rem;
			index--;
			if(index<0){
				return ERR_HEAP_ILLEGAL_ACCESS;
			}
			initSection = heap.sections[index];
			rem=addr-initSection.start+1;
		}
		memcpy(initSection.data+initSection.len-1-(addr-initSection.start),data,count);
		return NO_ERR;
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
		heap->len++;
		return NO_ERR;
	}
	return ERR_HEAP_OUT_OF_MEMORY;
}

/**updates that heap ensuring that the memory capacity is at least minSize
 * return ERR_HEAP_OUT_OF_MEMORY of growing the heap is not possible*/
ErrorCode heapEnsureCap(Heap* heap, uint64_t minSize,uint64_t maxSize){
	if(minSize>maxSize){
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


