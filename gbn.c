#include "gbn.h"

state_t s;

void alarm_handler(int sig) {
    s.timeout_times++;
    if (s.timeout_times > 5) {
        /*  close connection. */
        gbn_close(s.sockfd);
    }
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
        printf("[alarm_handler]: re-send DATA segment. seq_num = %d, ack_num = %d, body_len = %d.\n", s.segment.seqnum,
               s.segment.acknum, s.segment.body_len);
        if (sendto(s.sockfd, &s.segment, sizeof(s.segment), 0, s.addr, s.addrlen) < 0) {
            perror("error in sendto() at gbn_close()");
            exit(-1);
        }
        s.mode = 0; /* change to slow mode. */
    }

    signal(SIGALRM, alarm_handler); /* re-register handler. */
    alarm(TIMEOUT);
}

uint16_t checksum(uint8_t *buf, int nwords) {
    uint16_t sum;

    for (sum = 0; nwords > 0; nwords--)
        sum += *buf++;
    sum = (sum >> 16) + (sum & (uint16_t) 0xffff);
    sum += (sum >> 16);
    return ~sum;
}

ssize_t gbn_send(int sockfd, const void *buf, size_t len, int flags) {

    /* Hint: Check the data length field 'len'.
     *       If it is > DATALEN, you will have to split the data
     *       up into multiple packets - you don't have to worry
     *       about getting more than N * DATALEN.
     */

    printf("[gbn_send]: expect to send content of length = %ld. \n", len);

    /* Initialize variables */
    int init_seq_num = s.next_expected_seq_num;
    int next_seq_num = s.next_expected_seq_num; /* for cumulating the segment seq num in window. */
    int buf_ptr = 0, segment_ptr = 0;
    int this_window_total_data = 0;
    gbnhdr window_buffer[4]; /* record each segment's information in window buffer. */

    while (buf_ptr < len) {

        int window_size = (1 << s.mode); /* 2^(s.mode) to get window size. */
        int window_counter = 0;

        while (window_counter < window_size && buf_ptr < len) {
            /* window size numbers of segment can be sent sequentially. */

            gbnhdr new_segment;
            new_segment.type = DATA;
            new_segment.seqnum = (uint32_t) next_seq_num;
            new_segment.acknum = (uint32_t) s.curr_ack_num; /* ack_num is determined by the last received segment's seq_num and body_len. */
            new_segment.body_len = 0;
            segment_ptr = 0;

            while (segment_ptr < DATALEN && (buf_ptr + segment_ptr) < len) {
                new_segment.data[segment_ptr] = ((uint8_t *) buf)[buf_ptr + segment_ptr];
                segment_ptr++;
                new_segment.body_len++;
            }

            new_segment.checksum = checksum(new_segment.data, new_segment.body_len);
            buf_ptr += segment_ptr;
            this_window_total_data += new_segment.body_len;
            if (window_counter == 0) {
                s.segment = new_segment;
            }

            /* memorize in window buffer. */
            window_buffer[window_counter] = new_segment;
            next_seq_num = new_segment.seqnum + new_segment.body_len;

            sendto(sockfd, &new_segment, sizeof(new_segment), 0, s.addr, s.addrlen);
            printf("[gbn_send]: send one DATA segment. seq_num = %d, body_len = %d.\n", new_segment.seqnum,
                   new_segment.body_len);

            alarm(TIMEOUT);
            window_counter++;
        }

        gbnhdr received_data;
        int window_buffer_ptr = 0;

        /* first segment's seq_num + body_len. */
        int next_expected_ack_num = window_buffer[window_buffer_ptr].seqnum + window_buffer[window_buffer_ptr].body_len;
        int curr_window_size = (1 << s.mode);

        while (1) {
            /* GBN(N=1), immediately wait for DATA ACK to proceed. */
            struct sockaddr *from_addr = NULL;
            socklen_t from_len = sizeof(from_addr);
            ssize_t retval = maybe_recvfrom(sockfd, (char *) &received_data, sizeof(received_data), 0, from_addr, &from_len);
            if (retval < 0) {
                perror("error in recvfrom() at gbn_send()");
                exit(-1);
            }
            if (received_data.type == DATAACK) {
                printf("[gbn_send]: received DATAACK. ack_num = %d, body_len = %d.\n", received_data.acknum,
                       received_data.body_len);
                if (received_data.acknum == next_expected_ack_num) {
                    alarm(0); /* clear existing timers. */
                    s.timeout_times = 0; /* reset global timeout_times counter. */
                    alarm(TIMEOUT);
                    window_buffer_ptr++;
                    if (window_buffer_ptr < window_size) {
                        s.segment = window_buffer[window_buffer_ptr];
                    }
                    if (s.mode < 2) {
                        s.mode = s.mode + 1; /* when successfully sent one segment, switch to faster mode. */
                    }
                    s.curr_ack_num = received_data.seqnum + received_data.body_len;
                    s.next_expected_seq_num = received_data.acknum;
                    if (window_buffer_ptr == curr_window_size || (next_expected_ack_num - init_seq_num) == len) {
                        /* when receive all data from client, break; otherwise, keep listening. */
                        break;
                    }
                    /* last ack num + next segment's body_len. */
                    next_expected_ack_num = next_expected_ack_num + window_buffer[window_buffer_ptr].body_len;
                }
            }
        }
    }
    return (0);
}

ssize_t gbn_recv(int sockfd, void *buf, size_t len, int flags) {
    /**
     * buf.type can be:
     * - SYN
     * - FIN
     * - DATA
     *
     * any type can be corrupted.
     * if it's a DATA packet, check sequence number to see if there is packet lost.
     */
    while (1) {

        /* clear all old buf content. */
        int k;
        for (k = 0; k < DATALEN; k++) {
            ((uint8_t *) buf)[k] = '\0';
        }

        gbnhdr received_data;
        struct sockaddr *from_addr = NULL;
        socklen_t from_len = sizeof(from_addr);
        int retval = (int) maybe_recvfrom(sockfd, (char *) &received_data, SEGMENT_SIZE, flags, from_addr, &from_len);
        if (retval < 0) {
            perror("recvfrom in gbn_recv()");
            exit(-1);
        }
        if (received_data.type == SYN) { /* reply SYNACK. */
            printf("[gbn_recv]: receive SYN..\n");
            gbnhdr synack;
            synack.type = SYNACK;
            synack.seqnum = 1;
            synack.checksum = 0;
            sendto(sockfd, &synack, sizeof(synack), 0, s.addr, s.addrlen);
            printf("[gbn_recv]: reply SYNACK..\n");
            continue;
        }
        if (received_data.type == FIN) { /* reply FINACK. */
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
        if (received_data.type == DATA) { /* reply DATAACK. */
            printf("[gbn_recv]: received one DATA segment. seq_num = %d, body_len = %d.\n", received_data.seqnum,
                   received_data.body_len);

            /* check whether the data is corrupted */
            bool passed_checksum = true;
            if (received_data.checksum != checksum(received_data.data, received_data.body_len)) {
                printf("[gbn_recv]: The data is corrupted. Checksum should be %d, but it is actually %d\n",
                       received_data.checksum, checksum(received_data.data, received_data.body_len));
                passed_checksum = false;
            } else {
                printf("[gbn_recv]: Data integrity checked.\n");
            }

            if ((s.curr_ack_num == 1 || s.curr_ack_num == received_data.seqnum) && passed_checksum) {
                /* send correct ack. */
                gbnhdr dataack;
                dataack.type = DATAACK;
                dataack.seqnum = (uint32_t) s.prev_seq_num;
                s.prev_seq_num++;
                dataack.acknum = received_data.seqnum + received_data.body_len;
                dataack.body_len = 1; /* ACK's body_len = 1? */
                sendto(sockfd, &dataack, sizeof(dataack), 0, s.addr, s.addrlen);
                printf("[gbn_recv]: reply DATAACK. ack_num = %d, body_len = %d.\n", dataack.acknum, dataack.body_len);

                /* expect next segment's seq num to equal this. */
                s.curr_ack_num = dataack.acknum;

                /*
                 * write received_data into buf.
                 * body_len = DATALEN, except for the last packet.
                 */
                int i;
                for (i = 0; i < received_data.body_len; i++) {
                    ((uint8_t *) buf)[i] = received_data.data[i];
                }
                printf("[gbn_recv]: write to buf. len = %d.\n", received_data.body_len);
                return (received_data.body_len);
            } else if ((s.curr_ack_num < received_data.seqnum) || (!passed_checksum)) {
                /* send duplicate ack.  */
                gbnhdr dupack;
                dupack.type = DATAACK;
                dupack.seqnum = (uint32_t) -1;
                dupack.acknum = (uint32_t) s.curr_ack_num;
                sendto(sockfd, &dupack, sizeof(dupack), 0, s.addr, s.addrlen);
                printf("[gbn_recv]: reply dup DATAACK. seq_num = %d, ack_num = %d, body_len = %d.\n", dupack.seqnum,
                       dupack.acknum, dupack.body_len);
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

        /*
        * wait for FINACK / FIN.
        * - timeout: repeat connect.
        * - successfully receive FINACK / FIN, close connection.
        */
        struct sockaddr *from_addr = NULL;
        socklen_t from_len = sizeof(from_addr);
        gbnhdr buf;
        while (1) {
            printf("[gbn_close]: start listening...\n");
            ssize_t retval_rec = maybe_recvfrom(sockfd, (char *) &buf, sizeof(buf), 0, from_addr, &from_len);
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


int gbn_connect(int sockfd, const struct sockaddr *server, socklen_t socklen) {

    /* set initial state properties. */
    s.sockfd = sockfd;
    s.seq_num = 0;
    s.ack_num = -1;
    s.data_len = 0;
    s.mode = 0;
    s.curr_ack_num = 1;
    s.timeout_times = 0;
    s.next_expected_seq_num = 1;

    signal(SIGALRM, alarm_handler);

    /* only break when connection is established (SYNACK received). */
    while (1) {
        /* construct current syn packet. */
        gbnhdr syn_segment;
        syn_segment.type = SYN;
        syn_segment.seqnum = (uint32_t) s.seq_num;
        syn_segment.acknum = (uint32_t) s.ack_num;

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

        /*
        * wait for SYNACK.
        * - timeout: repeat connect.
        * - successfully receive SYNACK, move to next state.
        */
        gbnhdr buf;

        while (1) {
            struct sockaddr *from_addr = NULL;
            socklen_t from_len = sizeof(from_addr);
            ssize_t retval2 = maybe_recvfrom(sockfd, (char *) &buf, sizeof(buf), 0, from_addr, &from_len);
            if (retval2 < 0) {
                perror("error in recvfrom() at gbn_connect()");
                exit(-1);
            }
            if (buf.type == SYNACK) {
                alarm(0); /* clear all existing timers. */
                printf("[gbn_connect]: received SYNACK. \n");
                return (0);
            }
        }
    }
}

int gbn_listen(int sockfd, int backlog) {

    s.curr_ack_num = 1; /* init server side first ack num. */
    return 0;
}

int gbn_bind(int sockfd, const struct sockaddr *server, socklen_t socklen) {

    return bind(sockfd, server, socklen);
}

int gbn_socket(int domain, int type, int protocol) {

    /*----- Randomizing the seed. This is used by the rand() function -----*/
    srand((unsigned) time(0));

    return socket(domain, type, protocol);
}

int gbn_accept(int sockfd, struct sockaddr *client, socklen_t *socklen) {
    gbnhdr buf;
    while (1) {
        /* [1] call recvfrom. to get SYNC.*/
        ssize_t retval = maybe_recvfrom(sockfd, (char *) &buf, sizeof(buf), 0, client, socklen);
        if (retval < 0) {
            perror("error in maybe_recvfrom() at gbn_accept().");
            exit(-1);
        }
        if (buf.type == SYN) {
            /* [2] init SYNACK.*/
            gbnhdr synack;
            synack.type = SYNACK;
            synack.seqnum = 1;
            synack.checksum = 0;
            /* [3] call sendto, reply with SYNACK.*/
            ssize_t retval2 = sendto(sockfd, &synack, sizeof(synack), 0, client, *socklen);
            if (retval2 < 0) {
                perror("error in sendto() at gbn_accept().");
                exit(-1);
            }
            s.addr = client;
            s.addrlen = *socklen;
            printf("[gbn_accept]: server successfully receive SYN and reply with SYNACK. Move to state.\n");
            break;
        }
    }
    return (sockfd);
}

ssize_t maybe_recvfrom(int s, char *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen) {

    /*----- Packet not lost -----*/
    if (rand() > LOSS_PROB * RAND_MAX) {


        /*----- Receiving the packet -----*/
        ssize_t retval = recvfrom(s, buf, len, flags, from, fromlen);

        /*----- Packet corrupted -----*/
        if (rand() < CORR_PROB * RAND_MAX) {
            /*----- Selecting a random byte inside the packet -----*/
            int index = (int) ((len - 1) * rand() / (RAND_MAX + 1.0));

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
    return (len);  /* Simulate a success */
}
