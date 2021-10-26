/*
 * HashMap.h
 *
 *  Created on: 18.10.2021
 *      Author: bsoelch
 */

#ifndef HASHMAP_H_
#define HASHMAP_H_

#include <stdio.h>
#include <stdbool.h>
#include "Structs.h"

static const Mapable NONE={.type=MAPABLE_NONE,.value.asPos=NO_ERR};
static const Mapable MEM_ERROR={.type=MAPABLE_NONE,.value.asPos=ERR_MEM};

typedef struct MapImpl HashMap;

HashMap* createHashMap(size_t capacity);

size_t freeMapable(Mapable e);
//the return value is the bitwise or of the return-values of freeMapable
size_t freeHashMap(HashMap* map,size_t(*freeMapable)(Mapable));


/**sets the map entry for key to entry,
 * if entry.typeis NONE the previous entry is removed without adding a new entry
 * return the previous entry for key,or NONE is there is no previous key
 *  if a memory error occurs OUT_OF_MEMORY is returned
 *  !!! the given key is directly used in the map without performing a copy!!!*/
Mapable mapPut(HashMap* map,String key,Mapable entry);
/**gets the map entry for the given key
 * return the entry for key,or NONE is there is none*/
Mapable mapGet(HashMap* map,String key);

#endif /* HASHMAP_H_ */
