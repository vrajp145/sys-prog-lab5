/* Author: Vraj Patel
   Date: 4/14/24
    */
    
    
    
/***
 *      ______ .___  ___. .______     _______.  ______              ____    __   __  
 *     /      ||   \/   | |   _  \   /       | /      |            |___ \  /_ | /_ | 
 *    |  ,----'|  \  /  | |  |_)  | |   (----`|  ,----'              __) |  | |  | | 
 *    |  |     |  |\/|  | |   ___/   \   \    |  |                  |__ <   | |  | | 
 *    |  `----.|  |  |  | |  |   .----)   |   |  `----.             ___) |  | |  | | 
 *     \______||__|  |__| | _|   |_______/     \______|            |____/   |_|  |_| 
 *                                                                                   
 */


#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "cache.h"
#include "mdadm.h"
#include "util.h"
#include "jbod.h"
#include "net.h"

int check_mount = 0;

uint32_t use_addr(int commandVal, int diskVal, int blockVal) {
  return (commandVal << 14) | (diskVal << 28) | (blockVal << 20);
}

uint8_t *block = NULL;

//find minimum between two numbers; used for cache implementation in mdadm.c
int min(int num1, int num2){
	return (num1 > num2) ? num2 : num1;
}

int mdadm_mount(void) {
  uint32_t op = use_addr(JBOD_MOUNT, 0, 0);
   if (jbod_client_operation(op, NULL) == 0){
     check_mount = 1;
     return 1;
   }
   return -1;
}

int mdadm_unmount(void) {
  uint32_t op = use_addr(JBOD_UNMOUNT, 0, 0);
   if (jbod_client_operation(op, NULL) == 0){
     check_mount = 0;
     return 1;
   }
   return -1;
}



int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
  /* YOUR CODE */
  if((check_mount == 0)||(len + addr > 1048576)|| (len > 1024) || (len > 0 && buf == NULL)){
    return -1;
    }
  int finish = len + addr; // Calculate final address

  //cache implementation 

  int final_read = 0;

 // Loop through each block needed for the read operation
  for(int addr_copy = addr; addr_copy < finish;){

    // Calculate the disk and block numbers based on the current address
    int dB = addr_copy / 65536;
    int new_num = (addr_copy % 65536) / 256;
    int new_value = addr_copy % 256; //offset
    int remain = addr_copy % 256;
    int rem_bytes = 256 - remain;

    // Calculate the destination address in the buffer and the amount to copy
    uint8_t *address_value = buf + (addr_copy - addr);
    uint8_t remainder = finish - addr_copy;
    
    uint8_t temporaryBuf[256];

    //int index_for_cache = -1;
    // If cache is active, attempt to locate the specified block within it.
    if (cache_enabled() == true) {
      if (cache_lookup(dB, new_num, temporaryBuf) == -1){
        // Seek to the correct disk
        uint32_t op1 = use_addr(JBOD_SEEK_TO_DISK, dB, 0);
        jbod_client_operation(op1, block);
        
        // Seek to the correct block within the disk
        uint32_t op2 = use_addr(JBOD_SEEK_TO_BLOCK,0, new_num);
        jbod_client_operation(op2, block);
        
        // Read the current block into the temporary buffer
        uint32_t op3 = use_addr(JBOD_READ_BLOCK, 0, 0);
        jbod_client_operation(op3, temporaryBuf);
        }
      else {
        final_read += min(remainder, rem_bytes);
        addr_copy += min(remainder, rem_bytes);
      }
    }

    else{
      // Seek to the correct disk
      uint32_t op1 = use_addr(JBOD_SEEK_TO_DISK, dB, 0);
      jbod_client_operation(op1, block);
      
      // Seek to the correct block within the disk
      uint32_t op2 = use_addr(JBOD_SEEK_TO_BLOCK,0, new_num);
      jbod_client_operation(op2, block);
      
      // Read the current block into the temporary buffer
      uint32_t op3 = use_addr(JBOD_READ_BLOCK, 0, 0);
      jbod_client_operation(op3, temporaryBuf);
    }
 
    // Determine how many bytes to copy: either the remainder or up to 256 bytes
    int  holder_value = (finish - addr_copy < 256) ? remainder : 256;
    memcpy(address_value, temporaryBuf, holder_value);

     // Move to the next block, adjusting for any initial offset within the block
    addr_copy = (addr_copy - new_value) + 256;
    }

  return len; // Return the total number of bytes intended to read

  }


int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
  if((check_mount == 0)||(len + addr > 1048576)|| (len > 1024) || (len > 0 && buf == NULL)){ //this  checks if the mount status and inputs are valid
    return -1; //returns -1 if not mounted, not in the correct bytes range, or the buf is NULL
    }

  // Initialize a variable to keep track of the current position in the buffer
  int new_var = 0;

  int finish = len + addr; //storing end address

  // Loop through each block of 256 bytes
  int addr_copy = addr; // Initialize addr_copy with addr, moved outside the loop
  //int final_read = 0;
  while(addr_copy < finish){
      int dB = addr_copy / 65536; // Calculate the disk number for the current block
      int num_for_block = (addr_copy % 65536) / 256; // Calculate the block number for the current block
      int new_value = addr_copy % 256; // Calculate the offset within the block
      //int remain = addr_copy % 256;
      //int rem_bytes = 256 - remain;

      //uint8_t *address_value = buf + (addr_copy - addr);
      //uint8_t remainder = finish - addr_copy;
      
      // Create a temporary buffer of size 256 bytes
      uint8_t temporaryBuf[256];

      if (cache_enabled() == true) {
        if (cache_lookup(dB, num_for_block, temporaryBuf) == -1){
          // Seek to the correct disk
          uint32_t op1 = use_addr(JBOD_SEEK_TO_DISK, dB, 0);
          jbod_client_operation(op1, block);
          
          // Seek to the correct block within the disk
          uint32_t op2 = use_addr(JBOD_SEEK_TO_BLOCK,0, num_for_block);
          jbod_client_operation(op2, block);
          
          // Read the current block into the temporary buffer
          uint32_t op3 = use_addr(JBOD_READ_BLOCK, 0, 0);
          jbod_client_operation(op3, temporaryBuf);
          }
      }
      else {
        // Seek to the correct disk
        uint32_t op1 = use_addr(JBOD_SEEK_TO_DISK, dB, 0);
        jbod_client_operation(op1, block);
        
        // Seek to the correct block within the disk
        uint32_t op2 = use_addr(JBOD_SEEK_TO_BLOCK,0, num_for_block);
        jbod_client_operation(op2, block);
        
        // Read the current block into the temporary buffer
        uint32_t op3 = use_addr(JBOD_READ_BLOCK, 0, 0);
        jbod_client_operation(op3, temporaryBuf);
    }

      int remaining_distance_to_process = finish - addr_copy; // Calculate remaining distance to process

      // Determine the amount of data to process, ensuring it's a positive step forward
      int temp_distance_hold = (remaining_distance_to_process < (256 - new_value)) ? remaining_distance_to_process : (256 - new_value);
      // temp_distance_hold = temp_distance_hold > 0 ? temp_distance_hold : 1; // Ensure at least 1 byte is processed to avoid infinite loop

      // Read the data from the temporary buffer into the actual buffer
      memcpy(temporaryBuf + new_value, buf + new_var, temp_distance_hold);


      // Write the modified block back
      uint32_t op4 = use_addr(JBOD_SEEK_TO_DISK, dB, 0); 
      jbod_client_operation(op4, block);
      uint32_t op5 = use_addr(JBOD_SEEK_TO_BLOCK, 0, num_for_block);
      jbod_client_operation(op5, block);
      uint32_t op6 = use_addr(JBOD_WRITE_BLOCK, 0, 0);
      jbod_client_operation(op6, temporaryBuf);


      ///Check if caching is enabled before proceeding with cache operations.
      if (cache_enabled() == true) {
        // Attempt to insert a new block into the cache; update the block if it already exists.
        if (cache_insert(dB, num_for_block, temporaryBuf) == -1){
          cache_update(dB, num_for_block, temporaryBuf); // Update existing entry with new data.
        }
      }

      // Update addr_copy and new_var to progress through the data
      addr_copy += temp_distance_hold;
      new_var += temp_distance_hold;
  }

  return len; // Return the length of the data written
}
