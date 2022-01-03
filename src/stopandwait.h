/*
 *  Here is the starting point for your netster part.3 includes. Add the
 *  appropriate comment header as defined in the code formatting guidelines.
 */

#ifndef P3_H
#define P3_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>

/* add function prototypes */
void stopandwait_server(char*, long, FILE*);
void stopandwait_client(char*, long, FILE*);

#endif
