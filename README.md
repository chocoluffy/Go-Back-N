# CS5450 Lab 1

Shunzhe Yu & Xiangru Qian

------

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