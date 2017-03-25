Duc Nguyen
V00849182
CSC 361 Assignment 2

* Instruction to run the program:
	- run "make" to compile all files
	- run "make runrl" to start the receiver side, "make runsl" to start the sender side
	- after the program terminates, the loggings with be in slog.txt and rlog.txt
	- run "make isvalid" to check the validity of the file transfered.


* Implementation specification

CONNECTION MANAGEMENT:

On starting, the sender will send a SYN packet after every fixed amount of time (1 second). Every packet sent will be converted into a string buffer. On the receiver side, the receiver will convert the buffer back to a packet struct, get the ACK number, and send back ACK packet.

Upon reaching the end of the file, the sender will send a FIN packet after the last data packet. The sender either wait for the ACK packet from the receiver or resend the FIN packet if the timer runs out. When the sender receives the final ACK, it will close the connection and exit. On the receiver side, when the receiver receives the FIN packet, it will send back an ACK packet and wait for more than one round trip. If after one round trip it does not receive any FIN packet, it will close the connection and exit.

SENDING DATA IMPLEMENTATION (Including flow control and error control):

- The receiver side uses a GBN method and therefore have a window size of 1. Whenever a packet arrives out of order, the receiver will drop that packet and send back the ACK number that it expects. Because of this approach, the receiver always have a fixed window size of 1.

- The sender side uses cumulative acknowledgement approach and has an initial window size of 10. Initially, it will send 10 packets, timestamp each packet and put every packets into a queue. 
After this, these are the cases for the sender:
	- sender receives ACK packet from the receiver, verifies it, delete the corresponding packets from the queue, add new packets to the queue and send it.
	- sender receives an ACK packet that is in the pass, it will discard it.
	- sender loops through the queue to look for any timed out packet. If there is, it will resend, timestamp and put the packet in the queue again.

In order to ensure reliable data transfer, I keep every sent packet in a linked list and only send a new packet when I got a proper ACK packet back.

* Files I used

rdps.c: contains sender's logic
rdpr.c: contains receiver's logic
rdpheader.h: contains mutually defined function headers and structs used by both sides
sendlogic.c: contains mutually defined functions used by both sides