#include <stdio.h>
#include <stdlib.h>
#include "hash_func.h"
#include "epoll_helper.h"
#include "assert.h"

#define MAX_LEN 128

struct opts_struct
{
	int server_port;
};

struct port2pid *p2p = NULL;
struct fd2port  *f2p = NULL;
volatile unsigned long long it;
unsigned long long hash_mask = ~(unsigned long long)1 >> 32;
unsigned long long cyc_per100loop;
struct opts_struct opts = {11211};

void getopts(int argc, char** argv)
{
	int count = 1;

	while (count < argc) {
		if (strcmp(argv[count], "--server_port") == 0) {
			if (++count < argc)
				opts.server_port = atoi(argv[count]);
		}
		count++;
	}
}

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
handle_ekf(int ifd_read, int ofd_write, unsigned int spin)
{

	char data[MAX_LEN];
	int  len;

	unsigned long long stime = spin * 2900;
	unsigned long long loop = cyc2loop(stime);
	
	while (1) {
    	len = read(ifd_read, data, MAX_LEN);
		assert(len > 0);
		spin_delay(loop);
		len = write(ofd_write, data, len);
    }
	close(ifd_read);
	close(ofd_write);
	return 0;
}

int 
main(int argc, char *argv[]) {

	char      recv_data[MAX_LEN];
	char      buff[MAX_LEN] = {0};
	struct    sockaddr_in serverAddr, clientAddr;
	int       bytes_recv = 0, len = 0;
	int       in_fd[2] = {0}, out_fd[2] = {0};
	int       pid, c_port, ep_wait, i = 0;
	int       tmp, ip, port;
	struct    port2pid *p;
	
	unsigned int       spin_time = 0;
	unsigned long long iport = 0, ipp;
	unsigned long long s, e;

	getopts(argc, argv);
	s = mb_tsc();
	spin_delay(100000);
	e = mb_tsc();
	cyc_per100loop = (e-s)/1000;


   	serverAddr.sin_family = PF_INET;
   	serverAddr.sin_port = htons(opts.server_port);
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
	//printf("bind to 10.10.1.2:%d\n\t---->success\n", opts.server_port);

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
					if (pipe(in_fd) < 0 || pipe(out_fd) < 0) {
						perror("pipe error");
						exit(-1);
					}
					setnonblocking(in_fd[1]); //set nonblocking pipe
				    addfd(epfd, out_fd[0]); //listen to child

					pid = fork();

    				if(pid < 0) { 
						perror("fork error"); 
						exit(-1); 
					} else if (pid == 0) { // child
						close(in_fd[1]);
						close(out_fd[0]);
						handle_ekf(in_fd[0], out_fd[1], spin_time);
					} else { //parent
						add_port2pid(&p2p, iport, pid, in_fd, out_fd);
						add_fd2port(&f2p, out_fd[0], iport);
					
						close(in_fd[0]);
						close(out_fd[1]);
						len = write(in_fd[1], recv_data, bytes_recv);
					}	
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
