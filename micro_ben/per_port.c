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

void
spin_delay(unsigned long long loop)
{
	assert(it == 0);
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
handle_ekf(unsigned int spin)
{
	unsigned long long stime = spin * 2900;
	unsigned long long loop = cyc2loop(stime);
	unsigned long long e, s;
	
	s = mb_tsc();
	spin_delay(loop);
	e = mb_tsc();

	return 0;
}

int 
main(int argc, char *argv[]) {

	char      recv_data[MAX_LEN];
	char      buff[MAX_LEN] = {0};
	struct    sockaddr_in serverAddr, clientAddr;
	int       bytes_recv = 0, len = 0;
	
	unsigned int       spin_time = 0;
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

   	while(1) {
		bytes_recv = recvfrom(listener, recv_data, MAX_LEN, 0, (struct sockaddr*)&clientAddr, &cliLen);
		spin_time = atoi(recv_data+25) * 10;
		assert(spin_time == 50 || spin_time == 1000);
		if (bytes_recv < 0) {
			perror("recvfrom error");
			exit(-1);
		}

		handle_ekf(spin_time);

		sendto(listener, recv_data, bytes_recv, 0, ( struct sockaddr* )&clientAddr, cliLen);
	}
   	close(listener); //close socket
   	return 0;
}
