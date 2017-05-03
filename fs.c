// Kyle Williams
// Thomas Franceschi
// CSE 30341-01
// Professor Thain
// Project 6
// Due: 5/3/17

#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include <math.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

// globals
int *FREE_BLOCK_BITMAP = NULL;
int MOUNTED_FLAG = 0;

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

// creates a new filesystem on the disk, destroying any data already present
//  sets aside ten percent of the blocks for inodes, clears the inode table, and writes the superblock
//  returns one on success, zero otherwise
//  an attempt to format an already-mounted disk should do nothing and return zero
int fs_format()
{
	union fs_block block;

	// if disk is already mounted
	if(MOUNTED_FLAG == 1){
		printf("ERROR: filesystem already mounted\n");
		return 0;
	}

	// set super block data
	block.super.magic = FS_MAGIC;
	block.super.nblocks = disk_size();
	block.super.ninodeblocks = ceil(block.super.nblocks * (0.10));
	block.super.ninodes = block.super.ninodeblocks * INODES_PER_BLOCK;

	// destory any data already present on disk by making all valid inodes invalid
	char raw_data[4096] = {0};
	char *data = raw_data;
	int i;
	for(i = 1; i <= block.super.ninodeblocks; i++){
		// could read data from inode block and check for validity before writting, but that
		//  would mean more reads...
		disk_write(i, data);
	}
	
	disk_write(0, block.data);

	return 1;

}

// scan a mounted filesystem and report on how the inodes and blocks are organized
void fs_debug()
{
	union fs_block block;

	disk_read(0, block.data);

	printf("superblock:\n");

	// check for valid magic number
	if(block.super.magic == FS_MAGIC){
		printf("    magic number is valid\n");
	}
	else{
		printf("    magic number is invalid\n");
	}

	printf("    %d blocks on disk\n", block.super.nblocks);
	printf("    %d block(s) for inodes\n", block.super.ninodeblocks);
	printf("    %d inodes total\n", block.super.ninodes);

	// read inode data from each inode block, starting at block 1
	int i;
	for(i = 1; i <= block.super.ninodeblocks; i++){
		disk_read(i, block.data);

		// for each inode in the block with a valid bit...
		int j;
		for(j = 0; j < INODES_PER_BLOCK; j++){

			if(block.inode[j].isvalid){
				// print inode number and size
				int inumber = ((i-1) * 128) + (j+1);
				printf("inode %d:\n", inumber);
				printf("    size: %d bytes\n", block.inode[j].size);

				// print inode direct blocks if they are not NULL (0)
				printf("    direct blocks: ");
				int k;
				for(k = 0; k < POINTERS_PER_INODE; k++){
					if(block.inode[j].direct[k]){
						printf("%d ", block.inode[j].direct[k]);
					}
				}
				printf("\n");

				// if there is a non-zero indirect byte...
				if(block.inode[j].indirect){
					printf("    indirect block: %d\n", block.inode[j].indirect);

					// read the indirect block (array of ints) at the location given by the indirect integer
					disk_read(block.inode[j].indirect, block.data);

					// print the location of the indirect data blocks from the pointers array if non-zero
					printf("    indirect data blocks: ");
					int l;
					for(l = 0; l < POINTERS_PER_BLOCK; l++){
						if(block.pointers[l]){
							printf("%d ", block.pointers[l]);
						}
					}
					printf("\n");
				}
				// read the inode block data again before the next iteration
				disk_read(i, block.data);
			}

		}
	}

}

// examine the disk for a filesystem
//  if one is present, read the superblock, build a free block bitmap, and prepare the filesystem for use
//  return one on success, zero otherwise
int fs_mount()
{
	union fs_block block;

	// check for magic number in super block
	disk_read(0, block.data);

	int magic = block.super.magic;
	int nblocks = block.super.nblocks;
	int ninodeblocks = block.super.ninodeblocks;

	if(block.super.magic != FS_MAGIC){
		printf("ERROR: invalid magic number on super block: %x", magic);
		return 0;
	}

	// build free block bit map
	FREE_BLOCK_BITMAP = (int*) malloc(sizeof(int) * nblocks);

	// initialize to zeros
	int j;
	for(j = 0; j < nblocks; j++){
		FREE_BLOCK_BITMAP[j] = 0;
	}

	// set super block to 1
	FREE_BLOCK_BITMAP[0] = 1;

	// read inode blocks
	int i;
	for(i = 1; i <= ninodeblocks; i++){
		disk_read(i, block.data);

		int j;
		for(j = 0; j < INODES_PER_BLOCK; j++){
			// identify inode block in bitmap
			if(block.inode[j].isvalid){
				FREE_BLOCK_BITMAP[i] = 1;

				// identify direct data blocks in bitmap
				int k;
				for(k = 0; k < POINTERS_PER_INODE; k++){
					int direct_block = block.inode[j].direct[k];
					if(direct_block != 0){
						FREE_BLOCK_BITMAP[direct_block] = 1;
					}
				}

				// if there is an indirect section, identify the corresponding data blocks
				int indirect = block.inode[j].indirect;
				if(indirect != 0){
					FREE_BLOCK_BITMAP[indirect] = 1;
					disk_read(indirect, block.data);
						int l;
						for(l = 0; l < POINTERS_PER_BLOCK; l++){
							int block_ptr = block.pointers[l];
							if(block_ptr != 0){
								FREE_BLOCK_BITMAP[block_ptr] = 1;
							}
						}
				}
				// read the inode block data again before the next iteration
				disk_read(i, block.data);
			}
		}
	}

	MOUNTED_FLAG = 1;
	return 1;
}

// create a new inode of zero length, on success: return the (positive) inumber, on failure: return zero
int fs_create()
{

	//if no fs mounted, fail
	if(MOUNTED_FLAG != 1){
		printf("ERROR: no filesystem mounted\n");
		 return 0;
	}

	union fs_block block;

	int inumber;
	// iterate through inode blocks to find open space for inode
	disk_read(0, block.data);
	int i;
	for(i = 1; i <= block.super.ninodeblocks; i++){
		disk_read(i, block.data);

		int j;
		for(j = 0; j < INODES_PER_BLOCK; j++){
			if(block.inode[j].isvalid == 0){
				// calculate inumber
				inumber = ((i-1) * 128) + (j+1);

				// at the first open (invalid) inode, set isvalid to 1 and size to 0, and write back
				block.inode[j].isvalid = 1;
				block.inode[j].size = 0;
				int k;
				for(k = 0; k < POINTERS_PER_INODE; k++){
					block.inode[j].direct[k] = 0;
				}
				block.inode[j].indirect = 0;
				disk_write(i, block.data);

				// set the index of this new inode in the bit map to 1
				FREE_BLOCK_BITMAP[i] = 1;

				return inumber;
			}
		}
	}

	// return the error if there are no more inodes
	printf("ERROR: inode table full\n");
	return 0;

}

//Delete the inode indicated by the inumber. Release all data and 
//indirect blocks assigned to this inode and return them to the free 
//block map. On success, return one. On failure, return 0.
int fs_delete( int inumber )
{

	//if no fs mounted, fail
	if(MOUNTED_FLAG != 1){
		printf("ERROR: no filesystem mounted\n");
		 return 0;
	}

	union fs_block block;
	union fs_block superblk;

	disk_read(0, superblk.data);
	//check inode number is in valid range
	if (inumber > superblk.super.ninodes){
		printf("ERROR: Inode out of range\n");
		return 0;
	}
	
	int blocknum = 1; //block num
	int inodenum = 1; //index num
	
	//generate index numbers
	while(inumber > INODES_PER_BLOCK){
		blocknum++;
		inumber -= INODES_PER_BLOCK;
	}
	inodenum = inumber - 1;

	disk_read(blocknum, block.data);
	if(block.inode[inodenum].isvalid == 0){
		printf("ERROR: Invalid inode\n");
		return 0;
	}
	block.inode[inodenum].isvalid = 0;
	block.inode[inodenum].size = 0;
	disk_write(blocknum, block.data);
	
	int i = 1;
	//check if inode block is now empty
	FREE_BLOCK_BITMAP[blocknum] = 0;
	for( i = 1; i <= INODES_PER_BLOCK; i++){	
		if(block.inode[i].isvalid){
			FREE_BLOCK_BITMAP[blocknum] = 1;
		}
	}

	//delete all direct pointers
	int j;
	for(j = 0; j < POINTERS_PER_INODE; j++){
		if (block.inode[inodenum].direct[j] != 0){
			FREE_BLOCK_BITMAP[block.inode[inodenum].direct[j]] = 0;  //Set bitmap to 0
			block.inode[inodenum].direct[j] = 0; 	//Remove pointer
		}
	}
	disk_write(blocknum, block.data);

	//delete inderect pointers
	// if there is an indirect section, identify the corresponding data blocks
	int indirect = block.inode[inodenum].indirect;
	if(indirect != 0){
		FREE_BLOCK_BITMAP[indirect] = 0; 			//remove form map[]
		disk_read(indirect, block.data);
		int l;
		for(l = 0; l < POINTERS_PER_BLOCK; l++){
			int block_ptr = block.pointers[l];
			if(block_ptr != 0){
				FREE_BLOCK_BITMAP[block_ptr] = 0; 	//remove all ptrs from map
			}
			block_ptr = 0;
		}
		disk_write(indirect, block.data);

		disk_read(blocknum, block.data);
		block.inode[inodenum].indirect = 0; 		//remove indirect pointer
		disk_write(blocknum, block.data);
	}

	return 1;
}

// return the logical size of the given inode, in bytes
int fs_getsize( int inumber )
{
	//if no fs mounted, fail
	if(MOUNTED_FLAG != 1){
		printf("ERROR: no filesystem mounted\n");
		 return -1;
	}

	union fs_block superblk;

	disk_read(0, superblk.data);
	//check inode number is in valid range
	if (inumber > superblk.super.ninodes){
		printf("ERROR: Inode out of range\n");
		return 0;
	}

	union fs_block block;
	
	int blocknum = 1; // block num
	
	// generate index numbers
	while(inumber > INODES_PER_BLOCK){
		blocknum++;
		inumber -= INODES_PER_BLOCK;
	}

	disk_read(blocknum, block.data);

	return block.inode[inumber-1].size;

	// TODO Error checking
	// return -1;
}

// read data from a valid inode, copy "length" bytes from the inode into the "data" pointer, starting at "offset" in the inode
//  return the total number of bytes read, the number of bytes actually read could be smaller than the number of bytes requested, 
//  perhaps if the end of the inode is reached, if the given inumber is invalid, or any other error is encountered, return 0
int fs_read( int inumber, char *data, int length, int offset )
{
	//if no fs mounted, fail
	if(MOUNTED_FLAG != 1){
		printf("ERROR: no filesystem mounted\n");
		 return 0;
	}

	union fs_block superblk;
	disk_read(0, superblk.data);
	//check inode number is in valid range
	if (inumber > superblk.super.ninodes){
		printf("ERROR: Inode out of range\n");
		return 0;
	}

	union fs_block block;
	
	int blocknum = 1; // block num
	int inodenum = 1; //index num
	
	// generate index numbers
	while(inumber > INODES_PER_BLOCK){
		blocknum++;
		inumber -= INODES_PER_BLOCK;
	}

	inodenum = inumber - 1;

	// read data from blcok number containing given inumber
	disk_read(blocknum, block.data);

	// if inode is invalid, return 0
	if(!block.inode[inodenum].isvalid){
		printf("ERROR: invalid inode\n");
		return 0;
	}

	// get data from given inode, if size is 0, return 0
	int size = block.inode[inodenum].size;
	if(size == 0) return 0;

	int numblocks = ceil(size/DISK_BLOCK_SIZE) + 1;

	// build an arrray containing the indices corresponding to the data associated with this inode
	int blocknums[numblocks];

	// add the direct block indices to this array
	int i;
	int directblocks = 0;
	for(i = 0; i < POINTERS_PER_INODE; i++){
		if(block.inode[inodenum].direct[i]){
			blocknums[i] = block.inode[inodenum].direct[i];
			directblocks++;
		}
	}

	// add the indirect block indices to this array
	int indirect = block.inode[inodenum].indirect;
	if (indirect != 0){
		disk_read(indirect, block.data);

		int j;
		for(j = 0; j < POINTERS_PER_BLOCK; j++){
			if(block.pointers[j]){
				blocknums[j + directblocks] = block.pointers[j];
			}
		}
	}

	int index_to_start = offset / DISK_BLOCK_SIZE;
	int blocks_per_length = length / DISK_BLOCK_SIZE;
	int blocks_to_read = index_to_start + blocks_per_length;

	if(blocks_to_read > numblocks){
		blocks_to_read = numblocks;
	}

	int bytes_read = 0;
	int bytes_left = size;
	int blocksize = DISK_BLOCK_SIZE;

	int k;
	int l;
	for(k = index_to_start; k < blocks_to_read; k++){
		// see how many bytes are left to be read, if less than the whole block,
		//  set the block size to this number that's less than 4096
		bytes_left = size - bytes_read - offset;
		if(bytes_left < DISK_BLOCK_SIZE){
			blocksize = bytes_left;
		}

		// read each data block in the inode data block array
		disk_read(blocknums[k], block.data);
		for(l = 0; l < blocksize; l++){
			data[bytes_read] = block.data[l];
			bytes_read++;
		}
	}

	return bytes_read;

}

// write data to a valid inode, copy "length" bytes from the pointer "data" into the inode starting at "offset" bytes allocate
//  any necessary direct and indirect blocks in the process, return the number of bytes actually written, the number of bytes
//  actually written could be smaller than the number of bytes request, perhaps if the disk becomes full
//  If the given inumber is invalid, or any other error is encountered, return 0
int fs_write( int inumber, const char *data, int length, int offset )
{
	//if no fs mounted, fail
	if(MOUNTED_FLAG != 1){
		printf("ERROR: no filesystem mounted\n");
		 return 0;
	}

	union fs_block block;
	union fs_block indirectblock;
	union fs_block superblk;

	disk_read(0, superblk.data);
	//check inode number is in valid range
	if (inumber > superblk.super.ninodes){
		printf("ERROR: Inode our of range\n");
		return 0;
	}

	int blocknum = 1; //block num
	int inodenum = 1; //index num
	
	//generate index numbers
	while(inumber > INODES_PER_BLOCK){
		blocknum++;
		inumber -= INODES_PER_BLOCK;
	}
	inodenum = inumber - 1;

	int first_ptr = getLocation(offset);
	
	disk_read( blocknum, block.data);

	//make sure inumber is valid
	if(!block.inode[inodenum].isvalid){
		printf("ERROR: Invalid inode\n");
		return 0;
	}

	//calculate number of blocks needed + remainder size
	int rem = length;
	int numblocks = 1;
	while( rem > DISK_BLOCK_SIZE ){
		rem -= DISK_BLOCK_SIZE;
		numblocks++;
	}
	printf("numblocks: %d\n", numblocks);

	int left_to_write = length;
	int written = 0;
	int curr_ptr = 0;


	//while file still has data to write
	while(left_to_write > 0){
		int size_to_write = 4096;
		int dest_block;

		

		//get current pointer location in inode
		curr_ptr = getLocation( offset + written );

		//direct or inderect pointer?
		if(curr_ptr < 5){ 											//direct
			//find destination data block to write to
			if((dest_block = findBlock())){
				//mark block on bitmap as used
				FREE_BLOCK_BITMAP[dest_block] = 1;
			}
			else {
				printf("ERROR: File too large\n");
				return 0;
			}
			block.inode[inodenum].direct[curr_ptr] = dest_block;
			disk_write( blocknum, block.data);
		}
		else{  														//indirect
			int indirect = block.inode[inodenum].indirect;
			if( indirect == 0 ){
				if((indirect = findBlock())){
					//mark block on bitmap as used
					FREE_BLOCK_BITMAP[indirect] = 1;
				}
				else {
					printf("ERROR: File too large\n");
					return 0;
				}
				block.inode[inodenum].indirect = indirect;
				disk_write(blocknum, block.data);			
			}
			//find destination data block to write to
			if((dest_block = findBlock())){
				//mark block on bitmap as used
				FREE_BLOCK_BITMAP[dest_block] = 1;
			}
			else {
				printf("ERROR: File too large\n");
				return 0;
			}

			disk_read( block.inode[inodenum].indirect, indirectblock.data);
			indirectblock.pointers[ curr_ptr - 6 ] = dest_block;
			
			int r;
			for( r = (numblocks - 5) + first_ptr; r < 1024; r++){
				indirectblock.pointers[r] = 0;
			}
			disk_write( block.inode[inodenum].indirect, indirectblock.data);
			
		}


		//if last block and has uneven write size, adjust write size
		if(left_to_write < 4096){
			size_to_write = left_to_write;
		}

		//write to data block
		int i;
		char chunk[4096];
		for( i = 0; i < size_to_write; i++){
			chunk[i] = data[ i + written ];
		}

		if(size_to_write == 4096){
			disk_write(dest_block, chunk);
		}
		else{
			char remchunk[rem];
			int j;
			for( j = 0; j < rem; j++){
				remchunk[j] = chunk[j];
			}
			disk_write(dest_block, remchunk);
		}

		//track how much data is left
		left_to_write -= size_to_write;
		written += size_to_write;
		
	}
	block.inode[inodenum].size += written;
	disk_write( blocknum, block.data);
	return written;
}

int findBlock(){

	//load super block
	union fs_block block;
	disk_read(0, block.data);

	int n_inodes = block.super.ninodeblocks;
	int n_blocks = block.super.nblocks;
	int i;
	
	//Linearly probe data blocks for open block
	for(i = n_inodes + 1; i < n_blocks; i++){
		if(FREE_BLOCK_BITMAP[i] == 0) return i;
	}

	//ERROR
	return 0;
}

int getLocation( int offset ){

	int location = offset / DISK_BLOCK_SIZE;
	return location;

}
