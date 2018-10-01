# CS5450 Lab 1

Shunzhe Yu & Xiangru Qian

------

[9.30] debug

- most of time, when we find from console output that *some packet are sent but not received from the other end. check if the `sendto()` and `recvfrom()` api are written correctly! be extremely careful on the type of addr and addrlen!*

- the correct logic of how to send a packet is implemented in `connect()`, put `sendto()` inside a while loop and `recvfrom()` in the nested while loop!

- how the seq num works? in byte or packet number?

[9.29]

1. sendto() and recvfrom()

ssize_t
     sendto(int socket, const void *buffer, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);

the last argument is the true value `socklen_t dest_len`, while for recvfrom():

ssize_t
     recvfrom(int socket, void *restrict buffer, size_t length, int flags, struct sockaddr *restrict address, socklen_t *restrict address_len);

the last argument is a pointer `socklen_t *restrict address_len`.

we use :
```
if (retval < 0) {
    printf("error num: %s. \n", strerror(errno));
    exit(-1);
}
```

2. extern state_t s;

declare `state_t s;` in the gbn.c


---

- how sequence number works. and how it relate to the prev sequence num (stored in the global state).
- how SIGALRM and timer works.
- how checksum works.

## signal(SIGALRM, handler)
timeout only affect the mode. 
and then, it will repeat the operations, two cases:
- resend SYN\FIN.
- resend DATA.

https://stackoverflow.com/questions/1784136/simple-signals-c-programming-and-alarm-function

register handler; alarm.

## maybe_sendto \ maybe_recvfrom (either one)
write a function maybe_sendto(), which is a wrapper of sendto(), that simulate: 
- bit corrupt.
- packet lost. 

## gnb_send
Check the data length field 'len'. If it is > DATALEN, you will have to split the data up into multiple packets - you don't have to worry about getting more than N * DATALEN.

---
if seq num does not match:
- packet lost.
- packet come in different order. 

client and server have two different byte stream. 

seq num means the position at self's stream;
ack num means how many has received from the other's stream. which can be also used for the other's next seq num.

3. logic of close()
- Either client or server can initiate closing the communication by sending a FIN packet
- Wait for a FINACK or FIN.
    - If FINACK is received, exit directly
    - If FIN is received, reply a FINACK and exit (this is in case of FINACK lost and the target client/server is already exited)

----
## TODO
1. put checksum into use
2. mark the packet with type DATA in gbn_send()
3. add RST

----
## POSSIBLE PROBLEMS
1. ack_num, seq_num are uint_8, however, the initial values are rand()%100, which could be very close to the upper bound
2.