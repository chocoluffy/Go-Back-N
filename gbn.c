#include "gbn.h"

address addr_book;
state_t server_state;
state_t client_state;

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
	int retval = (int) sendto(sockfd, buf, len, 0, addr_book.server_addr, addr_book.serveraddrlen);
	if (retval < 0) {
		perror("sendto in gbn_send()");
		exit(-1);
	}
	printf("expect to send %d, actually send %d", len, retval);
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

	/**
	 * send SYN to the server.
	 * receive SYNACK.
	 */
	gbnhdr syn;
	syn.type = SYN;
	int retval = (int) sendto(sockfd, &syn, sizeof(syn), 0, server, socklen);
	// printf("gbn_connect(), server address at %s.\n", addr_book.client_addr->sa_data);
	addr_book.server_addr = (struct sockaddr *) server;
	addr_book.serveraddrlen = socklen;
	printf("successfully send SYN to server side.\n");

	/* TODO: check if receive SYNACK from server side. */

	return(0);
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
            addr_book.client_addr = (struct sockaddr *) client;
			addr_book.clientaddrlen = socklen;
            break;
		}
	}
	printf("server successfully receive SYN and reply with SYNACK. Move to state.\n");
	return(sockfd);
}

