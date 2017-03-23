#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "rdpheader.h"

// UDP Client

char* global_receiver_ip;
int global_receiver_port;

char* global_sender_ip;
int global_sender_port;

statistics_t statistics;
struct timeval duration;

int main(int argc, char* agrv[]){
  if(argc != 4){
    printf("rdpr receiver_ip receiver_port receiver_file_name \n");
    exit(-1);
  }
  char* receiver_ip = agrv[1];
  global_receiver_ip = agrv[1];
  int receiver_port = atoi(agrv[2]);
  global_receiver_port = receiver_port;
  char* file_name = agrv[3];
  FILE* file = fopen(file_name, "w");

  // Set up socket
  int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
 
  /* create an Internet, datagram, socket using UDP */

  struct sockaddr_in receiver_address;
  receiver_address.sin_family       = AF_INET;
  receiver_address.sin_port         = receiver_port;
  receiver_address.sin_addr.s_addr = inet_addr(receiver_ip);

  struct sockaddr_in sender_address;
  socklen_t sender_address_size   = sizeof(struct sockaddr_in);

  if (-1 == sock){
      /* if socket failed to initialize, exit */
      printf("Error Creating Socket");
      exit(-1);
  }
  int socket_ops = 1;
  if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &socket_ops, sizeof(socket_ops)) < 0){
    printf("Couldn't set socket\n");
    exit(-1);
  }
  if(bind(sock, (struct sockaddr*) &receiver_address, sizeof(receiver_address)) < 0 ){
    printf("Couldn't bind to socket\n");
    exit(-1);
  }

  char* buffer = calloc(MAX_PACKET_SIZE + 1, sizeof(char));

  int acknowledged_up_to;

  srand(time(NULL)); // set random timer for seq num later
  gettimeofday(&duration, NULL);
  for (;;) {
    memset(buffer, '\0', MAX_PACKET_SIZE); // reset memory of buffer
    packet_t* packet;
    int recsize = recvfrom(sock, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*) &sender_address, &sender_address_size); // This socket is blocking.
    global_sender_ip = inet_ntoa(sender_address.sin_addr);
    global_sender_port = (int) ntohs(sender_address.sin_port);
    if (recsize == - 1){
      printf("Receiving error\n");
    }
    // printf("recsize: %d\n", recsize);
    packet = buffer_to_packet(buffer);
    if(packet == NULL){
      printf("Packet is corrupted\n");
    } 
    statistics.total_data_packets_sent++;
    switch(packet->type){
      case 3: // SYN
        acknowledged_up_to = packet->sequence_num;
        logServer(3, 3, acknowledged_up_to, 0); // receive SYN
        send_ACK_packet(sock, &receiver_address, 
                      &sender_address, sender_address_size, 
                      acknowledged_up_to, 0);
        statistics.SYN_SENT++;
        break;
      case 2: //ACK
        printf("This is impossible.\n");
        exit_unsuccessful(1);
        break;
      case 1: // DAT
        statistics.total_data_bytes_sent += packet->data_payload_length;
        if(packet->sequence_num == acknowledged_up_to){ // Write to file
          logServer(3, 1, packet->sequence_num, packet->data_payload_length);
          write_packet_to_file(packet, file, &acknowledged_up_to);
          statistics.unique_data_bytes_sent += packet->data_payload_length;
          statistics.unique_data_packets_sent++;
        }else if(packet->sequence_num < acknowledged_up_to){ // send something in the past
          printf("Lower than expected. Drop packet with seq %d\n", packet->sequence_num);
          logServer(4, 1, packet->sequence_num, packet->data_payload_length);
        } else {//seq greater than expected
          logServer(4, 1, packet->sequence_num, packet->data_payload_length);
          printf("Higher than expected. Drop packet with seq %d\n",packet->sequence_num);
        }
        send_ACK_packet(sock, &receiver_address,
                       &sender_address, sender_address_size, 
                       acknowledged_up_to, 0);
        break;
      case 5: //RST
        statistics.RST_RECEIVED++;
        printf("Got a reset\n");
        exit_unsuccessful(1);
        break;
      case 4: // FIN
        if (packet->sequence_num == acknowledged_up_to){// If FIN is the next packet
          fcntl(sock, F_SETFL, O_NONBLOCK); // set to non-blocking
          struct timeval timeout;
          gettimeofday(&timeout, NULL);
          struct timeval now;
          int firstFin = 0;
          statistics.FIN_SENT++;
          acknowledged_up_to += packet->data_payload_length;
          for(;;){
            int recsize = recvfrom(sock, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*) &sender_address, &sender_address_size);
            if(recsize <= 0){ // Havent got anything
              gettimeofday(&now, NULL);
              struct timeval elapsedTime;
              int negative = timeval_subtract(&elapsedTime, &now, &timeout);
              if (negative){
                printf("Shouldn't be\n");
              }
              int toTime = elapsedTime.tv_sec * 1000 + elapsedTime.tv_usec / 1000;
              if (toTime > CONNECTION_TIMEOUT) {//havent got anything, timed out
                fclose(file);
                close(sock);
                free(packet);
                stats(1);
                return 0;
              }else{//Keep waiting
                continue;
              }
            }else{// Probably got a FIN. Gonna send back an ACK
              if (firstFin == 0){
                logServer(3, packet->type, packet->sequence_num, 0);
                firstFin++;
              }else{
                logServer(4, packet->type, packet->sequence_num, 0);
              }
              statistics.FIN_SENT++;
              send_ACK_packet(sock, &receiver_address, &sender_address, 
                            sender_address_size, acknowledged_up_to, 0);
              if(packet->type == 4){
                gettimeofday(&timeout, NULL); // reset timer
              }
            }
          }
        }else{
          printf("Got FIN but still need data %d  %d\n", packet->sequence_num, acknowledged_up_to );
          statistics.FIN_SENT++;
          send_ACK_packet(sock, &receiver_address,
                 &sender_address, sender_address_size, 
                 acknowledged_up_to, 0);
        }
        fflush(stdout);

        break;
    }
    free(packet);
  }
  close(sock); /* close the socket */
  stats(1);
  return 0;
}

