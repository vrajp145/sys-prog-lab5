#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {
    int bytesRead, totalBytesRead = 0; // bytesRead: Number of bytes read in a single read operation, totalBytesRead: Cumulative count of bytes read

    // Continuously read until the totalBytesRead matches the length required
    while (totalBytesRead < len) {
        // Read data from file descriptor into the buffer at the position indicated by totalBytesRead
        // The amount of data to be read is reduced by the totalBytesRead already achieved
        bytesRead = read(fd, buf + totalBytesRead, len - totalBytesRead);

        // Check if the read operation was successful
        if (bytesRead < 0) {
            return false; // Return false if an error occurred during read
        }
        // Update the total number of bytes read after a successful read operation
        totalBytesRead += bytesRead;
    }
    return true; // Return true once the requested number of bytes has been read successfully
}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
    int bytesWritten, totalWritten = 0;  // bytesWritten: stores number of bytes written in each write call, totalWritten: cumulative number of bytes written

    // Continue writing until all requested bytes are written
    while (totalWritten < len) {
        // Attempt to write the remaining data to the file descriptor
        bytesWritten = write(fd, buf + totalWritten, len - totalWritten);
        // Check if the write operation was successful
        if (bytesWritten < 0) {
            return false; // If an error occurred during writing, return false
        }
        // Update the total number of bytes written
        totalWritten += bytesWritten;
    }

    return true; // Return true once all data has been successfully written
}


/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opcode"  
ret - the address to store the return value of the server side calling the corresponding jbod_operation function.
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/
static bool recv_packet(int sd, uint32_t *op, uint16_t *ret, uint8_t *block) {
  // Define a buffer for the packet header, header_packet
  uint8_t header_packet[HEADER_LEN];

  // Read the header from the socket
  if (!nread(sd, HEADER_LEN, header_packet))
    return false;  // Return false if reading the header fails

  // Extract packet length, operation code, and return value from the header
  uint16_t len;
  memcpy(&len, header_packet, sizeof(uint16_t));        // Copy the packet length
  memcpy(op, header_packet + 2, sizeof(uint32_t));      // Copy the operation code
  memcpy(ret, header_packet + 6, sizeof(uint16_t));     // Copy the return value

  // Convert network byte order to host byte order
  *op = ntohl(*op);  // Convert operation code
  *ret = ntohs(*ret); // Convert return value
  len = ntohs(len);  // Convert packet length

  // Check if there's additional data beyond the header to read (e.g., a data block)
  if (len > HEADER_LEN) {
    // If additional data is present, read it into the provided 'block' buffer
    return nread(sd, JBOD_BLOCK_SIZE, block);
  }

  // If no additional data needs to be read, return true
  return true;
}


/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/

static bool send_packet(int sd, uint32_t op, uint8_t *block) {
    // Determine the packet length based on whether a data block needs to be included
    uint16_t len = HEADER_LEN + (block ? JBOD_BLOCK_SIZE : 0);

    // Convert total length and operation code to network byte order upfront
    uint16_t length_of_packet = htons(len);
    uint32_t op_of_packet = htonl(op);

    // Allocate memory for the packet
    uint8_t *packet = malloc(len);
    if (!packet) {
        return false; // Return false if memory allocation fails
    }

    // Pack the header: start with length and then operation code
    memcpy(packet, &length_of_packet, sizeof(uint16_t));
    memcpy(packet + 2, &op_of_packet, sizeof(uint32_t));

    // If a data block is to be included, append it after the header
    if (block) {
        memcpy(packet + HEADER_LEN, block, JBOD_BLOCK_SIZE);
    }

    // Send the packet and clean up
    bool success = nwrite(sd, len, packet);
    free(packet); // Always free the allocated memory after the packet is sent or in case of failure
    return success;
}

/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
    // Define the server address structure
    struct sockaddr_in caddr; 

    // Attempt to create a socket for IPv4 and TCP communication
    cli_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (cli_sd < 0) {
        return false; // Exit if the socket could not be created
    }

    // Initialize the address structure
    memset(&caddr, 0, sizeof(caddr)); // Zero out structure
    caddr.sin_family = AF_INET;       // Set address family to Internet (IPv4)
    caddr.sin_port = htons(port);     // Convert port number to network byte order

    // Convert IP address from text to binary form and set it
    if (inet_pton(AF_INET, ip, &caddr.sin_addr) <= 0) {
        close(cli_sd); // Ensure to close socket on failure
        return false;  // Exit if the IP address is invalid
    }

    // Establish a connection to the specified IP address and port
    if (connect(cli_sd, (struct sockaddr *)&caddr, sizeof(caddr)) < 0) {
        close(cli_sd); // Ensure to close socket on failure
        return false;  // Exit if connection cannot be established
    }

    // Connection successfully established
    return true;
}

/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
	close(cli_sd);
	cli_sd = -1;
}

/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block) {
// Send a packet to the server. Return -1 on failure.
if (!send_packet(cli_sd, op, block)) {
    return -1;
}

// Prepare variables temp_op and ret to receive the response
uint32_t temp_op;
uint16_t ret;

// Receive a response packet from the server. Return -1 on failure.
if (!recv_packet(cli_sd, &temp_op, &ret, block)) {
    return -1;
}

// Return the status code from the server response as an integer.
return (int)ret;
}