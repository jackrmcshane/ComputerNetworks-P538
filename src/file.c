#include "file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
/*
 *  FILE.C - Jack McShane (jamcshan)
 *  CREATED: 10/29/2021
 *
 *  file.c implements file transfer functionality across a given network. 
 *  Both TCP and UDP are supported for file transferrence.
 */

#define MAXSIZE 256
#define BACKLOG 10

void write_out(int, char *, ssize_t);
void tcp_file_server(char*, long, FILE*);
void udp_file_server(char*, long, FILE*);
void tcp_file_client(char*, long, FILE*);
void udp_file_client(char*, long, FILE*);
void udp_send(int, struct sockaddr_in*, char*, ssize_t);
void initialize_sockaddr(struct sockaddr_in *, char *, long);


/* Add function definitions */
void file_server(char* iface, long port, int use_udp, FILE* fp) {

  if(use_udp == 1){
    udp_file_server(iface, port, fp);
  } else {
    tcp_file_server(iface, port, fp);
  }
}

void file_client(char* host, long port, int use_udp, FILE* fp) {

  if(use_udp == 1){
    udp_file_client(host, port, fp);
  } else {
    tcp_file_client(host, port, fp);
  }
}


/*

tcp_file_server:
Provides the functionality of a file transfer TCP server.
Data received is written to an output file on disk.

*/
void tcp_file_server(char* iface, long port, FILE* fp){

  int listener, connection, nread, nwritten, optval = 1;
  char client_message[MAXSIZE];
  struct sockaddr_in server, client;
  socklen_t sockaddr_size;

  initialize_sockaddr(&server, iface, port);

  // initialize the socket the will listen for new connections
  if((listener = socket(AF_INET, SOCK_STREAM, 0)) < 0){

    perror("socket creation");
    exit(EXIT_FAILURE);

  }


  // bind listener to the server address
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  if(bind(listener, (struct sockaddr *)&server, sizeof(server)) < 0){

    perror("bind");
    exit(EXIT_FAILURE);

  }


  // listen for incoming connections on listener
  if(listen(listener, BACKLOG) < 0){

    perror("listen");
    exit(EXIT_FAILURE);

  }


  sockaddr_size = sizeof(struct sockaddr_in);

  // accept the incoming connection
  connection = accept(listener, (struct sockaddr *)&client, &sockaddr_size);
  if(connection < 0){

    perror("connect failed");
    exit(EXIT_FAILURE);

  }

  // loop while received data is not 0 (or error)
  while((nread = recv(connection, client_message, MAXSIZE, 0)) > 0){

    nwritten = fwrite(client_message, 1, nread, fp);
    if(nwritten != nread){

      perror("writing to file");
      exit(EXIT_FAILURE);

    }

    // clear the client_message buffer
    bzero(client_message, MAXSIZE);
  }

  close(connection);
  close(listener);

}


/*

udp_file_server:
Provides the functionality of a file transfer UDP server.
Data received is written to an output file on the system.

*/
void udp_file_server(char* iface, long port, FILE* fp){

  int receiver;
  ssize_t nwritten, nread;
  char client_message[MAXSIZE];
  struct sockaddr_in server, client;
  socklen_t sockaddr_size;


  sockaddr_size = sizeof(struct sockaddr_in);
  initialize_sockaddr(&server, iface, port);

  // initialize server socket that accepts data from client
  if((receiver = socket(AF_INET, SOCK_DGRAM, 0)) < 0){

    perror("socket creation");
    exit(EXIT_FAILURE);
    
  }

  // bind receiver socket to the server's address
  if(bind(receiver, (struct sockaddr *)&server, sizeof(server)) < 0){

    perror("bind");
    exit(EXIT_FAILURE);

  }


  // loop while still receiving data from client
  while((nread = recvfrom(receiver, client_message, MAXSIZE, 0,
                          (struct sockaddr *)&client, &sockaddr_size)) > 0){


    // if client indicates end of file, break from loop and stop accepting data
    if(strcmp(client_message, "EOF") == 0){
      break;
    }

    // write to output file
    nwritten = fwrite(client_message, 1, nread, fp);
    if(nwritten != nread){

      perror("fwrite");
      exit(EXIT_FAILURE);

    }

    // clear client_message buffer
    bzero(client_message, MAXSIZE);
  }

  close(receiver);
}


/*

tcp_file_client:
provides the functionality of a TCP client.
Reads a file from disk and sends accross network through connected socket.

*/
void tcp_file_client(char* host, long port, FILE* fp){

  int connection, nread;
  char buffer[MAXSIZE];
  struct sockaddr_in server;

  initialize_sockaddr(&server, host, port);

  // initialize socket that will act as connection to TCP server
  if((connection = socket(AF_INET, SOCK_STREAM, 0)) < 0){

    perror("socket creation");
    exit(EXIT_FAILURE);

  }


  // connect to TCP server
  if(connect(connection, (struct sockaddr *)&server, sizeof(server)) < 0){

    perror("connection failed");
    exit(EXIT_FAILURE);

  }


  // loop while not at end of file
  while((nread = fread(buffer, 1, MAXSIZE, fp)) != 0){

    // send to server
    write_out(connection, buffer, nread);
    // clear message buffer after data is sent
    bzero(buffer, MAXSIZE);

  }

  close(connection);
}


/*

udp_file_client:
provides the functionality of a UDP client.
Reads a file from disk and sends across network.

*/
void udp_file_client(char* host, long port, FILE* fp){

  int sock;
  ssize_t nread;
  char buffer[MAXSIZE];
  struct sockaddr_in server;

  initialize_sockaddr(&server, host, port);

  // initialize socket through which data will be sent
  if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){

    perror("socket creation");
    exit(EXIT_FAILURE);

  }

  // loop while not at end of file
  while((nread = fread(buffer, 1, MAXSIZE, fp)) > 0){

    // send data to server
    udp_send(sock, &server, buffer, nread);
    // clear buffer after data is sent
    bzero(buffer, MAXSIZE);

  }

  // if reached end of file
  if(nread == 0){
    // notify server that end of file has been reached
    udp_send(sock, &server, "EOF", 3);
  }

  close(sock);
}



/*

initialize_sockaddr:
initializes a sockaddr_in structure for use

*/
void initialize_sockaddr(struct sockaddr_in *name, char *hostname, long port){

    struct hostent *hostinfo;

    // if no hostname given, assume localhost
    if(hostname == NULL){
      hostname = "127.0.0.1";
    }

    // get host IP
    hostinfo = gethostbyname(hostname);
    if(hostinfo == NULL){
        fprintf(stderr, "unknown host %s.\n", hostname);
        exit(EXIT_FAILURE);
    }

    // initialize structure components
    name->sin_family = AF_INET;
    name->sin_port = htons(port);
    name->sin_addr = *(struct in_addr *)hostinfo->h_addr;

}



/*

write_out:
writes data (message) to given socket

*/
void write_out(int sock, char *message, ssize_t len){

  if(write(sock, message, len) < 0){

    perror("write");
    exit(EXIT_FAILURE);

  }
}


/*

udp_send:
sends data (buffer) through the given socket to the host at dest

*/
void udp_send(int sock, struct sockaddr_in *dest, char* buffer, ssize_t len){

  int nsent;

  // send data to dest
  nsent = sendto(sock, buffer, len, 0, (struct sockaddr *)dest,
                 sizeof(struct sockaddr));

  // if send fails exit
  if(nsent < 0){

    perror("sendto");
    exit(EXIT_FAILURE);

  }
}
