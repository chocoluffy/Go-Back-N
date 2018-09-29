#include "gbn.h"

uint16_t checksum(uint16_t *buf, int nwords)
{
	uint32_t sum;

	for (sum = 0; nwords > 0; nwords--)
		sum += *buf++;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	return ~sum;
}

ssize_t gbn_send(int sockfd, const void *buf, size_t len, int flags){
	
	/* TODO: Your code here. */

	/* Hint: Check the data length field 'len'.
	 *       If it is > DATALEN, you will have to split the data
	 *       up into multiple packets - you don't have to worry
	 *       about getting more than N * DATALEN.
	 */
	// int segment_num = len / DATALEN + 1;
	// void* segment_ptr;
	int retval = (int) sendto(sockfd, buf, len, 0, s.addr, s.addrlen);
	if (retval < 0) {
		perror("sendto in gbn_send()");
		exit(-1);
	}
	printf("expect to send %d, actually send %d\n", len, retval);
	// for (int i = 0; i < segment_num; i++) {
	// 	segment_ptr = buf + i * DATALEN;
	// 	int retval = (int) sendto(sockfd, segment_ptr, DATALEN, 0, addr_book.server_addr, addr_book.serveraddrlen);
	// }
	// printf("Client sends all segments, total: %d segments.\n", segment_num);
	return(0);
}

ssize_t gbn_recv(int sockfd, void *buf, size_t len, int flags){
	/**
	 * buf.type can be:
	 * - SYN
	 * - FIN
	 * - DATA
	 * 
	 * any type can be corrupted.
	 * if it's a DATA packet, check sequence number to see if there is packet lost.
	 */
	struct sockaddr_in from_addr;
	int from_len = sizeof(from_addr);
	int retval = (int) recvfrom(sockfd, buf, len, flags, &from_addr, &from_len);	
	if (retval < 0) {
		perror("recvfrom in gbn_recv()");
		exit(-1);
	}
	printf("Server receives segment of size: %d. \n", retval);
	return(retval);
}

int gbn_close(int sockfd){

	/* TODO: Your code here. */
	return close(sockfd);
	/*  return(-1); */
}

int gbn_connect(int sockfd, const struct sockaddr *server, socklen_t socklen){

	/* only break when connection is established (SYNACK received). */
	while (1) {
		/**
		 * init state.
		 * 
		 * seq_num = random()
		 * ack_num = -1
		 * data_len = 0
		 * checksum = -1
		 * mode = 0
		 * 
		 * send SYN to the server.
		 * receive SYNACK.
		 */
		s.seq_num = rand() % 100;
		s.ack_num = -1;
		s.data_len = 0;
		s.mode = 0;

		/* construct current syn packet. */
		gbnhdr syn_segment;
		syn_segment.type = SYN;
		syn_segment.seqnum = s.seq_num;
		syn_segment.acknum = s.ack_num;
		
		int retval = (int) sendto(sockfd, &syn_segment, sizeof(syn_segment), 0, server, socklen);
		s.addr = (struct sockaddr *) server;
		s.addrlen = socklen;
		printf("successfully send SYN packet to server side.\n");

		/* update state(prev state) */
		s.segment = syn_segment;
		
		/* wait for SYNACK. 
		* - timeout: repeat connect.
		* - successfully receive SYNACK, move to next state.
		*/
		while (1) {
			gbnhdr buf;
			struct sockaddr_in from_addr;
			int from_len = sizeof(from_addr);
			int retval = recvfrom(sockfd, &buf, sizeof(buf), 0, &from_addr, from_len);
			if (buf.type == SYNACK) {
				printf("successfully received SYNACK. \n");
				return(0);
			} 
		}
	}
}

int gbn_listen(int sockfd, int backlog){

	/* TODO: Your code here. */
	/* return listen(sockfd,  backlog); */
	return 1;
	/* return(-1); */
}

int gbn_bind(int sockfd, const struct sockaddr *server, socklen_t socklen){

	/* TODO: Your code here. */
	return bind(sockfd, server, socklen);
	/* return(-1); */
}	

int gbn_socket(int domain, int type, int protocol){
		
	/*----- Randomizing the seed. This is used by the rand() function -----*/
	srand((unsigned)time(0));
	
	/* TODO: Your code here. */
	return socket(domain,type,protocol);
	/* return(-1); */
}

int gbn_accept(int sockfd, struct sockaddr *client, socklen_t *socklen){
	gbnhdr buf;
	/* TODO: Your code here. */
	while(1) {
		/* [1] call recvfrom. to get SYNC.*/
		int retval = recvfrom(sockfd, &buf, sizeof(buf), 0, client, socklen);
		if (buf.type == SYN) {
			/* [2] check SYNC integrity.*/
			/* [3] init SYNACK.*/
			gbnhdr synack;
			synack.type = SYNACK;
			/* [4] call sendto, reply with SYNACK.*/
			sendto(sockfd, &synack, sizeof(synack), 0, client, socklen);
            s.addr = (struct sockaddr *) client;
			s.addrlen = socklen;
            break;
		}
	}
	printf("server successfully receive SYN and reply with SYNACK. Move to state.\n");
	return(sockfd);
}

ssize_t maybe_recvfrom(int  s, char *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen){

	/*----- Packet not lost -----*/
	if (rand() > LOSS_PROB*RAND_MAX){


		/*----- Receiving the packet -----*/
		int retval = recvfrom(s, buf, len, flags, from, fromlen);

		/*----- Packet corrupted -----*/
		if (rand() < CORR_PROB*RAND_MAX){
			/*----- Selecting a random byte inside the packet -----*/
			int index = (int)((len-1)*rand()/(RAND_MAX + 1.0));

			/*----- Inverting a bit -----*/
			char c = buf[index];
			if (c & 0x01)
				c &= 0xFE;
			else
				c |= 0x01;
			buf[index] = c;
		}

		return retval;
	}
	/*----- Packet lost -----*/
	return(len);  /* Simulate a success */
}
