////////////////////////////////////////////////////////////////////////////////
//
//  File           : raid_cache.c
//  Description    : This is the implementation of the cache for the TAGLINE
//                   driver.
//
//  Author         : Leonardo de la Cruz
//  Last Modified  : 12/09/2015
//

// Includes
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>

// Project includes
#include <cmpsc311_log.h>
#include <cmpsc311_util.h>
#include <raid_cache.h>


//Global Variables

//max number of items allowed in the cache
uint32_t glob_max_items;
//Number of objects currently in the cache
int objectCount = 0;
//Count for cache insets, gets, hits and misses
int hit = 0, miss = 0, insert = 0, get = 0;
//Keep track of time
uint64_t timea = 0;
uint64_t oldestTime = 0;


//Structures

//Cache structure
struct cache
{
	char *data;
	uint64_t timestamp;
	RAIDDiskID disk;
	RAIDBlockID block;
};

//Global Pointer for cacheArray
struct cache *cacheArray;


// TAGLINE Cache interface

////////////////////////////////////////////////////////////////////////////////
//
// Function     : init_raid_cache
// Description  : Initialize the cache and note maximum blocks
//
// Inputs       : max_items - the maximum number of items your cache can hold
// Outputs      : 0 if successful, -1 if failure
int init_raid_cache(uint32_t max_items) {

	int i;

	//Save number of items in the cache
	glob_max_items = max_items;
	

	//Create array of Structure
	cacheArray = (struct cache*)malloc(max_items * sizeof(struct cache));
	if (cacheArray == NULL)
		return (-1);

	for (i=0; i<glob_max_items; i++){
		//Allocate for each block
		cacheArray[i].data = (char *) malloc(RAID_BLOCK_SIZE);
		if (cacheArray[i].data == NULL)
			return (-1);

		//Set and invalid value to unused blocks
		cacheArray[i].disk = -1;
		cacheArray[i].block = -1;
	}


	// Return successfully
	return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : close_raid_cache
// Description  : Clear all of the contents of the cache, cleanup
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure
int close_raid_cache(void) {

	int i;
	double efficiency;

	efficiency = ((double)hit/((double)hit+(double)miss))*100;

	//Deallocate
	for (i = 0; i<glob_max_items; i++){
		free(cacheArray[i].data);
		cacheArray[i].data = NULL;
	}

	free(cacheArray);
	cacheArray = NULL;

	//Print Statistics
	logMessage(LOG_OUTPUT_LEVEL, "** Cache Statistics **");
	logMessage(LOG_OUTPUT_LEVEL, "Total cache inserts:   %i", insert);
	logMessage(LOG_OUTPUT_LEVEL, "Total cache gets:      %i", get);
	logMessage(LOG_OUTPUT_LEVEL, "Total cache hits:      %i", hit);
	logMessage(LOG_OUTPUT_LEVEL, "Total cache misses:	 %i", miss);
	logMessage(LOG_OUTPUT_LEVEL, "Cache efficiency: 	 %f %%", efficiency);



	// Return successfully
	return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : put_raid_cache
// Description  : Put an object into the block cache
//
// Inputs       : dsk - this is the disk number of the block to cache
//                blk - this is the block number of the block to cache
//                buf - the buffer to insert into the cache
// Outputs      : 0 if successful, -1 if failure
int put_raid_cache(RAIDDiskID dsk, RAIDBlockID blk, void *buf)  {

	int i;
	int found = 0;//Bool
	int oldestPosition;//use to find position of oldest block

	//Keep track of time
	timea++;

	//Insert new blocks into the cache
	if(objectCount<glob_max_items){

		for(i=0; i<objectCount; i++){
			if(cacheArray[i].disk == dsk && cacheArray[i].block == blk){
				
				//is a hit!
				hit++;
				found = 1;

				//replace the data that is already there with the new info
				memcpy(cacheArray[i].data, buf, RAID_BLOCK_SIZE);
				
				//Count an insert
				insert++;

				//Update time
				cacheArray[i].timestamp = timea;

				//Break the loop
				break;
			}

		}
		//if it was not in the cache, is a new block
		if (!found){
			memcpy(cacheArray[objectCount].data, buf, RAID_BLOCK_SIZE);
			cacheArray[objectCount].disk = dsk;
			cacheArray[objectCount].block = blk;
			cacheArray[objectCount].timestamp = timea;
			objectCount++;
			
			//Count an insert
			insert++;
		}
	}

	//Cache is full, update with new blocks
	else{

		for (i=0; i<glob_max_items; i++){
			//Check if the item is alreay on the cache
			if(cacheArray[i].disk == dsk && cacheArray[i].block == blk){
				//is a hit!
				hit++;
				found = 1;
				//replace the data that is already there with the new info
				memcpy(cacheArray[i].data, buf, RAID_BLOCK_SIZE);
				cacheArray[i].timestamp = timea;

				//Count an insert
				insert++;

				//Break the loop
				break;

			}

		}

		//if it went though the whole array and the block is not found is a miss
		if (!found){
			//is a miss
			miss++;

			//find the oldest block
			oldestTime = cacheArray[0].timestamp;
			oldestPosition = 0;

			for(i=0; i<glob_max_items; i++){
				if(cacheArray[i].timestamp < oldestTime){
					oldestPosition = i;
					oldestTime = cacheArray[i].timestamp;
				}

			}

			//replaced the block that is oldestTime
			memcpy(cacheArray[oldestPosition].data, buf, RAID_BLOCK_SIZE);
			cacheArray[oldestPosition].disk = dsk;
			cacheArray[oldestPosition].block = blk;
			cacheArray[oldestPosition].timestamp = timea;

			//Count an insert
			insert++;
		}
	}

	// Return successfully
	return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : get_raid_cache
// Description  : Get an object from the cache (and return it)
//
// Inputs       : dsk - this is the disk number of the block to find
//                blk - this is the block number of the block to find
// Outputs      : pointer to cached object or NULL if not found
void * get_raid_cache(RAIDDiskID dsk, RAIDBlockID blk) {

	int i;

	//Keep track of time
	timea++;

	//Count a get
	get++;

	for (i=0; i <glob_max_items; i++){
		//If found, return pointer to object
		if(cacheArray[i].disk == dsk && cacheArray[i].block == blk){
			//Hit!!!
			hit++;
			//Update time
			cacheArray[i].timestamp = timea;
			return cacheArray[i].data;
		}

	}
	//Miss!!!
	miss++;
	//Not found so return NULL
	return (NULL);

}