# Linux micro-benchmark document

### what is this micro-benchmark
This micro-benchmark is based on a multi-process echoserver. It uses epoll to block wait on multiple file descriptors. The parent process act like a flow controller which forks new process for new flows and sending packets to existing child processes. We use pipe for the communications between processes.

The whole picture is,
+ when a new packet comes, flow control will lookup the hash table to make sure if this packets is the first packet of a new flow. 
	+ If it is, flow control will fork a new process and add an entry in the flow table. Along with the child process, flow controller will create to pipes one for passing data to child process, the other one for receiving data from child process.
	+ If not, send data to the correspond child process.
+ Flow control also listening to pipes which child processes use to send return packets. If got any, it will send these packets back to client.

### How to run this micro-benchmark
You should be able to directly make this micro-benchmark. You can run the server by simply `./server`.

For the client, you can refer to [Linux_client](https://github.com/WenyuanShao/Linux_ben/tree/master/Linux_client) which is a modified version of facebook mcblast. There are multiple test scripts. You can use `multi_client.sh [FLOW_NUM] [RATE]` for basic tests. It will generate multiple flows which equals your input each has the sending rate you've input.

This script will keep sending and receiving packets for 30 seconds and generate log files in `logs` folder. You can use `python data_aggreate.py logs/` to parse the results in this folder. 
