#include "gbn.h"

state_t s;

void alarm_handler(int sig) {
    signal(SIGALRM, SIG_IGN); /* ignore same signal interrupting. */

    if (s.segment.type == SYN) {
        printf("[alarm_handler]: re-send SYN...\n");
        if (sendto(s.sockfd, &s.segment, sizeof(s.segment), 0, s.addr, s.addrlen) < 0) {
            perror("error in sendto() at gbn_connect()");
            exit(-1);
        }
    }
    if (s.segment.type == FIN) {
        printf("[alarm_handler]: re-send FIN...\n");
        if (sendto(s.sockfd, &s.segment, sizeof(s.segment), 0, s.addr, s.addrlen) < 0) {
            perror("error in sendto() at gbn_close()");
            exit(-1);
        }
    }
	if (s.segment.type == DATA) {
		printf("[alarm_handler]: re-send DATA segment. seq_num = %d, ack_num = %d, body_len = %d.\n", s.segment.seqnum, s.segment.acknum, s.segment.body_len);
        if (sendto(s.sockfd, &s.segment, sizeof(s.segment), 0, s.addr, s.addrlen) < 0) {
            perror("error in sendto() at gbn_close()");
            exit(-1);
        }
		s.mode = 0; /* change to slow mode. */
    }

    signal(SIGALRM, alarm_handler); /* re-register handler. */
	alarm(TIMEOUT);
}

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

	/* simulate GBN, N = 2. */
	printf("[gbn_send]: expect to send content of length = %d. \n", len);

	/* init value. */
	int init_seq_num = 1;
	s.next_expected_seq_num = init_seq_num; /* for checking when receiving data ack. */
	s.curr_ack_num = 1;
	int next_seq_num = init_seq_num; /* for cumulating the segment seq num in window. */

	gbnhdr received_data;
	struct sockaddr* from_addr;
	socklen_t from_len = sizeof(from_addr);

	int buf_ptr = 0, segment_ptr = 0;
	int this_window_total_data = 0;
	int window_buffer[4][3]; /* record each segment's information in window buffer. */

	while(buf_ptr < len) {

		int window_size = (1 << s.mode); /* 2^(s.mode) to get window size. */
		
		int window_counter = 0;

		while(window_counter < window_size && buf_ptr < len) { 
			/* window size numbers of segment can be sent sequentially. */

			gbnhdr new_segment;
			new_segment.type = DATA;
			new_segment.seqnum = next_seq_num;
			new_segment.acknum = s.curr_ack_num; /* ack_num is determined by the last received segment's seq_num and body_len. */
			new_segment.body_len = 0;

			segment_ptr = 0;
			while(segment_ptr < DATALEN && (buf_ptr + segment_ptr) < len) {
				new_segment.data[segment_ptr] = ((uint8_t*)buf)[buf_ptr + segment_ptr];
				segment_ptr++;
				new_segment.body_len++;
			}
			buf_ptr += segment_ptr;
			this_window_total_data += new_segment.body_len;

			s.segment = new_segment;
			s.seq_num = new_segment.seqnum;

			/* memorize in window buffer. */
			window_buffer[window_counter][0] = new_segment.seqnum;
			window_buffer[window_counter][1] = new_segment.acknum;
			window_buffer[window_counter][2] = new_segment.body_len;

			next_seq_num = new_segment.seqnum + new_segment.body_len; 
			sendto(sockfd, &s.segment, sizeof(s.segment), 0, s.addr, s.addrlen);
			printf("[gbn_send]: send one DATA segment. seq_num = %d, ack_num = %d, body_len = %d.\n", new_segment.seqnum, new_segment.acknum, new_segment.body_len);
			
			alarm(TIMEOUT);
			window_counter++;
		}

		gbnhdr received_data;
		int window_buffer_ptr = 0;
		int next_expected_ack_num = window_buffer[window_buffer_ptr][0] + window_buffer[window_buffer_ptr][2]; /* first segment's seq_num + body_len. */

		int curr_window_size = (1 << s.mode);

		while(1) {
			/* GBN(N=1), immediately wait for DATA ACK to proceed. */
			struct sockaddr* from_addr;
			socklen_t from_len = sizeof(from_addr);
			int retval = recvfrom(sockfd, &received_data, sizeof(received_data), 0, from_addr, &from_len);
			if (retval < 0) {
				perror('error in recvfrom() at gbn_send()');
				exit(-1);
			}
			if (received_data.type == DATAACK) {
				printf("[gbn_send]: received DATAACK. seq_num = %d, ack_num = %d, body_len = %d.\n", received_data.seqnum, received_data.acknum, received_data.body_len);
				if (received_data.acknum == next_expected_ack_num) {
					alarm(0); /* clear existing timers. */
					window_buffer_ptr++;
					if (s.mode < 2) {
						s.mode = s.mode + 1; /* when successfully sent one segment, switch to faster mode. */
					}
					s.curr_ack_num = received_data.seqnum + received_data.body_len;
					if (window_buffer_ptr == curr_window_size || (next_expected_ack_num - init_seq_num) == len) {
						/* when receive all data from client, break; otherwise, keep listening. */
						break;
					}
					next_expected_ack_num = next_expected_ack_num + window_buffer[window_buffer_ptr][2]; /* last ack num + next segment's body_len. */
				}
			}
		}
	}
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

	int cumulative_len = 0;
	s.curr_ack_num = 1;
	
	/* printf("[gbn_recv]: check s.addr: %d.\n", s.addr->sa_len); */

	while(1) {

		/* clear all old buf content. */
		int k;
		for (k = 0; k < DATALEN; k++) {
			((uint8_t*)buf)[k] = '\0';
		}

		gbnhdr received_data;
		struct sockaddr* from_addr;
		socklen_t from_len = sizeof(from_addr);

		int retval = (int) recvfrom(sockfd, &received_data, len, flags, &from_addr, &from_len);	
		if (retval < 0) {
			perror("recvfrom in gbn_recv()");
			exit(-1);
		}
		if (received_data.type == SYN) { /* resend SYNACK. */
			printf("[gbn_recv]: receive SYN..\n");
			gbnhdr synack;
			synack.type = SYNACK;
			synack.seqnum = 1;
			synack.checksum = 0;
			sendto(sockfd, &synack, sizeof(synack), 0, s.addr, s.addrlen);
			printf("[gbn_recv]: reply SYNACK..\n");
			continue;
		}
		if (received_data.type == FIN) { 
			printf("[gbn_recv]: receive FIN..\n");
			gbnhdr finack;
			finack.type = FINACK;
			finack.seqnum = 1;
			finack.checksum = 0;
			sendto(sockfd, &finack, sizeof(finack), 0, s.addr, s.addrlen);
			printf("[gbn_recv]: reply FINACK..\n");
			s.status = FIN_RCVD;
			return 0;
		}
		if (received_data.type == DATA) {
			printf("[gbn_recv]: received one DATA segment. seq_num = %d, ack_num = %d, body_len = %d.\n", received_data.seqnum, received_data.acknum, received_data.body_len);
			if (s.curr_ack_num == 1 || s.curr_ack_num == received_data.seqnum) { 

				/* send correct ack. */
				gbnhdr dataack;
				dataack.type = DATAACK;
				dataack.seqnum = s.prev_seq_num;
				s.prev_seq_num++;
				dataack.acknum = (int)received_data.seqnum + (int)received_data.body_len;
				dataack.body_len = 1; /* ACK's body_len = 1? */
				sendto(sockfd, &dataack, sizeof(dataack), 0, s.addr, s.addrlen);
				printf("[gbn_recv]: reply DATAACK. seq_num = %d, ack_num = %d, body_len = %d.\n", dataack.seqnum, dataack.acknum, dataack.body_len);

				/* expect next segment's seq num to equal this. */
				s.curr_ack_num = dataack.acknum;

				/* 
				 * write received_data into buf. 
				 * body_len = DATALEN, except for the last packet.
				 */
				int i;
				for (i = 0; i < received_data.body_len; i++) {
					((uint8_t*)buf)[i] = received_data.data[i];
				}
				printf("[gbn_recv]: write to buf. len = %d.\n", received_data.body_len);
				return(received_data.body_len);
			}
			else if(s.curr_ack_num < received_data.seqnum) {
				/* send duplicate ack.  */
				gbnhdr dupack;
				dupack.type = DATAACK;
				dupack.seqnum = (uint8_t) -1;
				dupack.acknum = (uint8_t) s.curr_ack_num;
				sendto(sockfd, &dupack, sizeof(dupack), 0, s.addr, s.addrlen);
			}
		}
	}
}


int gbn_close(int sockfd) {

    if (s.status == FIN_RCVD) {
		printf("[gbn_close]: connection already closed. exit.\n");
		return 0;
	}

	while (1) {
		/* construct current fin packet. */
		gbnhdr fin_segment;
		fin_segment.type = FIN;

		int retval = (int) sendto(sockfd, &fin_segment, sizeof(fin_segment), 0, s.addr, s.addrlen);
		if (retval < 0) {
			perror("error in sendto() at close()");
			exit(-1);
		}

		s.status = FIN_SENT;
		s.segment = fin_segment; /* update state(prev state) */
		printf("[gbn_close]: send FIN.\n");
		alarm(TIMEOUT); /* start timer. */
		
		/* wait for FINACK / FIN.
		* - timeout: repeat connect.
		* - successfully receive FINACK / FIN, close connection.
		*/
		struct sockaddr *from_addr;
		socklen_t from_len = sizeof(from_addr);
		gbnhdr buf;
		while (1) {
			printf("[gbn_close]: start listening...\n");
			int retval_rec = recvfrom(sockfd, &buf, sizeof(buf), 0, from_addr, &from_len);
			if (retval_rec < 0) {
				perror("error in recvfrom() at gbn_close()");
				exit(-1);
			}
			if (buf.type == FINACK) {
				printf("[gbn_close]: received FINACK...\n");
				alarm(0); /* clear all existing timers. */
				s.status = FIN_RCVD;
				return (close(sockfd));
			}
			if (buf.type == FIN) {
				printf("[gbn_close]: received FIN...\n");
				alarm(0); /* clear all existing timers. */
				gbnhdr finack_segment;
				finack_segment.type = FIN;
				finack_segment.seqnum = (uint8_t) s.seq_num;
				finack_segment.acknum = (uint8_t) s.ack_num;
				sendto(sockfd, &finack_segment, sizeof(finack_segment), 0, s.addr, s.addrlen);
				printf("Successfully received FIN. FINACK replied and connection closed\n");
				s.status = FIN_RCVD;
				return (close(sockfd));
			}
		}
	}
}


int gbn_connect(int sockfd, const struct sockaddr *server, socklen_t socklen){
	
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
	s.sockfd = sockfd;
	s.seq_num = rand() % 100;
	s.ack_num = -1;
	s.data_len = 0;
	s.mode = 0;

	signal(SIGALRM, alarm_handler);

	/* only break when connection is established (SYNACK received). */
	while (1) {
		/* construct current syn packet. */
		gbnhdr syn_segment;
		syn_segment.type = SYN;
		syn_segment.seqnum = s.seq_num;
		syn_segment.acknum = s.ack_num;
		
		int retval = (int) sendto(sockfd, &syn_segment, sizeof(syn_segment), 0, server, socklen);
		if (retval < 0) {
			perror("error in sendto() at gbn_connect()");
			exit(-1);
		}
		s.segment = syn_segment; /* update state(prev state) */
		alarm(TIMEOUT); /* start timer. */

		s.addr = (struct sockaddr *) server;
		s.addrlen = socklen;
		printf("[gbn_connect]: send SYN.\n");

		/* wait for SYNACK. 
		* - timeout: repeat connect.
		* - successfully receive SYNACK, move to next state.
		*/
		gbnhdr buf;

		while (1) {

			struct sockaddr* from_addr;
			socklen_t from_len = sizeof(from_addr);
			int retval = recvfrom(sockfd, &buf, sizeof(buf), 0, from_addr, &from_len);
			if (retval < 0) {
				perror('error in recvfrom() at gbn_connect()');
				exit(-1);
			}
			if (buf.type == SYNACK) {
				alarm(0); /* clear all existing timers. */
				printf("[gbn_connect]: received SYNACK. \n");
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

	while(1) {
		/* [1] call recvfrom. to get SYNC.*/
		int retval = recvfrom(sockfd, &buf, sizeof(buf), 0, client, socklen);

		if (buf.type == SYN) {
			/* [2] check SYNC integrity.*/
			/* [3] init SYNACK.*/
			gbnhdr synack;
			synack.type = SYNACK;
			synack.seqnum = 1;
			synack.checksum = 0;
			/* [4] call sendto, reply with SYNACK.*/
			int retval = sendto(sockfd, &synack, sizeof(synack), 0, client, *socklen);
			if (retval < 0) {
				perror("error in sendto() at gbn_accept().");
				exit(-1);
			}
            s.addr = client;
			s.addrlen = *socklen;
			printf("[gbn_accept]: server successfully receive SYN and reply with SYNACK. Move to state.\n");
            break;
		}
	}
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
