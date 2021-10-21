/*
 * HashMap.c
 *
 *  Created on: 18.10.2021
 *      Author: bsoelch
 */
#include "HashMap.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

typedef struct NodeImpl MapNode;

struct NodeImpl{
	String key;
	Mapable data;

	MapNode* next;
};
typedef struct MapImpl{
	MapNode** nodes;
	size_t capacity;
} HashMap;


HashMap* createHashMap(size_t capacity){
	HashMap* map=malloc(sizeof(HashMap));
	if(map){
		map->nodes=calloc(capacity,sizeof(MapNode*));
		if(!map->nodes){
			free(map);
			return NULL;
		}
		map->capacity=capacity;
	}
	return map;
}
int freeMapable(Mapable e){
	switch(e.type){
	case MAPABLE_NONE:return NO_ERR;
	case MAPABLE_POS:
		return NO_ERR;
	case MAPABLE_POSARRAY:
		free(e.value.asPosArray.data);
		return NO_ERR;
	case MAPABLE_MACRO:
		freeMacro(e.value.asMacro);
		return NO_ERR;
	}
	return ERR_MAPABLE_TYPE;
}
//the return value is the bitwise or of the return-values of freeMapable
int freeHashMap(HashMap* map,int(*freeMapable)(Mapable)){
	int ret=0;
	if(map){
		MapNode* node;
		MapNode* tmp;
		for(size_t i=0;i<map->capacity;i++){
			node=map->nodes[i];
			while(node){
				tmp=node;
				free(node->key.chars);
				if(freeMapable){
					ret|=freeMapable(node->data);
				}
				node=node->next;
				free(tmp);
			}
		}
		free(map->nodes);
		free(map);
	}
	return ret;
}

static int hashCode(String str){
	int code=0;
	for(size_t i=0;i<str.len;i++){
		code=code*31+str.chars[i];
	}
	return code;
}
//returns true if str1 is NOT equal to str2
static int stringNe(String str1,String str2){
	if(str1.len!=str2.len)
		return true;
	for(size_t i=0;i<str1.len;i++){
		if(str1.chars[i]!=str2.chars[i])
			return true;
	}
	return false;
}

static MapNode* createNode(String key,Mapable entry){
	MapNode* newNode=malloc(sizeof(MapNode));
	if(!newNode){
		return NULL;
	}
	newNode->key=key;
	newNode->data=entry;
	newNode->next=NULL;
	return newNode;
}

Mapable mapPut(HashMap* map,String key,Mapable entry){
	if(!map){
		return MEM_ERROR;
	}
	int index=hashCode(key)%map->capacity;
	if(map->nodes[index]){
		MapNode* node=map->nodes[index];
		MapNode** prev=map->nodes+index;
		while(stringNe(node->key,key)){
			if(node->next){
				prev=&(node->next);
				node=node->next;
			}else{
				if(entry.type!=MAPABLE_NONE){
					MapNode* newNode=createNode(key,entry);
					if(!newNode){
						return MEM_ERROR;
					}
					node->next=newNode;
				}
				return NONE;
			}
		}
		Mapable prevData=node->data;
		if(entry.type==MAPABLE_NONE){
			free(node->key.chars);
			MapNode* tmp=node->next;
			free(node);
			*prev=tmp;//unlink node
		}else{
			node->data=entry;
		}
		return prevData;
	}else{
		if(entry.type!=MAPABLE_NONE){
			MapNode* newNode=createNode(key,entry);
			if(!newNode){
				return MEM_ERROR;
			}
			map->nodes[index]=newNode;
		}
		return NONE;
	}
}
Mapable mapGet(HashMap* map,String key){
	if(!map){
		return NONE;
	}
	int index=hashCode(key)%map->capacity;
	if(map->nodes[index]){
		MapNode* node=map->nodes[index];
		while(stringNe(node->key,key)){
			if(node->next){
				node=node->next;
			}else{
				return NONE;
			}
		}
		return node->data;
	}
	return NONE;
}

