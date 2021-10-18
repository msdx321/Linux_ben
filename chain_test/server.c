#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include "hash_func.h"
#include "epoll_helper.h"
#include "assert.h"
#include <signal.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>
#include <linux/sched.h>
#include <sched.h>
#include <sys/types.h>

#define MAX_LEN 128
#define HI_STIME 5000
#define LO_STIME 20
#define DL_HI 500*1000
#define DL_LO 5*1000
#define __NR_sched_setattr 314
#define __NR_sched_getattr 315

struct sched_attr {
	uint32_t size;
	uint32_t sched_policy;
	uint64_t sched_flag;
	int32_t  sched_nice;
	uint32_t sched_priority;
	uint64_t sched_runtime;
	uint64_t sched_deadline;
	uint64_t sched_period;
};

typedef struct udphdr_s {
	uint16_t rqid;
	uint16_t partno;
	uint16_t nparts;
	uint16_t reserved;
} udphdr_t;

struct sched_dl {
	uint64_t runtime;
	uint64_t deadline;
	uint64_t period;
};

struct sched_dl hi_dl = {
	1024,
	50*1000*1000,
	50*1000*1000
};

struct sched_dl lo_dl = {
	20*1000,
	2000*1000,
	2000*1000
};

struct port2pid *p2p = NULL;
struct fd2port  *f2p = NULL;
volatile unsigned long long it;
volatile unsigned long long it_inf;
unsigned long long hash_mask = ~(unsigned long long)1 >> 32;
unsigned long long cyc_per100loop;
int chain_len = 4;
int server_port = 0;

int
sched_setattr (pid_t pid, const struct sched_attr *attr, unsigned int flags)
{
	return syscall(__NR_sched_setattr, pid, attr, flags);
}

int
sched_getattr (pid_t pid, const struct sched_attr *attr, unsigned int size, unsigned int flags)
{
	return syscall(__NR_sched_getattr, pid, attr, size, flags);
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

static long
get_us_interval(struct timeval *start, struct timeval *end)
{
	return (((end->tv_sec - start->tv_sec) * 1000000)
		+ (end->tv_usec - start->tv_usec));
}

unsigned long long
get_cpufreq(void)
{
	struct timeval start;
	struct timeval end;
	unsigned long long tstart;
	unsigned long long tend;
	long usec;

	if (gettimeofday(&start, 0))
		assert(0);
	tstart = mb_tsc();
	usleep(10000);

	if (gettimeofday(&end, 0))
		assert(0);
	tend = mb_tsc();

	usec = get_us_interval(&start, &end);
	return (tend-tstart) / (unsigned long long)usec;
}

int
handle_ekf(int infd, int outfd, int i)
{

	char data[MAX_LEN];
	char *body;
	int len;
	struct sockaddr_in clientAddr;
	unsigned long long e, s;
	unsigned long long cpufreq = get_cpufreq();

	unsigned long long stime = 0, ptime = 0, cyc;
	unsigned long long loop;
	int sched_algm = -100;
	struct sched_attr attr;

	clientAddr.sin_addr.s_addr = inet_addr("10.10.1.2");;
	clientAddr.sin_port = htons(server_port);
	clientAddr.sin_family = AF_INET;
	socklen_t cliLen = sizeof(clientAddr);

	attr.size = sizeof(attr);
	attr.sched_flag = SCHED_FLAG_RECLAIM;
	//attr.sched_flag = 0;
	attr.sched_nice = 0;
	attr.sched_priority = 0;
	attr.sched_policy = SCHED_DEADLINE;
	/*printf("size:     %d\n\
			flag:     %ld\n\
			nice:     %d\n\
			priority  %d\n\
			policy:   %d\n\
			runtime:  %ld\n\
			deadline: %ld\n\
			period:   %ld\n", attr.size, attr.sched_flag, attr.sched_nice, attr.sched_priority, attr.sched_policy, attr.sched_runtime, attr.sched_deadline, attr.sched_period);
*/
	while (1) {
		if (i == 1) {
			len = recvfrom(infd, data, MAX_LEN, 0, (struct sockaddr*)&clientAddr, &cliLen);
			assert(len > 0);
		} else {
			len = read(infd, data, MAX_LEN);
			assert(len > 0);
		}
		//if (stime == 0) {
			body = data + sizeof(udphdr_t);
			body += 14;
			ptime = atoi(body);
			assert(stime == 0 || ptime == stime);

			//if (stime != LO_STIME && stime != HI_STIME) {
			//	printf("i: %d, body: %s, data: %s, %s, %s\n", i, body, data, data+8, data+22);
				//printf("ERROR: stime: %llu\n", stime);
			//	printf("stime: %d, LO_STIME: %d, HI_STIME: %d\n", stime, LO_STIME, HI_STIME);
			//	assert(0);
			//}
			//assert(stime == LO_STIME || stime == HI_STIME || stime == 11211);
			assert(ptime == LO_STIME || ptime == HI_STIME);
		if (stime == 0) {
			if (ptime == HI_STIME) {
				attr.sched_runtime = hi_dl.runtime;
				attr.sched_deadline = hi_dl.deadline;
				attr.sched_period = hi_dl.period;
			} else if (ptime == LO_STIME) {
				attr.sched_runtime = lo_dl.runtime;
				attr.sched_deadline = lo_dl.deadline;
				attr.sched_period = lo_dl.period;
			} else assert(0);

			sched_algm = sched_setattr(0, &attr, 0x00);
			if (sched_algm) {
				perror("sched_setattr failed:");
			}
			stime = ptime;
		}
		//} else {
		//	printf("buffer overflow: %d\n", i);
		//}
		if (stime == 11211) {
			cyc = 30*1000*1000;
		}
		//sched_algm = sched_getscheduler(0);
		//printf("sched_algm: %d, szie: %d\n", sched_algm, sizeof(attr));

				//sched_algm = sched_getscheduler(0);
		//printf("sched_algm: %d\n", sched_algm);
		//printf("------------------\n");
		//if (i == 1 && stime == 5000)
		//	printf("[stime,inter_arrival us]: %llu, %llu\n", stime, inter_arrival);

		cyc = stime*cpufreq; 
		loop = cyc2loop(cyc);
		s = mb_tsc();
		spin_delay(loop);
		e = mb_tsc();
		while (e - s < cyc) {
			it ++;
			e = mb_tsc();
		}
		if (i < chain_len) {
			len = write(outfd, data, MAX_LEN);
			if (i <= 0 ) {
				perror("write fail");
				exit(-1);
			}
		} else {
			len = sendto(outfd, data, len, 0, ( struct sockaddr* )&clientAddr, cliLen);
		}
	}
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
		} else if (strcmp(argv[count], "--port") == 0) {
			if (++count < argc)
				server_port = atoi(argv[count]);
		} else {
			assert(0);
		}
		count ++;
	}
}

int 
main(int argc, char *argv[]) {

	struct    sockaddr_in serverAddr;
	int       pid, i = 0;
	
	unsigned long long s, e;
	int pl[2] = {0,0};
	int in = 0;
	int out = 0;
	int ret = 0;

	s = mb_tsc();
	spin_delay(100000);
	e = mb_tsc();
	//printf("100000 loops: %d\n", (e-s));
	cyc_per100loop = (e-s)/1000;

	if (argc > 1)
		getopts(argc, argv);
	assert(chain_len < 20);

	serverAddr.sin_family = PF_INET;
   	serverAddr.sin_port = htons(server_port);
   	serverAddr.sin_addr.s_addr = inet_addr("10.10.1.1");
	
   	int listener = socket(AF_INET, SOCK_DGRAM, 0);

   	if(listener < 0) {
		perror("listener"); 
		exit(-1);
	}
    
   	if(bind(listener, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
       	perror("bind error");
       	exit(-1);
   	}

/*	int epfd = epoll_create(5000);
	if (epfd < 0) {
		perror("epoll create fail");
		exit(-1);
	}

	static struct epoll_event events[5000];
	addfd(epfd, listener);
*/
	in = out = 0;
	for (i = 1; i < chain_len; i++) {
		if (pipe(pl) < 0)
			perror("pipe() fail");

		//ret = fcntl(pl[1], F_GETPIPE_SZ);
		//printf("before pipe_sz: %d\n",ret);
		ret = fcntl(pl[0], F_SETPIPE_SZ, 32*1000);
		assert(ret);
		ret = fcntl(pl[1], F_SETPIPE_SZ, 32*1000);
		assert(ret);

		//ret = fcntl(pl[1], F_GETPIPE_SZ);
		//printf("after pipe_sz: %d\n",ret);
		pid = fork();
		if (pid < 0) {
			perror("fork() failed");
		} else if (pid == 0) {
			in = dup(pl[0]);
			out = listener;
			close(pl[0]);
			close(pl[1]);
		} else {
			//printf("pid: %d, sched: %d; SCHED_NORMAL:%d, SCHED_RR: %d\n", pid, sched_getscheduler(pid),SCHED_OTHER, SCHED_RR);
			out = dup(pl[1]);
			close(pl[0]);
			close(pl[1]);
			break;
		}
	}
	if (in == 0)
		in = dup(listener);
	if (out == 0)
		out = dup(listener);
	
	handle_ekf(in, out, i);
	assert(0);
   	close(listener); //close socket
}
