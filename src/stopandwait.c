#include "stopandwait.h"
/*
 *  STOPANDWAIT - Jack McShane (jamcshan)
 *  CREATED: 11/1/21
 *
 *  STOPANDWAIT implements the functionality of reliable data transfer (RDT)
 *  for file transfer across lossy networks.
 */


/*
ideas:
  could make a packet struct?
  refactor the code
*/

#define MAXSIZE  256


// define acceptable message types
enum msg_type {ACK, DATA, FIN};

// define pkt header
typedef struct {
  int seq;
  enum msg_type msg_type;
  int nbytes;
} header_t;


int create_socket();
int isACK(int seq, header_t* header);
int server_socket(struct sockaddr_in* server);
int read_file_chunk(char* buffer, int size, FILE* fp);
int wait_on_input(fd_set* read_set, struct timeval* timeout);
void makeACK(char* packet, int seq);
void makeFIN(char* packet, int seq);
void write_file_chunk(char* buffer, int nbytes, FILE* fp);
void udt_send(int sock, struct sockaddr_in* dest, char* pkt, int nbytes);
void udt_recv(int sock, struct sockaddr_in* addr, char* buf);
void extract(header_t* header, char* buf, char* pkt);
void init_addr(struct sockaddr_in* server, char* iface, long port);
void make_packet(char* packet, header_t* header, ssize_t nbytes, char* buf);
void transfer_file(int sock, struct sockaddr_in* server, FILE* fp, int* seq);
void recv_file(int sock, FILE* fp, int* seq);
void client_shutdown(int sock, struct sockaddr_in* server, int* seq);
void server_shutdown(int sock, struct sockaddr_in* client, int* seq);



/*
void stopandwait_server(char* iface, long port, FILE* fp)
takes:
- iface: interface (generally loopback) over which to receive data
- port: port over which to receive data
- fp: pointer to the output file

provides server functionality for reliable file transfer
*/
void stopandwait_server(char* iface, long port, FILE* fp) {

  // declarations
  int sock, seq;
  struct sockaddr_in server;

  seq = 0;
  init_addr(&server, iface, port);
  sock = server_socket(&server);

  recv_file(sock, fp, &seq);

  server_shutdown(sock, &server, &seq);

}


/*
void stopandwait_client(char* host, long port, FILE* fp)
takes:
- host: hostname or IP address of the server where data is to be sent
- port: port over which to send data
- fp: pointer to file containing data to be relayed to server

reads from the input file and writes data out to the server using the specified
port
*/
void stopandwait_client(char* host, long port, FILE* fp) {

  // declarations
  int sock, seq;
  struct sockaddr_in server;

  seq = 0;
  sock = create_socket();
  init_addr(&server, host, port);

  transfer_file(sock, &server, fp, &seq);

  client_shutdown(sock, &server, &seq);
}



/*
void recv_file(int sock, FILE* fp, int* seq)
takes:
- sock: socket over which to receive the file
- fp: pointer to the output file
- seq: pointer to the starting sequence

continues to read data from the provided socket and write to designated output
file until a FIN packet is received
*/
void recv_file(int sock, FILE* fp, int* seq){

  // declarations
  const unsigned int TIMEOUT_SEC = 1;
  const unsigned int TIMEOUT_USEC = 0;

  int can_read;
  fd_set read_set;
  header_t header;
  struct timeval timeout;
  struct sockaddr_in client;
  char file_data[MAXSIZE], packet[MAXSIZE], recvd_packet[MAXSIZE];
  // end declarations

  while(1){

    // reset read_set
    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);

    // set timer
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = TIMEOUT_USEC;

    can_read = wait_on_input(&read_set, &timeout);

    if(can_read > 0){

      udt_recv(sock, &client, recvd_packet);
      extract(&header, file_data, recvd_packet);

      if(header.seq == *seq){ // received packet in sequence

        // enter exiting logic
        if(header.msg_type == FIN)
          break;

        // otherwise write buffer to file
        write_file_chunk(file_data, header.nbytes, fp);

        makeACK(packet, *seq);
        udt_send(sock, &client, packet, sizeof(header_t));

        *seq = (*seq + 1) % 2;

      } else { // seq numbers do not match

        // send ACK for previous sequence
        makeACK(packet, (*seq + 1) % 2);
        udt_send(sock, &client, packet, sizeof(header_t));

      }
    } // else timeout occurred

  } // end recv loop

}




/*
void transfer_file(int sock, struct sockaddr_in* server, FILE* fp, int* seq)
takes:
- sock: socket over which to send data from the input file
- server: pointer to sockaddr_in structure containing server/destination address
- fp: pointer to input file
- seq: pointer to starting sequence number

transfers the given file over the specified socket in chunks of 256B
*/
void transfer_file(int sock, struct sockaddr_in* server, FILE* fp, int* seq){

  // declarations
  const unsigned int TIMEOUT_SEC = 1;
  const unsigned int TIMEOUT_USEC = 0;

  fd_set read_set;
  header_t header;
  int nread, can_read;
  struct timeval timeout;
  char data[MAXSIZE], packet[MAXSIZE], recvd_packet[MAXSIZE];
  // end declarations


  // get chunk while data left in file
  while((nread = read_file_chunk(data, (MAXSIZE - sizeof(header_t)), fp)) > 0){

    header = (header_t){ .seq = *seq, .msg_type = DATA, .nbytes = nread };
    make_packet(packet, &header, nread, data);

    udt_send(sock, server, packet, sizeof(header_t) + nread);

    // set timer
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = TIMEOUT_USEC;

    while(1){

      // set read_set
      FD_ZERO(&read_set);
      FD_SET(sock, &read_set);

      can_read = wait_on_input(&read_set, &timeout);

      if(can_read > 0){

        // read input and process
        udt_recv(sock, server, recvd_packet);
        extract(&header, data, recvd_packet);

        if(isACK(*seq, &header)){

          *seq = (*seq + 1) % 2;
          break;

        }

      } else { // timeout has occurred

        // retransmit
        udt_send(sock, server, packet, sizeof(header_t) + nread);

        // restart timer
        timeout.tv_sec = TIMEOUT_SEC;
        timeout.tv_usec = TIMEOUT_USEC;
      }
    }


  }
}


/*
int read_file_chunk(char* buffer, int size, FILE* fp)
takes:
- buffer: empty buffer to be filled by file data
- size: number of bytes to read from file
- fp: pointer to the file to be read

returns: number of bytes successfully read from the file

reads *size* bytes from the specified file
*/
int read_file_chunk(char* buffer, int size, FILE* fp){
  int nread;

  nread = fread(buffer, 1, size, fp);
  if(nread < 0){

    perror("fread");
    exit(EXIT_FAILURE);

  }

  return nread;
}



/*
void write_file_chunk(char* buffer, int nbytes, FILE* fp)
takes:
- buffer: buffer containing data to be written to the output file
- nbytes: number of bytes to be written from the buffer
- fp: pointer to the output file

writes nbytes from the buffer to the output file
*/
void write_file_chunk(char* buffer, int nbytes, FILE* fp){

  int nwritten;
  nwritten = fwrite(buffer, 1, nbytes, fp);

  if(nwritten < 0){

    perror("fwrite");
    exit(EXIT_FAILURE);

  }
}


/*
void server_shutdown(int sock, struct sockaddr_in* client, int* seq)
takes:
- sock: socket from which to receive data
- client: pointer to sockaddr_in containing the address of the host we are
receiving data from 
- seq: pointer to the current sequence value

performs the necessary triple ack logic for shutting down the server side of the
communication
*/
void server_shutdown(int sock, struct sockaddr_in* client, int* seq){

  // declarations
  const unsigned int TIMEOUT_SEC = 1;
  const unsigned int TIMEOUT_USEC = 0;
  const unsigned int MAX_TIMEOUTS = 30;

  int n_timeouts, can_read;
  fd_set read_set;
  header_t header;
  struct timeval timeout;
  char packet[MAXSIZE], file_data[MAXSIZE], recvd_packet[MAXSIZE];

  // build FIN packet to ACK received FIN and tell client entering shutdown
  makeFIN(packet, *seq);
  udt_send(sock, client, packet, sizeof(header));

  // set timer
  timeout.tv_sec = TIMEOUT_SEC;
  timeout.tv_usec = TIMEOUT_USEC;

  // init timeout count
  n_timeouts = 0;

  // waiting ~30 sec for client ACK before shutting down
  while(n_timeouts < MAX_TIMEOUTS){

    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);

    can_read = wait_on_input(&read_set, &timeout);
    if(can_read > 0){

      // read and process
      udt_recv(sock, client, recvd_packet);
      extract(&header, file_data, recvd_packet);

      if(header.msg_type == ACK)
        break; 

    } else {

      // timeout occurred: resend FIN
      n_timeouts++;
      udt_send(sock, client, packet, sizeof(header));

      // reset timer
      timeout.tv_sec = TIMEOUT_SEC;
      timeout.tv_usec = TIMEOUT_USEC;

    }
  }

  close(sock);
}


/*
void client_shutdown(int sock, struct sockaddr_in* server, int* seq)
takes:
- sock: socket over which to communicate shutdown with server
- server: pointer to sockaddr_in structure containing the address of server to
which data is being sent

performs the triple ACK logic for shutting down the client side of the
communication once the end of the file has been reached
*/
void client_shutdown(int sock, struct sockaddr_in* server, int* seq){

  // declarations
  const unsigned int TIMEOUT_SEC = 1;
  const unsigned int TIMEOUT_USEC = 0;
  const unsigned int MAX_TIMEOUTS = 30;

  int n_timeouts, can_read;
  fd_set read_set;
  header_t header;
  char packet[MAXSIZE], message_data[MAXSIZE], recvd_packet[MAXSIZE];
  struct timeval timeout;

  // triple ACK logic for closing
  makeFIN(packet, *seq);
  udt_send(sock, server, packet, sizeof(header_t));

  // set timer
  timeout.tv_sec = TIMEOUT_SEC;
  timeout.tv_usec = TIMEOUT_USEC;

  while(1){

    // set read_set
    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);

    can_read = wait_on_input(&read_set, &timeout);

    if(can_read > 0){ // there is input to be read

      // read input and process
      udt_recv(sock, server, recvd_packet);
      extract(&header, message_data, recvd_packet);

      if(header.msg_type == FIN) // received FIN from server
        break;

    } else { // timeout has occurred

      // retransmit
      udt_send(sock, server, packet, sizeof(header_t));

      // reset timer
      timeout.tv_sec = TIMEOUT_SEC;
      timeout.tv_usec = TIMEOUT_USEC;

    }
  }


  // sending final ACK
  n_timeouts = 0;
  makeACK(packet, *seq);

  // this aint exactly right but should still work on 30% channel
  while(n_timeouts < MAX_TIMEOUTS){

    udt_send(sock, server, packet, sizeof(header_t));
    n_timeouts++;

  }

  close(sock);
}



/*
void udt_send(int sock, struct sockaddr_in* dest, char* pkt, int nbytes)
takes:
- sock: socket descriptor through which data is to be sent
- dest: pointer to the sockaddr_in structure containing the destination address
- pkt: pointer to the data to be sent
- nbytes: number of bytes to be send through the socket (from the pkt buffer)

sends the passed data through the socket using UDP
*/
void udt_send(int sock, struct sockaddr_in* dest, char* pkt, int nbytes){

    int nsent;
    nsent = sendto(sock, pkt, nbytes, 0, (struct sockaddr*)dest,
                    sizeof(struct sockaddr));

    if(nsent < 0){
        perror("sendto");
        exit(EXIT_FAILURE);
    }
}


/*
void udt_recv(int sock, struct sockaddr_in* addr, char* buf)
takes:
- sock: socket file descriptor through which to receive data
- addr: empty pointer to sockaddr_in structure which will be filled with the
address of the host data has been received from
- buf: the buffer into which the received data will be placed 

reads an incoming packet and places the data into the given buffer
*/
void udt_recv(int sock, struct sockaddr_in* addr, char* buf){
  int nread;
  socklen_t sockaddr_size;

  sockaddr_size = sizeof(struct sockaddr_in);
  nread = recvfrom(sock, buf, MAXSIZE, 0,
                    (struct sockaddr*)addr, &sockaddr_size);

  if(nread < 0){
    perror("rcvfrom");
    exit(EXIT_FAILURE);
  }
}


/*
int wait_on_input(fd_set* read_set, struct timeval* timeout)
takes:
- read_set: file descriptor set to monitor for input from a client machine
- timeout: pointer to timeval structure containing the length of time to wait
on input before timeout

returns:
- status -- <= 0 cannot read, >0 can read

wrapper for the select() function
*/
int wait_on_input(fd_set* read_set, struct timeval* timeout){
  int can_read;
  can_read = select(FD_SETSIZE, read_set, NULL, NULL, timeout);
  if(can_read < 0){
    perror("select");
    exit(EXIT_FAILURE);
  }

  return can_read;
}


/*
void extract(header_t* header, char* buf, char* pkt)
takes:
- header: pointer to an empty header structure
- buf: the buffer into which the packet's payload will be copied
- pkt: the received packet from which the header and data are to be extracted

extracts the header and the payload data of the passed packet for processing
*/
void extract(header_t* header, char* buf, char* pkt){

  memcpy(header, pkt, sizeof(header_t));
  if(header->nbytes != 0)
    memcpy(buf, pkt + sizeof(header_t), header->nbytes);
}



/*
void makeACK(char* packet, int seq)
takes:
- packet: buffer into which the ACK will be placed for sending
- seq: the sequence number of the packet to be acknowledged

wraps 'make_packet' function to synthesize an ACK for the packet with the passed
seq number
*/
void makeACK(char* packet, int seq){

  header_t header;

  header = (header_t){ .seq = seq, .msg_type = ACK, .nbytes = 0 };
  make_packet(packet, &header, 0, "");
}



/*
void makeFIN(char* packet, int seq)
takes:
- packet:  pointer to buffer where the newly made packet will be stored
- seq: the sequence number of the new packet to be used in the header

a wrapper of the 'make_packet' function that builds a FIN packet
*/
void makeFIN(char* packet, int seq){

  header_t header;

  header = (header_t){ .seq = seq, .msg_type = FIN, .nbytes = 0 };
  make_packet(packet, &header, 0, "");

}



/*
void make_packet(char* packet, header_t* header, ssize_t nbytes, char* buf)
takes:
- packet: the buffer into which the finished packet will be placed
- header: pointer to a header structure containing the relevant meta info
- nbytes: number of bytes to read from buf
- buf: buffer in which the file data is stored and from which it will be copied
into the packet buffer

creates a packet using the header and buffer structures
*/
void make_packet(char* packet, header_t* header, ssize_t nbytes, char* buf){

  memcpy(packet, header, sizeof(header_t));
  memcpy(packet + sizeof(header_t), buf, nbytes);
}



/*
int isACK(int seq, header_t* header)
takes:
- seq: expected sequence number
- header: pointer to the header structure of the packet just received. The seq
field of the structure is used to validate the received ACK packet

checks the validity of a received ACK
*/
int isACK(int seq, header_t* header){

  return (header->seq == seq) && (header->msg_type == ACK);
}



/*
void init_addr(struct sockaddr_in* server, char* iface, long port)
takes:
- server: empty structure that will store the address of the server to which
data will be sent
- iface: interface over which to communicate
- port: port number that communication will flow through

initializes a sockaddr_in structure for use
*/
void init_addr(struct sockaddr_in* server, char* iface, long port){

    struct hostent *hostinfo;

    // if no hostname given, assume localhost
    if(iface == NULL)
      iface = "127.0.0.1";

    // get host IP
    hostinfo = gethostbyname(iface);
    if(hostinfo == NULL){
        fprintf(stderr, "unknown host %s.\n", iface);
        exit(EXIT_FAILURE);
    }

    // initialize structure components
    server->sin_family = AF_INET;
    server->sin_port = htons(port);
    server->sin_addr = *(struct in_addr *)hostinfo->h_addr;
}


/*
int server_socket(struct sockaddr_in* server)
takes:
- server: sockaddr_in structure containing the address of the server to which
the created socket is to be bound

returns: socket file descriptor bound to the passed server address

creates a server socket
*/
int server_socket(struct sockaddr_in* server){

  int sock, optval = 1;

  sock = create_socket();
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

  if(bind(sock, (struct sockaddr*)server, sizeof(*server)) < 0){

    perror("bind");
    exit(EXIT_FAILURE);

  }

  return sock;
}


/*
int create_socket()
returns: socket file descriptor

creates socket file descriptor
*/
int create_socket(){

  int sock;
  if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){

    perror("socket");
    exit(EXIT_FAILURE);

  }

  return sock;
}
