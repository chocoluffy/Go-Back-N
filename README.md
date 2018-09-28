# CS5450 Lab 1

Shunzhe Yu & Xiangru Qian

------

## maybe_sendto
write a function maybe_sendto(), which is a wrapper of sendto(), that simulate: 
- bit corrupt.
- packet lost. 

## gnb_send
Check the data length field 'len'. If it is > DATALEN, you will have to split the data up into multiple packets - you don't have to worry about getting more than N * DATALEN.