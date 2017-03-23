#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h> /* for close() for socket */ 
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "rdpheader.h"

char* global_receiver_ip;
int global_receiver_port;

char* global_sender_ip;
int global_sender_port;

statistics_t statistics;

struct timeval duration;

int main(int argc, char *argv[]){
  // handle invoking call
  if (argc != 6){
    // Sample: rdps sender_ip sender_port receiver_ip receiver_port sender_file_name
    fprintf(stderr, "%s\n", "Usage: ./rdps sender_ip sender_port receiver_ip receiver_port sender_file_name");
    exit(EXIT_FAILURE);
  }else{
    char* sender_ip = argv[1];
    global_sender_ip = sender_ip;
    int sender_port = atoi(argv[2]);
    global_sender_port = sender_port;
    char* receiver_ip = argv[3];
    global_receiver_ip = receiver_ip;
    int receiver_port = atoi(argv[4]);
    global_receiver_port = receiver_port;
    char* sender_file_name = argv[5];
    FILE* file = fopen(sender_file_name, "r");
    // printf("rdps is running on RDP with sender_ip %s, sender_port %d, receiver_ip %s, receiver_port %d and serving %s\n",sender_ip, sender_port, receiver_ip, receiver_port, sender_file_name);
    fflush(stdout);
    // setting up socket
    int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    // socket for sender
    struct sockaddr_in sender_address;
    // socklen_t sender_address_size = sizeof(sender_address); // MAYBE: check back sizeof of this
    memset(&sender_address, 0, sizeof (sender_address));
    sender_address.sin_family = AF_INET;
    sender_address.sin_addr.s_addr = inet_addr(sender_ip); // MAYBE: makes integer from pointer without a cast
    sender_address.sin_port = sender_port;
    // socket for receiver
    struct sockaddr_in receiver_address;
    socklen_t receiver_address_size = sizeof(receiver_address);
    memset(&receiver_address, 0, sizeof (receiver_address));
    receiver_address.sin_family = AF_INET;
    receiver_address.sin_addr.s_addr = inet_addr(receiver_ip);
    receiver_address.sin_port = receiver_port;
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*) &opt, sizeof(opt)) == -1){
      fprintf(stderr,"Set Socket Option Failed\n"); 
      close(sock);
      exit(EXIT_FAILURE);
    }
    // bind to socket
    if (-1 == bind(sock, (struct sockaddr *)&sender_address, sizeof sender_address)) {
      perror("Error bind failed");
      close(sock);
      exit(EXIT_FAILURE);
    }
    // Sender side
    enum connection_states connection_state = HANDSHAKE;
    fcntl(sock, F_SETFL, O_NONBLOCK); // set to non-blocking
    // Set up seq num
    int init_seqnum = 0;
    int system_seqnum = init_seqnum;

    int acked_up_to;
    // Set up buffer
    char *buffer = calloc(MAX_PACKET_SIZE + 1, sizeof(char));
    packet_t* packet = NULL;
    // Sender logic
    Node* queue = NULL;
    gettimeofday(&duration, NULL);

    while (connection_state == HANDSHAKE) {
      // Get something from the socket
      // printf("Waiting\n");
      fflush(stdout); 
      int recsize = recvfrom(sock, (void*)buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*)&receiver_address, &receiver_address_size); //Maybe: fix this warning
      if (recsize <= 0) { // Cannot receive SYN packet
        statistics.SYN_SENT++; // increment SYN num
        system_seqnum = send_SYN_packet(sock, &receiver_address, receiver_address_size, &sender_address); // Send SYN packet and set init random seq num
        sleep(1);
      } else{ // TRANSFER STATE
        fflush(stdout);
        connection_state = TRANSFER;
        packet = buffer_to_packet(buffer);

        if(packet->type == 2 && 
          packet->acknowledgement_num == system_seqnum){ // if it's an acknowledgement
          statistics.ACK_RECEIVED++; // incr ack
          logServer(3, 2, packet->acknowledgement_num, 0);
          queue = send_full_queue(sock, &sender_address, 
                            &receiver_address, receiver_address_size, 
                            file, &system_seqnum, 
                            queue, &connection_state);
          acked_up_to = system_seqnum;
          fflush(stdout);
        }else{
          fflush(stdout);
        }
        free(packet);
      }
    }
    packet = NULL;
    // Done with handshaking. Start handling sending data
    while(1) {
      fflush(stdout);
      while(packet == NULL){ // main loop
        memset(buffer, '\0', MAX_PACKET_SIZE + 1); // reset buffer
        int recsize = recvfrom(sock, (void*)buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*)&receiver_address, &receiver_address_size); //Maybe: fix this warning
        if (recsize == - 1) { // nothing has arrived yet
          packet = find_expire_packet(&queue); // loop through the linked list timeout_queue and get a packet that timed out
          if (packet != NULL){
            // printf("Found expired packet\n");
          }
        } else{ // got something from the receiver
          packet = buffer_to_packet(buffer);
          if(packet->type == 1){ // Getting data from the buffer
            fclose(file);
            exit_unsuccessful(0);
          }
        }
      }
      if (packet->type == 2){ // ACK. should be from the receiver only 
        statistics.ACK_RECEIVED++;
        if (acked_up_to < packet->acknowledgement_num){
          acked_up_to = packet->acknowledgement_num;
          logServer(3, 2, packet->acknowledgement_num, packet->data_payload_length);
        }else{
          logServer(4, 2, packet->acknowledgement_num, packet->data_payload_length);
        }
        switch(connection_state){
          case TRANSFER:
            // drop the packet from timers
            queue = remove_acknowledged_packet(packet, &queue);
            // send new packets to fill in window
            if (getSize(queue) == 0){
              queue = send_full_queue(sock, &sender_address,
                                &receiver_address, 
                                receiver_address_size,
                                file, &system_seqnum, queue, &connection_state);
            }
            break;
          case RESET:
            break;
          case EXIT: //Final state. Can only reach here if there is no data to send 
            queue = remove_acknowledged_packet(packet, &queue);
            if (getSize(queue) == 0){ // All data has been received
              fclose(file);
              printf("EXIT HERE\n");
              exit_successful(0);
            }
            break;
          default:
            printf("Why reached here?\n");
            break;        
        }
      } else if (packet->type == 1){ //DAT Coming from the timeout queue thingy
        // if (packet->sequence_num + packet->data_payload_length >= acked_up_to){
          queue = resend_packet(sock, &receiver_address, receiver_address_size, 
                          packet, queue);          
        // }
      } else if (packet->type == 5){ // RST
        connection_state = RESET;
        fclose(file);
        printf("Getting reset from sender\n");
        exit_unsuccessful(0);
      } else if (packet->type == 4){ // FIN Coming from timeout queue in EXIT STATE
        // if (getSize(queue) == 0){
          // fclose(file);
          // printf("EXIT THERE\n");
          // exit_successful(0);
          // send_FIN_packet(sock, &sender_address, &receiver_address, 
          //         receiver_address_size, system_seqnum, 0);
          queue = resend_packet(sock, &receiver_address, receiver_address_size, 
                packet, queue);
        // }
        // send_FIN_packet(sock, &sender_address, &receiver_address, 
        //         receiver_address_size, system_seqnum, 0);
      } else { // Invalid packet type
      }
      packet = NULL; //reset packetset
    }
  }
  return 0;
}