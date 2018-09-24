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
	int segment_num = len / DATALEN + 1;
	for (int i = 0; i < segment_num; i++) {
		(void *) segment_ptr = buf + i * DATALEN;
		int retval = (int) sendto(sockfd, segment_ptr, DATALEN, 0, server, socklen);
	}

	return(-1);
}

ssize_t gbn_recv(int sockfd, void *buf, size_t len, int flags){

	/* TODO: Your code here. */

	return(-1);
}

int gbn_close(int sockfd){

	/* TODO: Your code here. */
	return close(sockfd);
	/*  return(-1); */
}

int gbn_connect(int sockfd, const struct sockaddr *server, socklen_t socklen){

	/* TODO: Your code here. */
	gbnhdr syn;
	syn.type = SYN;
	int retval = (int) sendto(sockfd, &syn, sizeof(syn), 0, server, socklen);
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
			break;
		}
	}
	printf("server successfully receive SYN and reply with SYNACK. Move to state.\n");
	return(sockfd);
}

