#include <stdlib.h>       // Builtin functions
#include <stdio.h>        // Standard IO.
#include <sys/types.h>    // Defines data types used in system calls.
#include <string.h>       // String functions.
#include <errno.h>
#include <assert.h>       // Needed for asserts.
#include <unistd.h>       // Sleep
#include <pthread.h>      // Threads! =D!

#include <sys/socket.h>   // Defines const/structs we need for sockets.
#include <netinet/in.h>   // Defines const/structs we need for internet domain addresses.
#include <arpa/inet.h>
#include <sys/time.h> 		// for the timer

#define MAX_PACKET_SIZE 1024
#define MAX_DATA_PAYLOAD_LENGTH 900
#define MAGIC_SIZE 7
#define TYPE_SIZE 3
#define MAX_WINDOW_SIZE_UNIT 10
#define CONNECTION_TIMEOUT 70	

extern char* global_receiver_ip;
extern int global_receiver_port;

extern char* global_sender_ip;
extern int global_sender_port;

extern struct timeval duration;


enum connection_states {
  HANDSHAKE,
  TRANSFER,
  RESET,
  EXIT
};

// enum packet_type {
//   DAT, 1
//   ACK, 2
//   SYN, 3
//   FIN, 4
//   RST 5
// };

typedef struct packet_t {
	char magic[MAGIC_SIZE];
	int type;
	int sequence_num;
	int acknowledgement_num;
	int window_size;	
	int data_payload_length;
	char data[900];
} packet_t;

typedef struct Node{
	packet_t packet;
	struct Node* next;
	struct timeval timeout;
} Node;

// packet has already been malloc before
// timestamp it here too
Node* insert(Node* queue, packet_t* packet);

void updateAcknowledged(Node* queue, packet_t* packet, int acknowledgement_num, int* ack_up_to);

int getSize(Node *queue);

/*
101 total data bytes sent: 1165152
102 unique data bytes sent: 1048576
103 total data packets sent: 1166
104 unique data packets sent: 1049
105 SYN packets sent: 1
106 FIN packets sent: 1
107 RST packets sent: 0
108 ACK packets received: 1051
109 RST packets received: 0
110 total time duration (second): 0.093
*/
typedef struct {
	int SYN_SENT;
	int FIN_SENT;
	int RST_SENT;
	int ACK_RECEIVED;
	int RST_RECEIVED;
	unsigned int total_data_bytes_sent;
	unsigned int unique_data_bytes_sent;
	int total_data_packets_sent;
	int unique_data_packets_sent;
} statistics_t;

extern statistics_t statistics;
int send_SYN_packet(int socket, struct sockaddr_in* receiver_address, socklen_t receiver_address_size, struct sockaddr_in* sender_address);
packet_t* buffer_to_packet(char* buffer);
char* packet_to_buffer(packet_t* packet);
Node* send_full_queue(	int sock, struct sockaddr_in* sender_address, 
							struct sockaddr_in* receiver_address, 
							socklen_t receiver_address_size, 
							FILE* file, int* sequence_number, 
							Node* queue, 
							enum connection_states* connection_state);
void send_FIN_packet(int sock, struct sockaddr_in* sender_address, struct sockaddr_in* receiver_address, socklen_t receiver_address_size, int sequence_number, int window_size);
Node* resend_packet(int sock, struct sockaddr_in* receiver_address, socklen_t receiver_address_size, 
					packet_t* packet, Node* queue);
packet_t* find_expire_packet(Node** queue);

Node* remove_acknowledged_packet(packet_t* acknowledged_packet, Node** queue);

void write_packet_to_file(packet_t* packet, FILE* file, int* acknowledged_up_to);
void send_ACK_packet(int sock, struct sockaddr_in* receiver_address, 
						struct sockaddr_in* sender_address, socklen_t sender_address_size, 
						int sequence_number, int window_size);
void debugPacket(packet_t* packet);

void logServer(int event_type, int packet_type, int number, int length);

void stats(int side);

void exit_unsuccessful(int side);

void exit_successful(int side);

int timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y);