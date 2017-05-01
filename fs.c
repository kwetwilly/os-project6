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
	if(FREE_BLOCK_BITMAP != NULL){
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

	// TODO: ERROR Checking, return 0, how can this fail?

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

	int x;
	for(x = 0; x < nblocks; x++){
		printf("block number %d: %d\n", x, FREE_BLOCK_BITMAP[x]);
	}

	return 1;
}

// create a new inode of zero length, on success: return the (positive) inumber, on failure: return zero
int fs_create()
{
	union fs_block block;

	int nblocks;

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

				printf("%d\n", inumber);

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

				// to break out of both loops
				disk_read(0, block.data);

				nblocks = block.super.nblocks;

				i = block.super.ninodeblocks + 1;
				break;
			}
		}
	}

	int x;
	for(x = 0; x < nblocks; x++){
		printf("block number %d: %d\n", x, FREE_BLOCK_BITMAP[x]);
	}

	// return the inumber
	return inumber;

	// TODO : ERROR checking, returning 0
	// return 0;
}

int fs_delete( int inumber )
{
	return 0;
}

int fs_getsize( int inumber )
{
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
