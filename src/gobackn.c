#include "gobackn.h"
/*
 *  GOBACKN - Jack McShane (jamcshan)
 *  CREATED: 11/15/2021
 *
 *  Provides Go Back N functionality for single file transfers
 */


/*
   program log:
   still have to finish the rtt calculations
*/


#define MAXSIZE 256
#define WINMAX 128


// define acceptable message types
enum msg_type {ACK, DATA, FIN};

// define pkt header
typedef struct {
  int seq;
  enum msg_type msg_type;
  int nbytes; // number of bytes in payload (not total num bytes of packet)
} header_t;


void write_to_file(char* buffer, int nbytes, FILE* fp);
int new_socket();
void buildFIN(char* packet, int seq);
int isValidACK(header_t* header, int base_seq, int nextseq);
int isValidFIN(header_t* header, int base_seq, int nextseq);
int get_dist(int start, int stop);
int get_chunk(char* buffer, int size, FILE* fp);
int wait_for_input(fd_set* read_set, struct timeval* timeout);
int isValidSeq(int seq, int base_seq, int nextseq);
void addr_init(struct sockaddr_in* server, char* iface, long port);
void build_packet(char* packet, header_t* header, ssize_t nbytes, char* buf);
void send_udt(int sock, struct sockaddr_in* dest, char* pkt, int nbytes);
void udp_recv(int sock, struct sockaddr_in* addr, char* buf);
void extract_data(header_t* header, char* buf, char* pkt);
void get_avg_rtt(struct timeval* avg_rtt, struct timeval* time_sent);
int server_sock(struct sockaddr_in* server);
void buildACK(char* packet, int seq);
void shutdown_server(int sock, struct sockaddr_in* client, int* seq);
void timeval_mult(struct timeval* result, struct timeval* tv, int alpha);

/*
void gbn_server(char* iface, long port, FILE* fp)
takes:
- iface: interface (generally loopback) over which to receive data
- port: port over which to receive data
- fp: pointer to the output file

provides server functionality for go back N procedure
*/
void gbn_server(char* iface, long port, FILE* fp) {

  const unsigned int TIMEOUT_SEC = 1;
  const unsigned int TIMEOUT_USEC = 0;

  // declarations
  int sock;
  int last_acked, nextseq;
  int can_read;
  fd_set read_set;
  header_t header;
  struct timeval timeout;
  struct sockaddr_in server, client;
  char packet[MAXSIZE], recvd_packet[MAXSIZE], data[MAXSIZE];

  // nextseq = 0
  last_acked = -1, nextseq = 0;
  addr_init(&server, iface, port);
  sock = server_sock(&server);

  // while( 1 )
  while( 1 ){

    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);

    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = TIMEOUT_USEC;

    // if receive packet
    can_read = wait_for_input(&read_set, &timeout);
    if( can_read > 0 ){

      udp_recv(sock, &client, recvd_packet);
      extract_data(&header, data, recvd_packet);

      // if seqnum == nextseq
      if( header.seq == nextseq ){

        if(header.msg_type == FIN){
          break; // enter shutdown logic
        }

        // deliver data to upper layer
        write_to_file(data, header.nbytes, fp);

        buildACK(packet, nextseq);
        send_udt(sock, &client, packet, sizeof(header_t));

        // update statements
        last_acked = nextseq;
        nextseq = ( nextseq + 1 ) % (WINMAX + 1);

      } else { // timeout occurred

        // ack last received packet
        buildACK(packet, last_acked);
        send_udt(sock, &client, packet, sizeof(header_t));
      }
    } // else timeout
  }

  // BEGIN: SHUTDOWN PHASE
  shutdown_server(sock, &client, &nextseq);
}



/*
void gbn_client(char* host, long port, FILE* fp)
takes:
- host: hostname or IP address of the server where data is to be sent
- port: port over which to send data
- fp: pointer to file containing data to be relayed to server

reads from the input file and writes data out to the server using the specified
port and Go Back N functionality
*/
void gbn_client(char* host, long port, FILE* fp) {

  // declarations
  int sock;
  int nread, can_read;
  int head, tail, current;
  int base_seq, nextseq, winsize, in_flight, num_acked;
  header_t header;
  fd_set read_set;
  struct sockaddr_in server;
  struct timeval timeout, avg_rtt;
  struct timeval send_times[WINMAX];
  char packets[WINMAX][MAXSIZE];
  char data[MAXSIZE], recvd_packet[MAXSIZE];
  int times_to_send = 30;
  unsigned int usec = 1000000;

  // initializations
  nread = -1;
  head = 0, tail = 0, current = 0;
  base_seq = 0, nextseq = 0, winsize = 1;

  avg_rtt.tv_sec = 3;
  avg_rtt.tv_usec = 0;

  sock = new_socket();
  addr_init(&server, host, port);


  // enter go back n loop
  while(1){


    // BEGIN: SEND N PACKETS
    in_flight = get_dist(tail, current);
    for(int i = (winsize - in_flight); i > 0; i--){

      if((current == head) && (nread != 0)){ // need new chunk

        nread = get_chunk(data, (MAXSIZE - sizeof(header_t)), fp);
        if(nread == 0){ // EOF

          buildFIN(packets[head], nextseq);

        } else { // read successful

          header=(header_t){.seq = nextseq, .msg_type = DATA, .nbytes = nread};
          build_packet(packets[head], &header, nread, data);
        }

        // update statements
        nextseq = (nextseq + 1) % (WINMAX + 1);
        head = (head + 1) % WINMAX;
      }

      // send packet & mark time sent
      send_udt(sock, &server, packets[current], sizeof(header_t) + nread);
      gettimeofday(&send_times[current], NULL);

      // point current to next packet
      current = (current + 1) % WINMAX;
    } // END: SEND N PACKETS



    // BEGIN: WAIT ON ACK

    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);

    // setting timeout value to 2RTT
    timeout.tv_sec = 2 * avg_rtt.tv_sec;
    timeout.tv_usec = 2 * avg_rtt.tv_usec;

    can_read = wait_for_input(&read_set, &timeout);
    if(can_read > 0){ // have input

      udp_recv(sock, &server, recvd_packet);
      extract_data(&header, data, recvd_packet);

      if(isValidACK(&header, base_seq, nextseq)){

        // point tail to ACKed packet & send time
        num_acked = base_seq - header.seq;
        tail = (tail + num_acked) % WINMAX;
        base_seq = (base_seq + num_acked) % WINMAX;
        if((winsize + num_acked) > WINMAX)
          winsize = WINMAX;
        else
          winsize += num_acked;

        // calc new moving avg
        get_avg_rtt(&avg_rtt, &send_times[tail]);

        // point tail @ packet to be acked next
        tail = (tail + 1) % WINMAX;
        base_seq = (base_seq + 1) % WINMAX;

        if(tail > current)
          current = tail;

      } else if(isValidFIN(&header, base_seq, nextseq))
        break; // move to final phase of client shutdown

    } else {  // timeout

      current = tail;
      winsize = winsize / 2;
      // in case first packet is lost
      if(winsize == 0)
        winsize = 1;
    }   // END: WAIT ON ACK
  } // END: CLIENT LOOP

  // BEGIN: FINAL ACK
  buildACK(packets[head], nextseq);

  // these updates are not strictly necessary but enforce the semantics of how
  // nextseq and head operate
  nextseq = (nextseq + 1) % (WINMAX + 1);
  head = (head + 1) % WINMAX; 

  for(int i = 0; i < times_to_send; i++){

    send_udt(sock, &server, packets[current], sizeof(header_t));
    usleep(usec);
  }

  close(sock);
}


/*
int wait_for_input(fd_set* read_set, struct timeval* timeout)
takes:
- read_set: file descriptor set to monitor for input from a client machine
- timeout: pointer to timeval structure containing the length of time to wait
on input before timeout

returns:
- status -- <= 0 cannot read, >0 can read

wrapper for the select() function
*/
int wait_for_input(fd_set* read_set, struct timeval* timeout){
  int can_read;
  can_read = select(FD_SETSIZE, read_set, NULL, NULL, timeout);
  if(can_read < 0){
    perror("select");
    exit(EXIT_FAILURE);
  }

  return can_read;
}



/*
int isValidACK(header_t* header, int base_seq, int nextseq)
takes:
- header: header of received packet
- base_seq: start of the send window
- nextseq: next sequence to be sent

returns: integer value indicating whether the header of the ACK package is valid
*/
int isValidACK(header_t* header, int base_seq, int nextseq){
  return (header->msg_type==ACK) && isValidSeq(header->seq, base_seq, nextseq);
}


/*
int isValidFIN(header_t* header, int base_seq, int nextseq)
takes:
- header: header of the FIN package to be validated
- base_seq: sequence of the packet that denotes the beginning of the in flight
window

returns: integer value indicating the validity of the received FIN package
*/
int isValidFIN(header_t* header, int base_seq, int nextseq){
  return (header->msg_type==FIN) && isValidSeq(header->seq, base_seq, nextseq);
}


/*
int isValidSeq(int seq, int base_seq, int nextseq)
takes:
- seq: sequence of the received packet
- base_seq: "lower" bound for validity window
- nextseq: upper" bound for validity window

returns: integer value indicating validity of the passed sequence number
*/
int isValidSeq(int seq, int base_seq, int nextseq){
  if(base_seq < nextseq)
    return (seq >= base_seq) && (seq < nextseq);

  if(base_seq > nextseq)
    return (( seq >= base_seq ) && ( seq < ( WINMAX + 1 ))) || 
            ((seq >= 0) && (seq < nextseq));

  return 0;
}






/*
int get_chunk(char* buffer, int size, FILE* fp)
takes:
- buffer: empty buffer to be filled by file data
- size: number of bytes to read from file
- fp: pointer to the file to be read

returns: number of bytes successfully read from the file

reads *size* bytes from the specified file
*/
int get_chunk(char* buffer, int size, FILE* fp){
  int nread;

  nread = fread(buffer, 1, size, fp);
  if(nread < 0){

    perror("fread");
    exit(EXIT_FAILURE);

  }

  return nread;
}



/*
int server_sock(struct sockaddr_in* server)
takes:
- server: sockaddr_in structure containing the address of the server to which
the created socket is to be bound

returns: socket file descriptor bound to the passed server address

creates a server socket
*/
int server_sock(struct sockaddr_in* server){

  int sock, optval = 1;

  sock = new_socket();
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

  if(bind(sock, (struct sockaddr*)server, sizeof(*server)) < 0){

    perror("bind");
    exit(EXIT_FAILURE);

  }

  return sock;
}


/*
int new_socket()
returns: socket file descriptor

creates socket file descriptor
*/
int new_socket(){

  int sock;
  if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){

    perror("socket");
    exit(EXIT_FAILURE);

  }

  return sock;
}



/*
void buildACK(char* packet, int seq)
takes:
- packet: buffer into which the ACK will be placed for sending
- seq: the sequence number of the packet to be acknowledged

wraps 'make_packet' function to synthesize an ACK for the packet with the passed
seq number
*/
void buildACK(char* packet, int seq){

  header_t header;

  header = (header_t){ .seq = seq, .msg_type = ACK, .nbytes = 0 };
  build_packet(packet, &header, 0, "");
}



/*
void buildFIN(char* packet, int seq)
takes:
- packet:  pointer to buffer where the newly made packet will be stored
- seq: the sequence number of the new packet to be used in the header

a wrapper of the 'make_packet' function that builds a FIN packet
*/
void buildFIN(char* packet, int seq){

  header_t header;

  header = (header_t){ .seq = seq, .msg_type = FIN, .nbytes = 0 };
  build_packet(packet, &header, 0, "");

}


/*
void build_packet(char* packet, header_t* header, ssize_t nbytes, char* buf)
takes:
- packet: the buffer into which the finished packet will be placed
- header: pointer to a header structure containing the relevant meta info
- nbytes: number of bytes to read from buf
- buf: buffer in which the file data is stored and from which it will be copied
into the packet buffer

creates a packet using the header and buffer structures
*/
void build_packet(char* packet, header_t* header, ssize_t nbytes, char* buf){

  memcpy(packet, header, sizeof(header_t));
  memcpy(packet + sizeof(header_t), buf, nbytes);
}



/*
void addr_init(struct sockaddr_in* server, char* iface, long port)
takes:
- server: empty structure that will store the address of the server to which
data will be sent
- iface: interface over which to communicate
- port: port number that communication will flow through

initializes a sockaddr_in structure for use
*/
void addr_init(struct sockaddr_in* server, char* iface, long port){

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
void extract_data(header_t* header, char* buf, char* pkt)
takes:
- header: pointer to an empty header structure
- buf: the buffer into which the packet's payload will be copied
- pkt: the received packet from which the header and data are to be extracted

extracts the header and the payload data of the passed packet for processing
*/
void extract_data(header_t* header, char* buf, char* pkt){

  memcpy(header, pkt, sizeof(header_t));
  if(header->nbytes != 0)
    memcpy(buf, pkt + sizeof(header_t), header->nbytes);
}




/*
void send_udt(int sock, struct sockaddr_in* dest, char* pkt, int nbytes)
takes:
- sock: socket descriptor through which data is to be sent
- dest: pointer to the sockaddr_in structure containing the destination address
- pkt: pointer to the data to be sent
- nbytes: number of bytes to be send through the socket (from the pkt buffer)

sends the passed data through the socket using UDP
*/
void send_udt(int sock, struct sockaddr_in* dest, char* pkt, int nbytes){

    int nsent;
    nsent = sendto(sock, pkt, nbytes, 0, (struct sockaddr*)dest,
                    sizeof(struct sockaddr));

    if(nsent < 0){
        perror("sendto");
        exit(EXIT_FAILURE);
    }
}


/*
void udp_recv(int sock, struct sockaddr_in* addr, char* buf)
takes:
- sock: socket file descriptor through which to receive data
- addr: empty pointer to sockaddr_in structure which will be filled with the
address of the host data has been received from
- buf: the buffer into which the received data will be placed 

reads an incoming packet and places the data into the given buffer
*/
void udp_recv(int sock, struct sockaddr_in* addr, char* buf){
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
void write_to_file(char* buffer, int nbytes, FILE* fp)
takes:
- buffer: buffer containing data to be written to the output file
- nbytes: number of bytes to be written from the buffer
- fp: pointer to the output file

writes nbytes from the buffer to the output file
*/
void write_to_file(char* buffer, int nbytes, FILE* fp){

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
void shutdown_server(int sock, struct sockaddr_in* client, int* seq){

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
  buildFIN(packet, *seq);
  send_udt(sock, client, packet, sizeof(header));

  // set timer
  timeout.tv_sec = TIMEOUT_SEC;
  timeout.tv_usec = TIMEOUT_USEC;

  // init timeout count
  n_timeouts = 0;

  // waiting ~30 sec for client ACK before shutting down
  while(n_timeouts < MAX_TIMEOUTS){

    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);

    can_read = wait_for_input(&read_set, &timeout);
    if(can_read > 0){

      // read and process
      udp_recv(sock, client, recvd_packet);
      extract_data(&header, file_data, recvd_packet);

      if(header.msg_type == ACK)
        break; 

    } else {

      // timeout occurred: resend FIN
      n_timeouts++;
      send_udt(sock, client, packet, sizeof(header));

      // reset timer
      timeout.tv_sec = TIMEOUT_SEC;
      timeout.tv_usec = TIMEOUT_USEC;

    }
  }

  close(sock);
}



/*
int get_dist(int start, int stop)
takes:
- start: starting value
- stop: end value

returns: the distance between stop and start for a circular buffer
*/
int get_dist(int start, int stop){

  if(start < stop)
    return stop - start;

  else if(start > stop)
    return stop + (WINMAX - start);

  else
    return 0;

}



/*
void get_avg_rtt(struct timeval* avg_rtt, struct timeval* time_sent)
takes:
- avg_rtt: empty timeval pointer to be filled with the value of the average rtt
- time_sent: timeval pointer the contains the time a packet was sent
*/
void get_avg_rtt(struct timeval* avg_rtt, struct timeval* time_sent){

  int alpha = .125;
  int usec = 1000000;
  struct timeval now, rtt, result1, result2;

  gettimeofday(&now, NULL);
  if(now.tv_usec < time_sent->tv_usec){
    now.tv_sec -= 1;
    now.tv_usec += usec;
  }

  rtt.tv_sec = now.tv_sec - time_sent->tv_sec;
  rtt.tv_usec = now.tv_usec - time_sent->tv_usec;

  // new_avg = alpha * latest_rtt + (1-alpha) * old_avg
  timeval_mult(&result1, &rtt, alpha);
  timeval_mult(&result2, avg_rtt, 1-alpha);

  // add result1 and result2 to get new avg
  avg_rtt->tv_sec = result1.tv_sec + result2.tv_sec + 
                    (int)((result1.tv_usec + result2.tv_usec) / usec);

  avg_rtt->tv_usec = (result1.tv_usec + result2.tv_usec) % usec;
}


/*
void timeval_mult(struct timeval* result, struct timeval* tv, int alpha)
takes:
- result: timeval structure to be filled with the result of the calculation
- tv: the timeval to be multiplied by the alpha value
- alpha: the constant by which to multiply the value of tv
*/
void timeval_mult(struct timeval* result, struct timeval* tv, int alpha){

  float temp;
  int usec = 1000000;
  struct timeval intermediate;

  temp = ( float )(alpha * tv->tv_sec);
  intermediate.tv_sec = ( int )temp;
  intermediate.tv_usec = ( int )(alpha * tv->tv_usec) + 
                          ( int )(temp * usec - intermediate.tv_sec * usec);

  result->tv_sec = intermediate.tv_sec + ( int )( intermediate.tv_usec / usec );
  result->tv_usec = intermediate.tv_usec % usec;
}
