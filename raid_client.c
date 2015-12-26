////////////////////////////////////////////////////////////////////////////////
//
//  File          : raid_client.c
//  Description   : This is the client side of the RAID communication protocol.
//
//  Author        : Leonardo de la Cruz
//  Last Modified : 12/09/2015
//

// Include Files
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>

// Project Include Files
#include <raid_network.h>
#include <cmpsc311_log.h>
#include <cmpsc311_util.h>

// Global data
unsigned char *raid_network_address = NULL; // Address of CRUD server
unsigned short raid_network_port = 0; // Port of CRUD server
int sockfd = -1;

//Global struct
struct network
{
	uint64_t opcode;
	uint64_t length;
} remoteRaid;


// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : client_raid_bus_request
// Description  : This the client operation that sends a request to the RAID
//                server.   It will:
//
//                1) if INIT make a connection to the server
//                2) send any request to the server, returning results
//                3) if CLOSE, will close the connection
//
// Inputs       : op - the request opcode for the command
//                buf - the block to be read/written from (READ/WRITE)
// Outputs      : the response structure encoded as needed, -1 if failure

RAIDOpCode client_raid_bus_request(RAIDOpCode op, void *buf) {

	
	struct sockaddr_in caddr;//structure for network
	uint8_t type; //type of request
	uint64_t length; //lenth of data to be sent
	uint64_t blocks;//number of blocks to be written or read
	RAIDOpCode response = 0; //the response opcode from the server
	remoteRaid.length = 0; //length of data to be sent
	

	//Get the type of request:
	type = (op>>56);

	//Get the length of data (only needed for reads and writes)
	blocks = (op<<8)>>56;
	length = blocks*RAID_BLOCK_SIZE;

	//Change opcode to network byte order
	remoteRaid.opcode = htonll64(op);

	
	//Change length of data to network byte order
	if(type == RAID_READ || type == RAID_WRITE){
		remoteRaid.length = htonll64(length);
	}

	
	//Make a connection to the server
	if (type == RAID_INIT){

		//Setup the address and port in proper form
		caddr.sin_family = AF_INET;
		caddr.sin_port = htons(RAID_DEFAULT_PORT);

		if(inet_aton(RAID_DEFAULT_IP, &caddr.sin_addr) == 0 ){
    		return(-1);
    	}

    	//Create the socket
    	sockfd = socket(AF_INET, SOCK_STREAM, 0);
    	if (sockfd == -1)
    		return (-1);

    	//Now Connect
    	if(connect(sockfd, (const struct sockaddr *)&caddr, sizeof(caddr)) == -1){
    		return(-1);
    	}

	}

	////////////////////
    //Write to server//
    //////////////////

    //Send opcode (64 bits = 8 bytes)
    write(sockfd, &remoteRaid.opcode, 8);

    //Send length (64 bits == 8 bytes)
    write(sockfd, &remoteRaid.length, 8);

	//Send buffer
	if(type == RAID_READ || type == RAID_WRITE){
    	write(sockfd, buf, RAID_BLOCK_SIZE*blocks);
	}

    /////////////////////////
    //Read from the server//
	///////////////////////

    //read response
    read(sockfd, &response, 8);

    //Read length
    read(sockfd, &remoteRaid.length, 8);

    //Read buffer
    if(type == RAID_READ || type == RAID_WRITE){
    	read(sockfd, buf, RAID_BLOCK_SIZE*blocks);
    }


	//Turn response back to client byte order
	response = ntohll64(response);


	///////////////////////////
	//Close server connection/
	/////////////////////////

	if (type == RAID_CLOSE){
		close(sockfd);
		sockfd = -1;
	}

	return (response);
}
