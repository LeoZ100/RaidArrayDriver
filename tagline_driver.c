///////////////////////////////////////////////////////////////////////////////
//
//  File           : tagline_driver.c
//  Description    : This is the implementation of the driver interface
//                   between the OS and the low-level hardware.
//
//  Author         : Leonardo de la Cruz
//  Created        : 09/22/2015
//  Last Modified  : 12/09/2015

// Include Files
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <cmpsc311_log.h>

// Project Includes
#include "raid_bus.h"
#include "tagline_driver.h"
#include "raid_cache.h"
#include "raid_network.h"

//Definitions
#define false 0
#define true  1
typedef int bool;


//Global Variables and Structures

int maxtaglines = 1;
int *tagcounter = NULL;

struct disks
{
	int status;
	//Blocks in the disk are filled linearly, the number store in 'int block'
	//denotes that until that block disk is filled (0 based), -1 means is empty
	int blocks;
} array[RAID_DISKS] = { [0 ... RAID_DISKS-1].status = RAID_DISK_UNINITIALIZED};


//Each Block of the tagline have 4 properties, the disk, the backup disk and the diskblock 
//in which it was written to.
struct tagline
{
	int disk;
	int diskPosition;
	int backupDisk;
	int backupDiskPosition;
};

//Global Pointer to the tag structure table
//Array tag[maxlines][MAX_TAGLINE_BLOCK_NUMBER] created in tagline_driver_init
struct tagline **Globtag = NULL;

struct RAIDresponse
{
	uint8_t status; //status bit of response (only 1 bit)
	uint8_t type;
	uint8_t blockQuantity;
	uint8_t diskNumber;
	uint32_t id;
} GlobResponse;


//Functions Prototypes
RAIDOpCode create_raid_request(uint8_t, uint8_t, uint8_t, uint32_t);
int extract_raid_response(RAIDOpCode, RAIDOpCode);
int raid_disk_recover(uint8_t);
int chooseDisk(int*, int*, int);

// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : tagline_driver_init
// Description  : Initialize the driver with a number of maximum lines to process
//
// Inputs       : maxlines - the maximum number of tag lines in the system
// Outputs      : 0 if successful, 1 if failure

int tagline_driver_init(uint32_t maxlines) {

	RAIDOpCode operation = 0;
	RAIDOpCode response  = 0;

	int i, j, currentDisk;

	//Global variable to keep reference to maxlines
	maxtaglines = maxlines;

	//Initialize the cache
	init_raid_cache(TAGLINE_CACHE_SIZE);


	//Seed to the random number generator used in tagline_write()
	time_t t;
	srand((unsigned) time(&t));

	//Create table to keep track of tagline
	//Equivalent to:
	//tagline Globtag[maxlines][MAX_TAGLINE_BLOCK_NUMBER];
	//Freeing memory at tagline_close()
	Globtag = (struct tagline**) malloc(maxlines * sizeof(struct tagline*));
	if (Globtag == NULL)
		return (1);

	for (i = 0; i < maxlines; i++){
		Globtag[i] = (struct tagline*) malloc(MAX_TAGLINE_BLOCK_NUMBER * sizeof(struct tagline));
		if (Globtag[i] == NULL)
			return (1);
	}

	//Initialize disks and position to -1 (invalid block), 
	for (i = 0; i < maxlines; i++){
		for(j = 0; j < MAX_TAGLINE_BLOCK_NUMBER; j++){
			Globtag[i][j].disk = -1;
			Globtag[i][j].diskPosition = -1;
			Globtag[i][j].backupDisk = -1;
			Globtag[i][j].backupDiskPosition = -1;
		}
	}

	//Create an array to count the number of blocks of each tagline, freeing memory at tag_close()
	tagcounter = (int*) calloc(maxlines, sizeof(int));
	if (tagcounter == NULL)
		return (1);

	//RAID_INIT
	//Create OPcode for RAID_INIT
	operation = create_raid_request(RAID_INIT, RAID_DISKBLOCKS/RAID_TRACK_BLOCKS+3, RAID_DISKS, 0);

	//Initalize all disks
	response = client_raid_bus_request(operation, NULL);


	//extract raid response for RAID_INIT
	if (extract_raid_response(response, operation))
		return (1);

	//RAID_FORMAT
	//Format all disks
	for (currentDisk = 0; currentDisk < RAID_DISKS; currentDisk++){
		
		if (array[currentDisk].status == RAID_DISK_UNINITIALIZED){

			//Create OPcode for RAID_FORMAT
			operation = create_raid_request(RAID_FORMAT, 0, currentDisk, 0);

			//Format the disk
			response = client_raid_bus_request(operation, NULL);

			//extract the raid response for RAID_FORMAT
			if(extract_raid_response(response, operation))
				return(1);

			//Set Status to Ready
			array[currentDisk].status = RAID_DISK_READY;

			//All Blocks are initially unused, -1 is an invalid position meaning that there's nothing
			//in the current disk
			for(i = 0; i < RAID_DISKBLOCKS; i++)
				array[currentDisk].blocks = -1;

		}

	}

	// Return successfully
	logMessage(LOG_INFO_LEVEL, "TAGLINE: initialized storage (maxline=%u)", maxlines);
	return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : tagline_read
// Description  : Read a number of blocks from the tagline driver
//
// Inputs       : tag - the number of the tagline to read from
//                bnum - the starting block to read from
//                blks - the number of blocks to read
//                buf - memory block to read the blocks into
// Outputs      : 0 if successful, 1 if failure

int tagline_read(TagLineNumber tag, TagLineBlockNumber bnum, uint8_t blks, char *buf) {

	RAIDOpCode operation = 0;
	RAIDOpCode response = 0;

	int i;

	//Temporal buffer for the cache
	char *tempbuf;

	//Read all the blocks, 1 by 1, and keep adding data to the reading buffer
	for (i=0; i< blks; i++){

		//But first check if data is in the cache
		tempbuf = get_raid_cache(Globtag[tag][bnum+i].disk, Globtag[tag][bnum+i].diskPosition);

		if (tempbuf != NULL){
			memcpy(&buf[i*RAID_BLOCK_SIZE], tempbuf, RAID_BLOCK_SIZE);
		}
		else{
			operation = create_raid_request(RAID_READ, 1, Globtag[tag][bnum+i].disk, Globtag[tag][bnum+i].diskPosition);
			response = client_raid_bus_request(operation, &buf[TAGLINE_BLOCK_SIZE*i]);

			//Put the block into the cache
			put_raid_cache(Globtag[tag][bnum+i].disk, Globtag[tag][bnum+i].diskPosition, &buf[RAID_BLOCK_SIZE*i]);

		}
	}

	//Check Response:
	if(extract_raid_response(response, operation))
		return (1);


	//Return successfully
	logMessage(LOG_INFO_LEVEL, "TAGLINE : read %u blocks from tagline %u, starting block %u.",
			blks, tag, bnum);
	return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : tagline_write
// Description  : Write a number of blocks from the tagline driver
//
// Inputs       : tag - the number of the tagline to write from
//                bnum - the starting block to write from
//                blks - the number of blocks to write
//                buf - the place to write the blocks into
// Outputs      : 0 if successful, 1 if failure

int tagline_write(TagLineNumber tag, TagLineBlockNumber bnum, uint8_t blks, char *buf) {


	RAIDOpCode operation = 0;
	RAIDOpCode operation2 =0;
	RAIDOpCode response  = 0;
	RAIDOpCode response2  = 0;
	TagLineBlockNumber localBnum = bnum;
	int disk, backupDisk, i;

	//Blocks available before overlapping with another tagline
	int blocksAvailable = 1;
	int blocksAvailable2 = 1;
	//Rewriting tagline?
	bool rewritting;


	//Check if tagline is going to be overwritten
	if(bnum < tagcounter[tag])
		rewritting = true;
	else
		rewritting = false;

	//Choose two random disk number (from 0 to RAID_DISKS-1) to write to:
	disk = rand() % RAID_DISKS;
	backupDisk = rand() % RAID_DISKS;

	//Make sure the disks are not the same and are not full
	chooseDisk(&disk,&backupDisk,blks);

	//Check how many blocks are available before overlapping with another tag
	if (rewritting && blks != 1){
		for(i = 0; i<blks; i++){
			//If next block of the tagline is in the next position of the disk
			if(Globtag[tag][localBnum+1].disk == Globtag[tag][bnum].disk && Globtag[tag][localBnum+1].diskPosition == Globtag[tag][localBnum].diskPosition+1
			   && Globtag[tag][localBnum+1].disk != -1 && Globtag[tag][bnum+i].diskPosition == Globtag[tag][localBnum].diskPosition+i){
				blocksAvailable++;
				}
			if(Globtag[tag][localBnum+1].backupDisk == Globtag[tag][bnum].backupDisk 
			   && Globtag[tag][localBnum+1].backupDiskPosition == Globtag[tag][localBnum].backupDiskPosition+1
			   && Globtag[tag][localBnum+1].backupDisk != -1 && Globtag[tag][bnum+i].backupDiskPosition == Globtag[tag][localBnum].backupDiskPosition+i){
				blocksAvailable2++;
				}
			localBnum++;
		}
	}


	if(rewritting){
		//If there is enough space and we are rewriting:
		if (blocksAvailable >= blks){

			//Write to the cache
			for(i=0; i<blks; i++){
				put_raid_cache(Globtag[tag][bnum+i].disk, Globtag[tag][bnum+i].diskPosition, &buf[i*RAID_BLOCK_SIZE]);
			}


			operation = create_raid_request(RAID_WRITE, blks, Globtag[tag][bnum].disk, Globtag[tag][bnum].diskPosition);
			response = client_raid_bus_request(operation, buf);
			//No need to save information as we are just rewriting.

			//Check Response:
			if(extract_raid_response(response, operation))
				return(1);			
		}

		//Same thing for the backup disk:
		if (blocksAvailable2 >= blks){

			//Write to cache
			for(i=0; i<blks; i++){
				put_raid_cache(Globtag[tag][bnum+i].backupDisk, Globtag[tag][bnum+i].backupDiskPosition, &buf[i*RAID_BLOCK_SIZE]);
			}

			operation2 = create_raid_request(RAID_WRITE, blks, Globtag[tag][bnum].backupDisk, Globtag[tag][bnum].backupDiskPosition);
			response2 = client_raid_bus_request(operation2, buf);
			
			if(extract_raid_response(response2, operation2))
				return(1);

		}

		//Rewriting but not enough blocks before overlapping:
		if(blocksAvailable < blks){

			//Write to the Cache
			for(i=0; i<blocksAvailable; i++){
				put_raid_cache(Globtag[tag][bnum+i].disk, Globtag[tag][bnum+i].diskPosition, &buf[i*RAID_BLOCK_SIZE]);
			}

			//Write the blocks available
			operation = create_raid_request(RAID_WRITE, blocksAvailable, Globtag[tag][bnum].disk, Globtag[tag][bnum].diskPosition);
			response = client_raid_bus_request(operation, buf);
			//No need to save new information

			//Check Response
			if(extract_raid_response(response, operation))
				return(1);

			//Write the rest one by one
			localBnum = bnum + blocksAvailable;

			for(i=0; i < blks - blocksAvailable; i++){

				//Make sure that the backup disk and and the regular disk are not the same and that there is enough space
				while(Globtag[tag][localBnum].backupDisk == disk || Globtag[tag][bnum].backupDisk == disk){
					chooseDisk(&disk,NULL,1);
				}


				//If map has invalid position, it means that the block is new, write to the end of disk
				if (Globtag[tag][localBnum].diskPosition == -1){

					//Write to cache
					put_raid_cache(disk, array[disk].blocks+1, &buf[TAGLINE_BLOCK_SIZE*(blocksAvailable+i)]);


					operation = create_raid_request(RAID_WRITE, 1, disk, array[disk].blocks+1);
					response = client_raid_bus_request(operation, &buf[TAGLINE_BLOCK_SIZE*(blocksAvailable+i)]);

					//Check Response
					if(extract_raid_response(response, operation))
						return(1);
					
					//Save information of the current operation:
					Globtag[tag][localBnum].disk = disk;
					array[disk].blocks++;
					Globtag[tag][localBnum].diskPosition = array[disk].blocks;
					tagcounter[tag]++;

				}

				//If position is valid, is an old block: overwrite
				else{


					//Put in cache
					put_raid_cache(Globtag[tag][localBnum].disk, Globtag[tag][localBnum].diskPosition, &buf[TAGLINE_BLOCK_SIZE*(blocksAvailable+i)]);

					operation = create_raid_request(RAID_WRITE, 1, Globtag[tag][localBnum].disk, Globtag[tag][localBnum].diskPosition);
					response = client_raid_bus_request(operation, &buf[TAGLINE_BLOCK_SIZE*(blocksAvailable+i)]);
					//No need to save new information

					//Check Response
					if(extract_raid_response(response, operation))
						return(1);
				}

				//move in the map
				localBnum++;
			}
		}

		//Same thing for the backup disk:
		if(blocksAvailable2 < blks){

			//Write to the Cache
			for(i=0; i<blocksAvailable; i++){
				put_raid_cache(Globtag[tag][bnum+i].backupDisk, Globtag[tag][bnum+i].backupDiskPosition, &buf[i*RAID_BLOCK_SIZE]);
			}

			//Write the blocks available
			operation2 = create_raid_request(RAID_WRITE, blocksAvailable2, Globtag[tag][bnum].backupDisk, Globtag[tag][bnum].backupDiskPosition);
			response2 = client_raid_bus_request(operation2, buf);
			//No need to save new information

			//Check Response
			if(extract_raid_response(response2, operation2))
				return(1);

			//Write the rest one by one
			localBnum = bnum + blocksAvailable2;
			for(i=0; i < blks - blocksAvailable2; i++){

				//Make sure that the backup disk and and the regular disk are not the same
				while(Globtag[tag][localBnum].disk == backupDisk || Globtag[tag][bnum].disk == backupDisk){
					chooseDisk(NULL, &backupDisk, 1);
				}

				//if map has invalid position, it means that the block is new, write to the end of disk
				if (Globtag[tag][localBnum].backupDiskPosition == -1){

					//Write to cache
					put_raid_cache(backupDisk, array[backupDisk].blocks+1, &buf[TAGLINE_BLOCK_SIZE*(blocksAvailable+i)]);


					operation2 = create_raid_request(RAID_WRITE, 1, backupDisk, array[backupDisk].blocks+1);
					response2 = client_raid_bus_request(operation2, &buf[TAGLINE_BLOCK_SIZE*(blocksAvailable2+i)]);

					//Check Response
					if(extract_raid_response(response2, operation2))
						return(1);

					//Save information of the current operation:
					Globtag[tag][localBnum].backupDisk = backupDisk;
					array[backupDisk].blocks++;
					Globtag[tag][localBnum].backupDiskPosition = array[backupDisk].blocks;

				}
				//overwrite in the position of the disk
				else{

					//Put in cache
					put_raid_cache(Globtag[tag][localBnum].backupDisk, Globtag[tag][localBnum].backupDiskPosition, &buf[TAGLINE_BLOCK_SIZE*(blocksAvailable+i)]);

					operation2 = create_raid_request(RAID_WRITE, 1, Globtag[tag][localBnum].backupDisk, Globtag[tag][localBnum].backupDiskPosition);
					response2 = client_raid_bus_request(operation2, &buf[TAGLINE_BLOCK_SIZE*(blocksAvailable2+i)]);
					//No need to save new information

					//Check Response
					if(extract_raid_response(response2, operation2))
						return(1);
				}
				//move in the map
				localBnum++;
			}
		}
	}

	//If not rewriting blocks just continue to fill disk linearly
	if (!rewritting){

		//Write to cache 
		for (i=0; i < blks; i++){
			put_raid_cache(disk, array[disk].blocks+1+i, &buf[i*RAID_BLOCK_SIZE]);
		}

		operation = create_raid_request(RAID_WRITE, blks, disk, array[disk].blocks+1);
		response = client_raid_bus_request(operation, buf);

		//Check Response
		if(extract_raid_response(response, operation))
			return(1);

		//Same thing for backup disk

		//Write to cache 
		for (i=0; i < blks; i++){
			put_raid_cache(backupDisk, array[backupDisk].blocks+1+i, &buf[i*RAID_BLOCK_SIZE]);
		}

		operation2 = create_raid_request(RAID_WRITE, blks, backupDisk, array[backupDisk].blocks+1);
		response2 = client_raid_bus_request(operation2, buf);

		//Check Response
		if(extract_raid_response(response2, operation2))
			return(1);

		//Save information of current operation:
		tagcounter[tag] += blks;
		localBnum = bnum;

		for(i = 0; i < blks; i++){
			//In which disk the blocks where stored:
			Globtag[tag][localBnum].disk = disk;
			Globtag[tag][localBnum].backupDisk = backupDisk;
			
			//Which blocks of each disk were used.
			array[disk].blocks++;
			array[backupDisk].blocks++;

			//Map postion of each Tagline Block to a Disk Block
			Globtag[tag][localBnum].diskPosition = array[disk].blocks;
			Globtag[tag][localBnum].backupDiskPosition = array[backupDisk].blocks;

			localBnum++;
		}

	}

	

	logMessage(LOG_INFO_LEVEL, "TAGLINE : wrote %u blocks to tagline %u, starting block %u.",
			blks, tag, bnum);

	// Return successfully
	return(0);
}	

////////////////////////////////////////////////////////////////////////////////
//
// Function     : tagline_close
// Description  : Close the tagline interface
//
// Inputs       : none
// Outputs      : 0 if successful, 1 if failure

int tagline_close(void) {

	RAIDOpCode operation = 0;
	RAIDOpCode response = 0;
	int i = 0;


	//RAID_CLOSE
	//Generate opcode for RAID_CLOSE
	operation = create_raid_request(RAID_CLOSE,0,0,0);

	//Close
	response = client_raid_bus_request(operation, NULL);

	//Check Response:
	if(extract_raid_response(response, operation))
		return (1);

	//Clear the cache
	close_raid_cache();

	//Free Memory
	for (i =0; i < maxtaglines; i++)
		free(Globtag[i]);
	
	free(Globtag);
	Globtag = NULL;

	free(tagcounter);
	tagcounter = NULL;


	// Return successfully
	logMessage(LOG_INFO_LEVEL, "TAGLINE storage device: closing completed.");
	return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : raid_disk_signal
// Description  : goes through all the disk and finds which one fail
//
// Inputs       : none
// Outputs      : 0 if successful, 1 if failure

int raid_disk_signal(void){

	RAIDOpCode operation = 0;
	RAIDOpCode response = 0;
	uint8_t disk;

	//RAID_STATUS
	//Generate opcode for RAID_STATUS

	//Find out which disk Failed
	for (disk = 0; disk<RAID_DISKS; disk++){
		operation = create_raid_request(RAID_STATUS, 0, disk, 0);

		//Check Status
		response = client_raid_bus_request(operation, NULL);

		//Check Response:
		if(extract_raid_response(response, operation))
			return (1);

		//Now fix the disk that failed
		if (GlobResponse.id == RAID_DISK_FAILED){
			array[disk].status = RAID_DISK_FAILED;
			raid_disk_recover(disk);
		}

	}


	//return successfully	
	return (0);
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : raid_disk_recover
// Description  : recovers the disk that failed in the raid array, first reformats
//				  the disk, then finds all the lost blocks and copies them back.
//
// Inputs       : disk - the disk that failed in the raid array
//				  		       
// Outputs      : 0 if successful, 1 if failure

int raid_disk_recover(uint8_t disk){

	RAIDOpCode operation = 0;
	RAIDOpCode response = 0;
	int i= 0;
	int j = 0;
	char *tempbuf;
	char *tempbufcache;//Temporal buffer for the cache

	//Allocate space for one block
	tempbuf = (char*)calloc(1, TAGLINE_BLOCK_SIZE);

	//Reformat the disk
	operation = create_raid_request(RAID_FORMAT, 0, disk, 0);
	response = client_raid_bus_request(operation, NULL);

	//Check Response:
	if(extract_raid_response(response, operation))
		return (1);

	//Now scan for lost blocks
	for(i = 0; i < maxtaglines; i++){
		for(j = 0; j < tagcounter[i]; j++){

			//If the block was in the disk that failed:
			if(Globtag[i][j].disk == disk){

				//But first check if data is in the cache
				tempbufcache = get_raid_cache(Globtag[i][j].disk, Globtag[i][j].diskPosition);
				
				if (tempbufcache != NULL){
					memcpy(tempbuf, tempbufcache, RAID_BLOCK_SIZE);
				}
				else{
					//Read from backup
					operation = create_raid_request(RAID_READ, 1, Globtag[i][j].backupDisk, Globtag[i][j].backupDiskPosition);
					response = client_raid_bus_request(operation,tempbuf);

					//And put the block in the cache
					//Put the block into the cache
					put_raid_cache(Globtag[i][j].disk, Globtag[i][j].diskPosition, tempbuf);
				}


				//Check Response
				if(extract_raid_response(response, operation))
					return (1);

				//Now copy back to the disk
				operation = create_raid_request(RAID_WRITE, 1, Globtag[i][j].disk, Globtag[i][j].diskPosition);
				response = client_raid_bus_request(operation, tempbuf);

				//Check Response
				if(extract_raid_response(response, operation))
					return (1);

			}
			if (Globtag[i][j].backupDisk == disk){


				//Read from the other disk
				operation = create_raid_request(RAID_READ, 1, Globtag[i][j].disk, Globtag[i][j].diskPosition);
				response = client_raid_bus_request(operation,tempbuf);

				//Check Response
				if(extract_raid_response(response, operation))
					return (1);

				//Now copy back to the disk
				operation = create_raid_request(RAID_WRITE, 1, Globtag[i][j].backupDisk, Globtag[i][j].backupDiskPosition);
				response = client_raid_bus_request(operation, tempbuf);

				if(extract_raid_response(response, operation))
					return (1);
			}
		}
	}

	//Disk Recovered!
	array[disk].status = RAID_DISK_READY;

	//Free memory
	free(tempbuf);
	tempbuf = NULL;

	return (0);
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : chooseDisk
// Description  : changes the disks it receives by reference, making sure that 
//				  they are different and have enough blocks
// Inputs       : disk - the disk passed
//				  backupDisk - the second disk passed
//				  if any of the disk received is NULL, then just changes the one
//				  of the disk
//				  blks - the amount of blocks needed
// Outputs      : 0 if successful

int chooseDisk(int *disk, int *backupDisk, int blks){


	int oldDisk;
	int oldBDisk;

	if (disk != NULL)
		oldDisk = *disk;

	if (backupDisk != NULL)
		oldBDisk = *backupDisk;

	//Change disk
	if(backupDisk == NULL){
		while(*disk == oldDisk)
			*disk = rand() % RAID_DISKS;
	}

	//Change Backup
	if (disk == NULL){
		while(*backupDisk == oldBDisk)
			*backupDisk = rand() % RAID_DISKS;
	}

	//Changing Both
	if (disk != NULL && backupDisk != NULL){
		//Make sure disks are not full and are Different
		while (*disk == *backupDisk){	
			*disk = rand() % RAID_DISKS;
			*backupDisk = rand() % RAID_DISKS;
		}
	}

	//return succesfully
	return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : RAIDopCode
// Description  : generates an opcode for an operation with the raid array
//
// Inputs       : type - request type
//				  blockQuantity - number of blocks
//				  diskNumber - "name of the disk"
//				  id - block ID      
// Outputs      : RAIDopCode, which is the opcode for the actual operation

RAIDOpCode create_raid_request(uint8_t type, uint8_t blockQuantity, uint8_t diskNumber, uint32_t id) {
		
	//Initialize the op code
	RAIDOpCode op = 0;

	//Add request Type
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
	// |  0000000000... |    000000..   |  000...    |    type(8 bits)         
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
	op = type;

	//add the number of blocks to the op code
	//by shifting 8 bits and doing a "logical or operation"
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
	// |  000... |    000...  |    type(8bits)  | blockQuantity (8bits)     
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
	op = (op << 8) | blockQuantity;

	//add disk number
	//+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
	// |  000... |    type(8bits)  | blockQuantity (8bits) | diskNumber (8bits)  
		//+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
	op = (op << 8) | diskNumber;

	//bits from 24-30 are unused, set to 0. 31 is Status bit, for now set to 0.
	//+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
	// |  000... |    type(8bits)  | blockQuantity (8bits) | diskNumber (8bits)| 000..(8bits)  
		//+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
	op = (op << 8);

	//add block id
	//+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
	// | type(8bits) | blockQuantity (8bits) | diskNumber (8bits)| 000..(8bits) | id (32 bits)
		//+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
	op = (op << 32) | id;

    return(op); 
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : extract_raid_response
// Description  : Checks every field of the raid response, saves the fields on
//                Globresponse and compares to the original request returning
//				  failure if something changed.				  
// Inputs       : resp - response of the raid after applying and operation
//				  operation - the op code originally sent to the RAID  
// Outputs      : 1 if failure, 0 if success

int extract_raid_response(RAIDOpCode resp, RAIDOpCode operation) {

	//Response fields:

	//+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
	// | type(8bits) | blockQuantity (8bits) | diskNumber (8bits)| 000..(7bits) |status(1bit)| | id (32 bits)
	//+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-

	//Clear this field
	GlobResponse.id = 0;

	//temporal variables
	uint8_t temp = 0;
	uint32_t tempID = 0;


	//Get the ID:
	//shift 32 bits to the left to erase upper bits, then back the right.
	GlobResponse.id = (resp<<32) >> 32;
	tempID = (operation << 32) >> 32;

	
	//Check status bit:
	//Shift 31 bits to erase upper bits, then shift 63 back and check
	GlobResponse.status = (resp<<31)>>63;

	if(GlobResponse.status == 1)
		return (1);

	//Get Disk Number:
	// 0xFF0000000000u = 1111 1111 0000 0000 .... 0000 (40 bits of 0s)
	//Does an AND logical operation to copy bits, then shift to the right
	GlobResponse.diskNumber = (0xFF0000000000u & resp) >> 40;
	temp = (0xFF0000000000u & operation) >> 40;

	if(GlobResponse.diskNumber != temp)
		return (1);

	//Get block Quantity, shift 8 bits to the left to erase upper bits, then shift back
	GlobResponse.blockQuantity = (resp<<8)>>56;
	temp = (operation<<8)>>56;
	
	if (GlobResponse.blockQuantity != temp)
		return (1);
	
	//Get type:
	GlobResponse.type = (resp) >> 56;
	temp = (operation) >> 56;
	
	if(GlobResponse.type != temp)
		return (1);	

	if (GlobResponse.id != tempID && GlobResponse.type != RAID_STATUS)
		return (1);

	
	//return successfully
	return (0);
}