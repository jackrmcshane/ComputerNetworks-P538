/*
 *  Here is the starting point for your netster part.4 includes. Add the
 *  appropriate comment header as defined in the code formatting guidelines.
 */

#ifndef P4_H
#define P4_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>

/* add function prototypes */
void gbn_server(char*, long, FILE*);
void gbn_client(char*, long, FILE*);

#endif
