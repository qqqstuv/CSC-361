#include <stdlib.h> 
#include <stdio.h>       
#include <sys/types.h>    
#include <string.h>      
#include <errno.h>
#include <assert.h>       
#include <unistd.h>       

#include <sys/socket.h>
#include <netinet/in.h>   
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include "rdpheader.h"


void logServer(int event_type, int packet_type, int number, int length){
	//Print time
	time_t rawtime;
 	struct tm * timeinfo; 
	time ( &rawtime );
	timeinfo = localtime ( &rawtime );
	char timeBuffer[80];
	strftime(timeBuffer,80,"%H:%M:%S",timeinfo);
	printf("%s ", timeBuffer);
	struct timeval tv;
	gettimeofday(&tv, NULL);
	printf("%06ld ",tv.tv_usec);

	char* event_type_char = NULL;
	switch(event_type){
		case 1: //send first time
			event_type_char = "Send      ";
			break; 
		case 2: // resend
			event_type_char = "Resend    ";
			break;
		case 3: // receive first time
			event_type_char = "Received  ";
			break;
		case 4: // receive again
			event_type_char = "Rereceived";
			break;
	}
	printf("%s ", event_type_char);
	//Port and stuff
	// printf("%s:%d ", global_sender_ip, global_sender_port );
	// printf("%s:%d ", global_receiver_ip, global_receiver_port);

	char* packet_type_char = NULL;
	switch(packet_type){
		case 1:
			packet_type_char = "DAT";
			break;
		case 2:
			packet_type_char = "ACK";
			break;
		case 3:
			packet_type_char = "SYN";
			break;
		case 4:
			packet_type_char = "FIN";
			break;
		case 5:
			packet_type_char = "RST";
			break;
	}
	printf("%s ", packet_type_char);

	printf("%d ", number);
	printf("%d\n", length);

}
int getSize(Node *queue){
	Node* head = queue;
	int size = 0;
	if (head == NULL){
		return size;
	}
	while(head != NULL){
		size++;
		head = head -> next;
	}
	return size;
}

// packet has already been malloc before
// timestamp it here too
Node* insert(Node* queue, packet_t* packet){
	struct Node *newNode= (struct Node*)malloc(sizeof(struct Node));
	newNode	-> packet 	= *packet;
	newNode -> next 	= NULL;
	gettimeofday(&(newNode->timeout), NULL); // Ask Issac why this works
	if(queue == NULL){
		// printf("Queue is null\n");
		return newNode;		
	}else{
		Node* head = queue;
		int size = 0;
		Node* target = head;
		while(head != NULL){
			// printf("sequence_num %d\n", head->packet.sequence_num);
			size++;
			target = head;
			head = head->next;
		}
		target->next = newNode;
		return queue;
	}
}

void updateAcknowledged(Node* queue, packet_t* packet, int acknowledgement_num, int* ack_up_to){
	Node* head = queue;
	while(head != NULL){
		if(acknowledgement_num  == head->packet.sequence_num){ //
			queue = head->next;
			*ack_up_to += *ack_up_to + head->packet.data_payload_length;
			break;
		}
		head = head->next;
	}
}

void debugPacket(packet_t* packet){
    printf("packet->type is %d ", packet->type);
    printf("packet->acknowledgement_num is %d ", packet->acknowledgement_num);
    printf("packet->magic is %s ", packet->magic);
    printf("packet->sequence_number is %d ", packet->sequence_num);
    printf("packet->data is %s\n", packet->data);
    printf("packet->data_payload_length %d\n", packet->data_payload_length);
}


packet_t* buffer_to_packet(char* buffer){
	packet_t* packet = calloc(1, sizeof(struct packet_t));
	// memcpy(packet, buffer, (MAX_PACKET_SIZE) * sizeof(char));
	memcpy(packet, buffer, sizeof(struct packet_t));
	return packet;
}

char* packet_to_buffer(packet_t* packet){
	char* buffer = calloc(1, sizeof(struct packet_t));
	memcpy(buffer, packet, sizeof(struct packet_t));
	return buffer;
}

void debugQueue(Node* queue){
	Node* head = queue;
	printf("Debugging Queue\n");
	while(head != NULL){
		printf("Seq num %d ", head->packet.sequence_num);
	}
}

// Send DAT packets to fill up given window
Node* send_full_queue(	int sock, struct sockaddr_in* sender_address, 
							struct sockaddr_in* receiver_address, 
							socklen_t receiver_address_size, 
							FILE* file, int* sequence_number, 
							Node* queue, 
							enum connection_states* connection_state){
	int num_of_sendable_packets = 10 - getSize(queue);
	// printf("Sendable packet numbers: %d\n",  num_of_sendable_packets);
	int packets_sent = 0;
	while(packets_sent < num_of_sendable_packets){
		// printf("Packet_sent # %d\n", packets_sent);
		int sequence_number_increment = 0; // how much to add to current sequence number 
		packet_t* packet = calloc(1, sizeof(packet_t)); // allocate memory for packet
		memset(packet->data,'\0', sizeof(char) * MAX_DATA_PAYLOAD_LENGTH); // allocate memory for data
		//read in MAX_DATA_PAYLOAD_LENGTH bytes of data
		sequence_number_increment = (int) fread(packet->data, sizeof(char), MAX_DATA_PAYLOAD_LENGTH, file); // get total number of bytes read
		if (sequence_number_increment == 0){ // no more data to send. Send FIN instead
			
			packet->type 				= 4;
			packet->sequence_num 		= *sequence_number; // current sequence_number
			packet->acknowledgement_num = 0;
			packet->data_payload_length = 1;
			packet->window_size			= 0;
			strcpy(packet->magic, "CSC361");
			*connection_state = EXIT;
			printf("CONNECTION STATE: EXIT\n");
			char* buffer = packet_to_buffer(packet);
			sendto(sock,buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*) receiver_address, receiver_address_size);
			free(buffer);
			logServer(1, 4, *sequence_number, 0);
			*sequence_number += 1;
			queue = insert(queue, packet);
			statistics.FIN_SENT++;
			statistics.total_data_packets_sent++;
			break;
		}else{ // Still some data left to send. Concat data
			packet->type 				= 1;
			packet->sequence_num 	 	= *sequence_number; // current sequence_number
			packet->acknowledgement_num = 0;
			packet->data_payload_length = sequence_number_increment; // how long the data is
			packet->window_size			= 0;
			strcpy(packet->magic, "CSC361");
			char* buffer = packet_to_buffer(packet);
			// debugPacket(packet);
			sendto(sock,buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*) receiver_address, receiver_address_size);
			free(buffer);
			logServer(1, 1, packet->sequence_num, packet->data_payload_length);
			*sequence_number += sequence_number_increment;
			packets_sent++;
			queue = insert(queue, packet);
			statistics.total_data_packets_sent++;
			statistics.unique_data_packets_sent++;
			statistics.total_data_bytes_sent += sequence_number_increment;
			statistics.unique_data_bytes_sent += sequence_number_increment;
			// printf("Queue size after sending new data is %d\n", getSize(queue));
		}
	}
	return queue;
}

// to be used only by the receiver side
void send_ACK_packet(int sock, struct sockaddr_in* receiver_address, 
						struct sockaddr_in* sender_address, socklen_t sender_address_size, 
						int sequence_number, int window_size){
	packet_t packet;
	strcpy(packet.magic, "CSC361");
	packet.window_size			= 0;
	packet.type 				= 2;
	packet.acknowledgement_num 	= sequence_number;
	packet.data_payload_length 	= 0; // how long the data is
	packet.sequence_num 	 	= 0;	
	char* buffer = packet_to_buffer(&packet);
	sendto(sock, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*) sender_address, sender_address_size);
	free(buffer);
	logServer(1,2, sequence_number, 0);
	statistics.ACK_RECEIVED++;
	return;
}

//(sock, &receiver_address, receiver_address_size, &sender_address)
// Send syn packet to receiver and return seq
int send_SYN_packet(int socket, struct sockaddr_in* receiver_address, 
	socklen_t receiver_address_size, struct sockaddr_in* sender_address){
	int sequence_number = rand();
	packet_t packet;
	strcpy(packet.magic, "CSC361");
	packet.type 					= 3;
	packet.acknowledgement_num	 	= 0; // ignore this even
	packet.data_payload_length 	 	= 0; // empty data
	packet.sequence_num 			= sequence_number;
	packet.window_size 				= 0; // dont know yet
	char* buffer = packet_to_buffer(&packet);
	// printf("Sending buffer is %s\n", buffer);
	sendto(socket,buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*) receiver_address, receiver_address_size);
	free(buffer);
	logServer(1, 3, sequence_number, 0);
	return sequence_number;
}


// Send again. Used by sender only
void send_FIN_packet(int sock, struct sockaddr_in* sender_address, 
	struct sockaddr_in* receiver_address, socklen_t receiver_address_size, 
	int sequence_number, int window_size){
	packet_t packet;
	strcpy(packet.magic, "CSC361");
	packet.type 				= 4;
	packet.sequence_num 		= sequence_number;
	packet.acknowledgement_num 	= 0;
	packet.data_payload_length 	= 0; // how long the data is
	packet.window_size			= 0;
	// packet.data 				= NULL;
	char* buffer = packet_to_buffer(&packet);
	sendto(sock, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*) receiver_address, receiver_address_size);
	free(buffer);
	logServer(2, 4, sequence_number, 0);
	statistics.FIN_SENT++;
	return;
}


// Used by sender to resend a packet. Stats is included
Node* resend_packet(int sock, struct sockaddr_in* receiver_address, socklen_t receiver_address_size, 
					packet_t* packet, Node* queue){
	char*buffer = packet_to_buffer(packet);
	sendto(sock, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*) receiver_address, receiver_address_size);
	free(buffer);
	queue = insert(queue, packet);
	logServer(2,packet->type,packet->sequence_num, packet->data_payload_length);
	statistics.total_data_bytes_sent += packet->data_payload_length;
	statistics.total_data_packets_sent += 1;
	// printf("Queue size after resend is %d\n", getSize(queue));
	return queue;
}

// http://www.gnu.org/software/libc/manual/html_node/Elapsed-Time.html
int timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y){
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}

// Find expired packet (the first) then remove it and return the data
// Make the queue points to the next element in the queue. Might be null.
packet_t* find_expire_packet(Node** queue){
	Node* head = *queue;
	if (head == NULL){
		return NULL;
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	struct timeval elapsedTime;
	// long int elapsedTime = (now.tv_sec - head->timeout.tv_sec) / 1000;
	// elapsedTime += (now.tv_usec - head->timeout.tv_usec) * 1000;
	int negative = timeval_subtract (&elapsedTime, &now, &(head->timeout));
	if (negative){
		printf("Shouldn't be\n");
	}
	int toTime = elapsedTime.tv_sec * 1000 + elapsedTime.tv_usec / 1000;
	if (toTime > CONNECTION_TIMEOUT) {// take element out of queue, return element
		*queue = head->next;
		packet_t* packet = &(head -> packet);
		// printf(">%ld.%06ld< >%d<", elapsedTime.tv_sec, elapsedTime.tv_usec, toTime);
		return packet;
	}else{
		// printf(">%ld.%06ld< >%d<\n", elapsedTime.tv_sec, elapsedTime.tv_usec, toTime);
		return NULL;
	}
}
// Remove potentially resent packets that have already been acknowledged
Node* remove_acknowledged_packet(packet_t* acknowledged_packet, Node** queue){
	Node* head = *queue;
	Node* prev = NULL;
	Node* target = head;
	while(target != NULL){
  		if(		target->packet.sequence_num + 
  				target->packet.data_payload_length
  			< acknowledged_packet->acknowledgement_num){ 
  			//If the packet can be removed from linkedlist because it's too old
  			if (prev == NULL){
  				Node* freeNode = target;
  				target = target->next;
  				head = target;
  				free(freeNode);
  			}else{
	  			prev -> next = target -> next;
	  			free(target);
	  			target = prev -> next;
  			}
  			// logServer(4, 2, acknowledged_packet->acknowledgement_num, acknowledged_packet->data_payload_length);
  		}else if(	target->packet.sequence_num + 
  					target->packet.data_payload_length
  		  		== 	acknowledged_packet->acknowledgement_num){ 
  				//If the packet is the one to be removed from linkedlist
  			if (prev == NULL){
  				Node* freeNode = target;
  				target = target->next;
  				head = target;
  				free(freeNode);
  			}else{
	  			prev -> next = target -> next;
	  			free(target);
	  			target = prev -> next;
  			}
  			// logServer(3, 2, acknowledged_packet->acknowledgement_num, acknowledged_packet->data_payload_length);
  		}else{
  			prev = target;
  			target = target->next;
  		}
  	}
  	// printf("Size of queue after acknowledged is %d\n", getSize(head) );
  	return head;
}

Node* remove_acknowledged_packet4(packet_t* acknowledged_packet, Node** queue){
	if (*queue == NULL){
		return NULL;
	}
	// Node* head = remove_resent_packet(acknowledged_packet, *queue);
	Node* head = *queue; 
	if (	head->packet.sequence_num + 
  			head->packet.data_payload_length
  	 	> 	acknowledged_packet->acknowledgement_num){
		// Got something from the past. Ignore it
	  	logServer(4, 2, acknowledged_packet->acknowledgement_num, acknowledged_packet->data_payload_length);
	  	return head;
  	}
  	if(		head->packet.sequence_num + 
  			head->packet.data_payload_length
  		 == acknowledged_packet->acknowledgement_num){
  		// If the head is the one
    	Node* freeNode = head;
    	head = head->next;
    	free(freeNode);
	  	// logServer(3, 2, acknowledged_packet->acknowledgement_num, acknowledged_packet->data_payload_length);    	
    	return head;
  	}
  	head = head->next; // get the second element
  	while(head != NULL){
  		if(		head->packet.sequence_num + 
  				head->packet.data_payload_length
  			 == acknowledged_packet->acknowledgement_num){
  			// Iterate to the next biggest item
  			Node* freeNode = head;
  			head = head -> next;
  			free(freeNode);
  			logServer(3, 2, acknowledged_packet->acknowledgement_num, acknowledged_packet->data_payload_length);    	
  			// printf("Improper Delete. queue size is %d\n", getSize(head));
  			return head;
  		}else{
  			Node* freeNode = head;
  			head = head->next;
  			free(freeNode);
  		}
  	}
  	printf("Should never reached here. Size of queue is %d\n", getSize(head));
  	return head;
}

Node* remove_acknowledged_packet3(packet_t* acknowledged_packet, Node** queue){
	if (*queue == NULL){
		return NULL;
	}
	Node* head = *queue;
	if (	head->packet.sequence_num + 
  			head->packet.data_payload_length
  	 	> 	acknowledged_packet->acknowledgement_num){
		// Got something from the past. Ignore it
	  	logServer(4, 2, acknowledged_packet->acknowledgement_num, acknowledged_packet->data_payload_length);
	  	return head;
  	}
  	if(		head->packet.sequence_num + 
  			head->packet.data_payload_length
  		 == acknowledged_packet->acknowledgement_num){
  		// If the head is the one
    	Node* freeNode = head;
    	head = head->next;
    	free(freeNode);
	  	logServer(3, 2, acknowledged_packet->acknowledgement_num, acknowledged_packet->data_payload_length);    	
    	return head;
  	}
  	head = head->next; // get the second element
  	while(head != NULL){
  		if(		head->packet.sequence_num + 
  				head->packet.data_payload_length
  			 == acknowledged_packet->acknowledgement_num){
  			// Iterate to the next biggest item
  			Node* freeNode = head;
  			head = head -> next;
  			free(freeNode);
  			logServer(3, 2, acknowledged_packet->acknowledgement_num, acknowledged_packet->data_payload_length);    	
  			// printf("Improper Delete. queue size is %d\n", getSize(head));
  			return head;
  		}else{
  			Node* freeNode = head;
  			head = head->next;
  			free(freeNode);
  		}
  	}
  	printf("Should never reached here. Size of queue is %d\n", getSize(head));
  	return head;
}


Node* remove_acknowledged_packet2(packet_t* acknowledged_packet, Node** queue){
  // printf("Size of queue before removing pkt is %d\n", getSize(*queue));
  if(*queue == NULL){
  	return NULL;
  }
  Node* head = *queue;
  Node* current = head;
  printf("Remove acknowledgement_num is %d ",acknowledged_packet->acknowledgement_num );
  if (	current->packet.sequence_num + 
  		current->packet.data_payload_length
  	 > 	acknowledged_packet->acknowledgement_num){
  	printf("Got something from the past. Ignore it\n");
  	return head;
  }
  if (  current->packet.sequence_num + 
  		current->packet.data_payload_length
  	 == acknowledged_packet->acknowledgement_num){ // ack packet is the earliest one -> proper. Just gotta remove it
    head = current->next; // set the head to the next one in the queue
    // free(&packet);
    free(current);
    printf("Remove normally\n");
    return head;
  } else { //Head is not what we look for.
  	printf("Remove abnormally. ACK comes not in order\n");
  	Node* target = current->next;
  	free(current); // free the Head
  	while(target != NULL){
	    if (target->packet.sequence_num +
	    	target->packet.data_payload_length
	     == acknowledged_packet->acknowledgement_num){
	        current->next = target->next; 
	        free(target);
	      	target = current; // traverse next
	      	break;
	    }else{
			current->next = target->next; //change pointer
	      	target = current; // traverse next	
	    }
  	}
  	fflush(stdout);
  	return current;
  }
  // printf("Size of queue after removing ack is %d\n", getSize(current));
}

void write_packet_to_file(packet_t* packet, FILE* file, int* acknowledged_up_to){
	fputs(packet->data, file);
	*acknowledged_up_to = *acknowledged_up_to + packet->data_payload_length;
}

void exit_unsuccessful(int side){
	stats(side);
	exit(-1);
}

void exit_successful(int side){
	stats(side);
	exit(0);
}
// 0 is sender, 1 is receiver
void stats(int side){
	struct timeval now;
	gettimeofday(&now, NULL);
	gettimeofday(&now, NULL);
	struct timeval elapsedTime;
	elapsedTime.tv_sec = (now.tv_sec - duration.tv_sec);
	elapsedTime.tv_usec = abs((now.tv_usec - duration.tv_usec));
	if (side == 0){
    	printf(
    		"total data bytes send: %d\n"
            "unique data bytes sent: %d\n"
            "total data packets sent: %d\n"
            "unique data packets sent: %d\n"
            "SYN packets sent: %d\n"
            "FIN packets sent: %d\n"
            "RST packets send: %d\n"
            "ACK packets received: %d\n"
            "RST packets received: %d\n"
            "total time duration (second): %d.%d\n",
             statistics.total_data_bytes_sent,
             statistics.unique_data_bytes_sent,
             statistics.total_data_packets_sent,
             statistics.unique_data_packets_sent,
             statistics.SYN_SENT,
             statistics.FIN_SENT,
             statistics.RST_SENT,
             statistics.ACK_RECEIVED,
             statistics.RST_RECEIVED,
             (int) elapsedTime.tv_sec,
             (int) elapsedTime.tv_usec);
	}else{
		printf(
			"total data bytes received: %d\n"
            "unique data bytes received: %d\n"
            "total data packets received: %d\n"
            "unique data packets received: %d\n"
            "SYN packets received: %d\n"
            "FIN packets received: %d\n"
            "RST packets received: %d\n"
            "ACK packets sent: %d\n"
            "RST packets sent: %d\n"
            "total time duration (second): %d.%d\n",
             statistics.total_data_bytes_sent,
             statistics.unique_data_bytes_sent,
             statistics.total_data_packets_sent,
             statistics.unique_data_packets_sent,
             statistics.SYN_SENT,
             statistics.FIN_SENT,
             statistics.RST_SENT,
             statistics.ACK_RECEIVED,
             statistics.RST_RECEIVED,
             (int) elapsedTime.tv_sec,
             (int) elapsedTime.tv_usec);
	}
}