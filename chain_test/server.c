#include <stdio.h>
#include <stdlib.h>
#include "hash_func.h"
#include "epoll_helper.h"
#include "assert.h"

#define MAX_LEN 128

struct port2pid *p2p = NULL;
struct fd2port  *f2p = NULL;
volatile unsigned long long it;
unsigned long long hash_mask = ~(unsigned long long)1 >> 32;
unsigned long long cyc_per100loop;
int chain_len = 1;

static inline unsigned long long
mb_tsc(void) {
	unsigned long a, d, c;

	__asm__ __volatile__("rdtsc" : "=a" (a), "=d" (d), "=c" (c) : : );

	return ((unsigned long long)d << 32) | (unsigned long long)a;
}

void
print_recv(char *recv, int len)
{
	int i = 0;

	for (i= 0; i < len ; i++) {
		printf("%x ",recv[i]);
	}
	printf("\n");
}

static inline void
spin_delay(unsigned long long loop)
{
	while (it < loop) {
		it ++;
	}
	it = 0;
	return;
}

static inline unsigned long long
cyc2loop(unsigned long long spin_time)
{
	unsigned long long loop;

	loop = (spin_time/cyc_per100loop)*100;

	return loop;
}

int
handle_ekf(int* infd, int *outfd, unsigned int spin, int i)
{

	char data[MAX_LEN];
	int  len;

	unsigned long long stime = spin * 2900;
	unsigned long long loop = cyc2loop(stime);
	
	close(infd[1]);
	close(outfd[0]);
	while (1) {
    	len = read(infd[0], data, MAX_LEN);
		spin_delay(loop);
		len = write(outfd[1], data, len);
    }
	close(infd[0]);
	close(outfd[1]);
	return 0;
}

void
getopts(int argc, char** argv)
{
	int count = 1;

	while (count < argc) {
		if (strcmp(argv[count], "--clen") == 0) {
			if (++count < argc)
				chain_len = atoi(argv[count]);
		}
		count ++;
	}
}

int 
main(int argc, char *argv[]) {

	char      recv_data[MAX_LEN];
	char      buff[MAX_LEN] = {0};
	struct    sockaddr_in serverAddr, clientAddr;
	int       bytes_recv = 0, len = 0;
	//int       in_fd[2] = {0}, out_fd[2] = {0};
	int       pid, c_port, ep_wait, i = 0, j = 0;
	int       tmp, ip, port;
	int       chain_fd[10][2] = {{0}};
	struct    port2pid *p;
	int       ret=0;
	
	unsigned int       spin_time = 0;
	unsigned long long iport = 0, ipp;
	unsigned long long s, e;
	int idx = 0;

	s = mb_tsc();
	spin_delay(100000);
	e = mb_tsc();
	cyc_per100loop = (e-s)/1000;

	if (argc > 1)
		getopts(argc, argv);
	assert(chain_len < 10);

	serverAddr.sin_family = PF_INET;
   	serverAddr.sin_port = htons(SERVER_PORT);
   	serverAddr.sin_addr.s_addr = inet_addr("10.10.1.2");
	
    socklen_t cliLen = sizeof(clientAddr); 

   	int listener = socket(AF_INET, SOCK_DGRAM, 0);

   	if(listener < 0) {
		perror("listener"); 
		exit(-1);
	}
    
   	if(bind(listener, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
       	perror("bind error");
       	exit(-1);
   	}
	printf("bind to 10.10.1.2:%d\n\t---->success\n", SERVER_PORT);

	//create epoll
    int epfd = epoll_create(EPOLL_SIZE);
    if(epfd < 0) { 
		perror("epfd error");
		exit(-1);
	}
    static struct epoll_event events[PIDNUMB * 2 + 1];
    //sock add to epfd list
    addfd(epfd, listener);

   	while(1) {
		ep_wait = epoll_wait(epfd, events, PIDNUMB * 2, EPOLL_SIZE);
		for (i = 0; i < ep_wait; ++i) {
			if (events[i].data.fd == listener) {
				bytes_recv = recvfrom(listener, recv_data, MAX_LEN, 0, (struct sockaddr*)&clientAddr, &cliLen);
				spin_time = atoi(recv_data+25) * 10;
				assert(spin_time == 50 || spin_time == 1000);
				if (bytes_recv < 0) {
					perror("recvfrom error");
					exit(-1);
				}
				c_port = clientAddr.sin_port;
				iport  = (unsigned long long)clientAddr.sin_addr.s_addr << 32;
				iport |= (unsigned long long)c_port;

				p = find_port2pid(&p2p, iport);
				if (!p) { //new client
					if (pipe(chain_fd[0]) < 0 || pipe(chain_fd[chain_len]) < 0) {
						perror("pipe error");
						exit(-1);
					}
					setnonblocking(chain_fd[0][1]); //set nonblocking pipe
				    addfd(epfd, chain_fd[chain_len][0]); //listen to child

					for (j = 0; j < chain_len; j++) {
						if (j < chain_len-1) {
							ret = pipe(chain_fd[j+1]);
							assert(ret == 0);
						}
						pid = fork();
						assert(pid >= 0);
						if (pid == 0) {
							handle_ekf(chain_fd[j], chain_fd[j+1], spin_time, j);
						}
					}
					add_port2pid(&p2p, iport, chain_fd[0], chain_fd[chain_len]);
					add_fd2port(&f2p, chain_fd[chain_len][0], iport);
				
					close(chain_fd[0][0]);
					close(chain_fd[chain_len][1]);
					len = write(chain_fd[0][1], recv_data, bytes_recv);
				} else {
					len = write(p->in_fd[1], recv_data, bytes_recv);
				}
			} else if (events[i].events & EPOLLIN) {
				tmp = events[i].data.fd;
				len = read(tmp, buff, MAX_LEN);
				ipp = find_fd2port(&f2p, tmp)->iport;						
				ip = (int)(ipp >> 32);	
				port = (int)(ipp & hash_mask);
				clientAddr.sin_addr.s_addr = ip;
				clientAddr.sin_port = port;
				sendto(listener, buff, len, 0, ( struct sockaddr* )&clientAddr, cliLen);
			}
		}
	}
   	close(listener); //close socket
   	close(epfd); 
   	return 0;
}
