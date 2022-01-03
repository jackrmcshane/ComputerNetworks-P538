/*

 *  CHAT.C - Jack McShane (jamcshan)
 *  CREATED: 09/20/21
 *
 *  The chat.c file implements the functionality for a simple tcp and udp client
 *  server relationship.
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>


#define MAXSIZE         255 // max message size
#define BACKLOG         10  // max number of pending connections
#define DEFAULT_IFACE   "silo.luddy.indiana.edu"

void write_to(int, char *);
void tcp_server(char* iface, long port);
void udp_server(char* iface, long port);
void tcp_client(char* host, long port);
void udp_client(char* host, long port);
void init_sockaddr(struct sockaddr_in *, char *, long);
void respond_to_client(int, char *);
void respond_udp(int, char *, struct sockaddr_in *);
void send_to(int, char *, struct sockaddr_in *);


/* Add function definitions */
void chat_server(char* iface, long port, int use_udp) {

  if(use_udp == 1){

    udp_server(iface, port);

  } else {

    tcp_server(iface, port);

  }
}


void chat_client(char* host, long port, int use_udp) {

  if(use_udp == 1){

    udp_client(host, port);

  } else {

    tcp_client(host, port);

  }
}



void tcp_server(char* iface, long port){

  // declarations
  bool keep_alive;
  char client_message[MAXSIZE],
       close_message[MAXSIZE] = "goodbye\n",
       exit_message[MAXSIZE] = "exit\n";
  int listener, connection, nread, nconn = -1;
  struct sockaddr_in server, client;
  socklen_t sockaddr_size;


  // init server address
  init_sockaddr(&server, iface, port);

  // create server side socket
  if((listener = socket(AF_INET, SOCK_STREAM, 0)) < 0){

    perror("socket creation");
    return;

  }

  // bind listener to server port
  if(bind(listener, (struct sockaddr *)&server, sizeof(server)) < 0){

    perror("bind");
    exit(EXIT_FAILURE);

  }

  // listening for incoming connections request
  if(listen(listener, BACKLOG) < 0){

    perror("listen");
    exit(EXIT_FAILURE);

  }

  // accepting connections
  do{

    sockaddr_size = sizeof(struct sockaddr_in);

    // initializing connection
    if((connection = accept(listener, (struct sockaddr*)&client, &sockaddr_size)) < 0){

      perror("accept");
      exit(EXIT_FAILURE);

    }

    printf("connection %d from ('%s', %u)\n", ++nconn, inet_ntoa(client.sin_addr), ntohs(client.sin_port));

    // send and receive loop until close is sent
    do{

      // receive message and print/process
      if((nread = recv(connection, client_message, MAXSIZE, 0)) < 0){

        perror("recv");
        exit(EXIT_FAILURE);

      }

      client_message[nread] = '\0';
      printf("got message from ('%s', %u)\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
      respond_to_client(connection, client_message);

      keep_alive = ( strcmp(client_message, close_message) != 0 ) &&
                   ( strcmp(client_message, exit_message) != 0 );

    } while( keep_alive );

    close(connection);

  } while(strcmp(client_message, exit_message) != 0);

  close(listener);
}


void udp_server(char* iface, long port){

  int receiver;
  ssize_t nread;
  char client_message[MAXSIZE], exit_message[MAXSIZE] = "exit\n";
  struct sockaddr_in server, client;
  socklen_t sockaddr_size;


  // create UDP socket
  if((receiver = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
    perror("socket");
    exit(EXIT_FAILURE);
  }


  // initialize server address
  init_sockaddr(&server, iface, port);
  // bind UDP socket to server addr
  if(bind(receiver, (struct sockaddr *)&server, sizeof(server)) < 0){
    perror("bind");
    exit(EXIT_FAILURE);
  }


  do{

    sockaddr_size = sizeof(struct sockaddr_in);

    if((nread = recvfrom(receiver, client_message, MAXSIZE, 0, (struct sockaddr *)&client, &sockaddr_size)) < 0){
      perror("recvfrom");
      exit(EXIT_FAILURE);
    }

    client_message[nread] = '\0';
    printf("got message from ('%s', %u)\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
    respond_udp(receiver, client_message, &client);

  } while(strcmp(client_message, exit_message) != 0);

  close(receiver);

}


void tcp_client(char* host, long port){

  bool keep_alive;
  int sock; // client side socket
  ssize_t nread;
  struct sockaddr_in server; // holds server info
  char buffer[MAXSIZE],
       received[MAXSIZE],
       close_message[MAXSIZE] = "goodbye\n",
       exit_message[MAXSIZE] = "exit\n";


  // create client side socket
  if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){

    perror("socket");
    return;

  }

  // connecting to server side socket
  init_sockaddr(&server, host, port);

  if(connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0){

    perror("connect failed");
    return;

  }


  // send & recv loop
  do{

    // get input from user
    if(fgets(buffer, sizeof(buffer), stdin) == NULL){

      perror("failed: input read");
      exit(EXIT_FAILURE);

    }

    // write to server
    write_to(sock, buffer);

    // wait for response
    if((nread = recv(sock, received, MAXSIZE, 0)) < 0){

      perror("recv");
      exit(EXIT_FAILURE);

    }

    received[nread] = '\0';
    printf("%s", received);
    keep_alive = ( strcmp(close_message, buffer) != 0 ) &&
                       ( strcmp(exit_message, buffer) != 0 );

  } while( keep_alive );

  close(sock);
}


void udp_client(char* host, long port){

  bool keep_alive;
  int sock;
  ssize_t nread;
  struct sockaddr_in server;
  char buffer[MAXSIZE], received[MAXSIZE], close_message[MAXSIZE] = "goodbye\n", exit_message[MAXSIZE] = "exit\n";
  socklen_t sockaddr_size;

  init_sockaddr(&server, host, port);

  if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
    perror("socket");
    exit(EXIT_FAILURE);
  }


  do{

    if(fgets(buffer, sizeof(buffer), stdin) == NULL){

      perror("failed: input read");
      exit(EXIT_FAILURE);

    }

    // stuff
    send_to(sock, buffer, &server);
    sockaddr_size = sizeof(struct sockaddr_in);

    if((nread = recvfrom(sock, received, MAXSIZE, 0, (struct sockaddr *)&server, &sockaddr_size)) < 0){

      perror("recvfrom");
      exit(EXIT_FAILURE);

    }

    received[nread] = '\0';
    printf("%s", received);
    keep_alive = (strcmp(buffer, close_message) != 0) && (strcmp(buffer, exit_message) != 0);

  } while( keep_alive );

  close(sock);

}


void write_to(int sock, char *message){

  if(write(sock, message, strlen(message)+1) < 0){

    perror("write");
    exit(EXIT_FAILURE);

  }
}


void init_sockaddr(struct sockaddr_in *name, char *hostname, long port){

    struct hostent *hostinfo;

    if(hostname == NULL){
      hostname = "127.0.0.1";
    }

    hostinfo = gethostbyname(hostname);
    if(hostinfo == NULL){
        fprintf(stderr, "unknown host %s.\n", hostname);
        exit(EXIT_FAILURE);
    }

    name->sin_family = AF_INET;
    name->sin_port = htons(port);
    name->sin_addr = *(struct in_addr *)hostinfo->h_addr;

}


void respond_to_client(int connection, char* client_message){

  #define NRESPONSES 3
  char responses[NRESPONSES][MAXSIZE] = {
    "ok\n",
    "farewell\n",
    "world\n"
  };

  if(strcmp(client_message, "exit\n") == 0){

    write_to(connection, responses[0]);

  } else if(strcmp(client_message, "goodbye\n") == 0) {

    write_to(connection, responses[1]);

  } else if(strcmp(client_message, "hello\n") == 0){

    write_to(connection, responses[2]);

  } else {

    write_to(connection, client_message);

  }
}


void respond_udp(int sock, char *client_message, struct sockaddr_in *client){

  char responses[NRESPONSES][MAXSIZE] = {
    "ok\n",
    "farewell\n",
    "world\n"
  };

  if(strcmp(client_message, "exit\n") == 0){

    send_to(sock, responses[0], client);

  } else if(strcmp(client_message, "goodbye\n") == 0){

    send_to(sock, responses[1], client);

  } else if(strcmp(client_message, "hello\n") == 0){

    send_to(sock, responses[2], client);

  } else {

    send_to(sock, client_message, client);

  }
}


void send_to(int sock, char* message, struct sockaddr_in *client){

  if(sendto(sock, message, strlen(message), 0, (struct sockaddr *)client, sizeof(struct sockaddr)) < 0){
    perror("sendto");
    exit(EXIT_FAILURE);
  }
}
