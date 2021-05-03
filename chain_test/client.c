#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/types.h>
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <assert.h>
#include <pthread.h>
#include "epoll_helper.h"
#include "cpu.h"

#define BASE_PORT 11211
#define SAMPLE_NUM (10000+1)
#define succ(n, size) (((n)+1) % (size))
#define KEYPREFIX "kkkkkkkkk"

typedef struct stats_t {
	uint64_t rtt_total,
             rtt_min,
			 rtt_max;
	uint64_t nmeasured,
             ntimeouts,
			 nsent,
			 nmade;
	uint64_t samples[SAMPLE_NUM];
} stats_t;

typedef struct udphdr_s {
	uint16_t rqid;
	uint16_t partno;
	uint16_t nparts;
	uint16_t reserved;
} udphdr_t;

typedef struct thread_s {
	pthread_t pt;
	stats_t stats;
	double cpu_freq;
	uint64_t tstart;
	uint64_t tend;
	int idx;

	volatile int done;
} thread_t;

typedef struct quantum_s {
	uint64_t current,
			 last,

			 size;
} quantum_t;

typedef struct req_s{
	uint32_t id;

	int npartsleft;

	uint64_t tsent;
	uint64_t treply;
} req_t;

typedef struct rqwheel_s {
	int tail;
	int head;
	
	req_t *rqs;
	int size;
	uint32_t nextrqid;
	thread_t *th;
} rqwheel_t;

static inline void
quantum_init(quantum_t *q, uint64_t size)
{
	q->current = q->last = 0;
	q->size = size;
}

static int num = 1;
static int rate_limit = 50;
thread_t threads[2048];
static int duration = 0;
static unsigned long long deadline;
static int spin_time = 10;


/*static inline uint64_t
cycle_timer(void)
{
	uint32_t a, d;
	uint64_t val;

	asm volatile("rdtsc" : "=a" (a), "=d" (d));
	val = ((uint64_t)a) | ((uint64_t)d << 32);

	return val;
}*/

void
stopthread(int sig)
{
	int i;

	printf("stop thread\n");
	for (i = 0; i < num; i++) {
		threads[i].done = 1;
	}
}

/*static long
get_us_interval(struct timeval *start, struct timeval *end)
{
	return (((end->tv_sec - start->tv_sec) * 1000000)
		+ (end->tv_usec - start->tv_usec));
}

double
get_cpu_frequency(void)
{    
	struct timeval start;
	struct timeval end;
	uint64_t tsc_start;
	int64_t tsc_end;
	long usec;

	if (gettimeofday(&start, 0))
		assert(0);
	
	tsc_start = cycle_timer(); 
	usleep(10000);
	
	if (gettimeofday(&end, 0))
		assert(0);
    tsc_end = cycle_timer();
		usec = get_us_interval(&start, &end);
	return (tsc_end - tsc_start) * 1.0 / usec;
}*/

static inline void
req_init(req_t *rq, uint32_t id)
{
	rq->id = id;
	rq->npartsleft = -1;
	rq->tsent = cycle_timer();
	rq->treply = 0;
}

static void
rqwheel_init(rqwheel_t *w)
{
	int size = 4096;
	memset(w, 0, sizeof(*w));
	w->size = size;
	w->nextrqid = 0;

	w->rqs = (req_t*)calloc(size, sizeof(req_t));
	assert(w->rqs);
}

static inline void
rqwheel_append_request(rqwheel_t *w)
{
	req_init(&w->rqs[w->head], w->nextrqid++);
	w->head = succ(w->head, w->size);
	w->th->stats.nsent ++;

	if (w->head == w->tail) {
		req_t *rq;
		rq = &w->rqs[w->tail];
		rq->treply = cycle_timer();

		w->th->stats.ntimeouts++;

		do {
			w->tail = succ(w->tail, w->size);
		} while (w->tail != w->head && w->rqs[w->tail].treply > 0);
	}
}

static inline int
rqwheel_isempty(rqwheel_t *w)
{
	if (w->head == w->tail)
		return 1;
	else
		return 0;
}

static inline void
stats_update_rtts(stats_t *st, uint64_t tsent, uint64_t treply, double cpufreq) {
	uint64_t rtt = treply - tsent;

    if (rtt / cpufreq <= deadline)
		st->nmade++;
	if (rtt < st->rtt_min)
		st->rtt_min = rtt;
	if (rtt > st->rtt_max)
		st->rtt_max = rtt;

	st->rtt_total += rtt;
	st->nmeasured++;

	/*if (rtt / cpufreq >= MAXRTT)
		st->nslow++;
	else
		st->rtt_buckets[(unsigned)(rtt / cpufreq / RTTBUCKET)]++;*/

	if (st->nmeasured < SAMPLE_NUM) {
		st->samples[st->nmeasured] = rtt / cpufreq;
	} else {
		int rk = (int)(treply % st->nmeasured) + 1;
		if (rk < SAMPLE_NUM) st->samples[rk] = rtt / cpufreq;
	}
}

static inline void
rqwheel_note_udp_reply(rqwheel_t *w, udphdr_t rs/*, int k, reqtype_t t*/) {
	int match; /* expected position of the corresponding request in w->rqs[] */
    int last = (w->head == 0 ? w->size-1 : w->head-1);
	uint16_t rqdistance = rs.rqid - (uint16_t)w->rqs[w->tail].id;
	req_t *rq; /* matching request */

	/* try to locate the request record for rs.rqid */

	if (rqwheel_isempty(w)) {
		assert(0);
	/* request wheel is empty. Ignore this reply. */
	//	if (!quiet) {
	//		fprintf(stderr, "Got a UDP reply with id %d for key %d with empty "
	//				"request queue!\n", (int)rs.rqid, k);
	//	}
	//	w->th->stats[req_get].nignore++;
	//	return;

	}
	match = (w->tail + (uint32_t)rqdistance) % w->size;

	/* verify that _match_ is in [tail..last] modulo w->size. If it is not,
	   the reply is for a request that's no longer in the queue. The timedout
	   counter has already been incremented in rqwheel_append_request(). */
	if (w->tail <= last) {
		if (match < w->tail || last < match) {
			assert(0);
			//w->th->stats[req_get].nignore++;
			return;
		}
	} else if (last < match && match < w->tail) {
		assert(0);
		//w->th->stats[req_get].nignore++;
		return;
	}

	rq = &w->rqs[match];

	/* request ids wrapped around AND the matching request is no longer in
	   the queue. The request has already been counted as timed out in
	   rqwheel_append_request() that bumped the request record off
	   the queue. */
	if ((uint16_t)rq->id != rs.rqid) {
		/*if (!quiet) {
			fprintf(stderr, "Got reply for request id %u, expected %u\n",
			(unsigned)rs.rqid, (unsigned)(rq->id & 0xffff));
		}*/
		assert(0);
		//w->th->stats[req_get].nignore++;
		return;
	}
	/*if (rq->key != k && k >= 0) {
		if (!quiet) {
			fprintf(stderr, "Got reply for a 'get' with key %d, expected key %d\n",
					k, rq->key);
		}
		w->th->stats[req_get].nignore++;
		return;
	}
	if (t >= 0 && rq->type != t) {
		if (!quiet) {
			fprintf(stderr, "Got a reply of type %s, expected %s\n",
					reqtype_str[t], reqtype_str[rq->type]);
		}
		w->th->stats[req_get].nignore++;
		return ;
	}*/

	if (rq->npartsleft < 0) {
		/* got first reply part for this request */
		rq->npartsleft = (unsigned)rs.nparts;
	} else if (rq->npartsleft == 0) {
		/*if (!quiet) {
			printf(stderr, "Got a duplicate reply for 'get' request %u "
				   "for key %d\n", rq->id, rq->key);
		}*/
		assert(0);
		//w->th->stats[req_get].nignore++;
		return;
	}

	rq->npartsleft--;

	/* if (rs.partno == 0 && k < 0 && !quiet) { */
	/*   fprintf(stderr, "'get' request for key %d failed\n", rq->key); */
	/* } */

	if (rq->npartsleft > 0) {
		assert(0);
		//w->th->stats[req_get].nignore++;
		return;
	}

	/* we got all reply parts, mark request completed */

	rq->treply = cycle_timer();

	stats_update_rtts(&w->th->stats, rq->tsent, rq->treply,
					   w->th->cpu_freq);

	if (match == w->tail) {
		/* trim the request queue by moving tail forward to the oldest request
		   that is still outstanding */
		while (w->tail != w->head && w->rqs[w->tail].treply)
			w->tail = succ(w->tail, w->size);
	}
}

void
getopts(int argc, char** argv)
{
	int count = 1;

	while (count < argc) {
		if (strcmp(argv[count], "-n") == 0) {
			if (++count < argc)
				num = atoi(argv[count]);
		} else if (strcmp(argv[count], "-r") == 0) {
			if (++count < argc) {
				rate_limit = atoi(argv[count]);
				deadline = (unsigned long long)1000000 / rate_limit;
			}
		} else if (strcmp(argv[count], "-d") == 0) {
			if (++count < argc)
				duration = atoi(argv[count]);
		} else {
			assert(0);
		}
		count ++;
	}
	printf("num: %d, rate: %d, duration: %d\n", num, rate_limit, duration);
}

static void
block_signals(void)
{
	sigset_t set;
	int rv;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGALRM);

	rv = pthread_sigmask(SIG_BLOCK, &set, NULL);
	assert(rv == 0);
}

static inline int
parse_reply(char* dgram, int len, udphdr_t *udphdr)
{
	udphdr->rqid = htons(((uint16_t *)dgram)[0]);
	udphdr->partno = htons(((uint16_t *)dgram)[1]);
	udphdr->nparts = htons(((uint16_t *)dgram)[2]);
	udphdr->reserved = 0;

	return 1;
}

static inline void
compose_packet(char* buff, uint32_t reqid)
{
	*buff++ = reqid >>0x8;
	*buff++ = reqid &0xff;
	*buff++ = 0;
	*buff++ = 0;
	*buff++ = 0;
	*buff++ = 1;
	*((uint16_t*)buff) = htons(11211);
}

static inline int
compose(char *buf, int bufsize)
{
	int ret = snprintf(buf, bufsize, "get " KEYPREFIX "-%06d\r\n", spin_time);
	return ret;
}

static void *
thread_main(void* arg) 
{
	thread_t *th = (thread_t *)arg;
	quantum_t q;
	int sockfd; 
	struct sockaddr_in servaddr, cliaddr;
	int ret = 0;
	int idx = 0;
	int wait = 0;
	udphdr_t udphdr;
	socklen_t addr_len = sizeof(struct sockaddr_in);
	char buf[256];
	rqwheel_t *w;
	int dgsize;
	int i = 0;

	w = (rqwheel_t *)malloc(sizeof(rqwheel_t));
	th->tstart = cycle_timer();
	block_signals();
	th->cpu_freq = get_cpu_frequency();
	quantum_init(&q, (rate_limit > 0) ?
					  th->cpu_freq * 1000000 / rate_limit : 0);

	rqwheel_init(w);
	w->th = th;

	printf("thread main-> quantum size: %lld, rate: %d, freq: %lf\n", q.size, rate_limit, th->cpu_freq);
	sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
	if (sockfd < 0) {
		perror("socket creation failed");
		exit(0); 
	} 
	memset(&servaddr, 0, sizeof(servaddr)); 
	memset(&cliaddr, 0, sizeof(cliaddr)); 

	cliaddr.sin_family = AF_INET;
	cliaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	cliaddr.sin_port = htons(BASE_PORT + th->idx + 100);

	if (bind(sockfd, (struct sockaddr *)&cliaddr, addr_len) < 0) {
		perror("bind fail");
		exit(0);
	}

	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	servaddr.sin_port = htons(BASE_PORT + th->idx);
	setnonblocking(sockfd);

	int epfd = epoll_create(2);
	if (epfd < 0) {
		perror("epoll creation failed");
		exit(-1);
	}

	static struct epoll_event event[2];
	addfd(epfd, sockfd);

	compose_packet(buf, idx);
	dgsize = compose(buf+8, sizeof(buf)-8) + 8;
	ret = sendto(sockfd, buf, dgsize, 0, (struct sockaddr*) &servaddr, addr_len);
	/*for (i=0; i < dgsize; i++) {
		printf("buffer: %x\n", (unsigned long)buf[i]);
	}*/
	rqwheel_append_request(w);
	assert(ret > 0);
	q.last ++;
	idx++;
	while (!th->done) {
		wait = epoll_wait(epfd, event, 2, 1000/rate_limit);
		if (wait) {
			ret = recvfrom(sockfd, buf, 256, 0, (struct sockaddr*) &servaddr, &addr_len);
			parse_reply(buf, ret, &udphdr);
			rqwheel_note_udp_reply(w, udphdr);
		} else {
			if (q.size > 0)	{
				q.current = (cycle_timer() - th->tstart) / q.size;
				if (q.last < q.current) {
					compose_packet(buf, idx);
					dgsize = compose(buf+8, sizeof(buf)-8) + 8;
					ret = sendto(sockfd, buf, dgsize, 0, (struct sockaddr*) &servaddr, addr_len);
					rqwheel_append_request(w);
					assert(ret > 0);
					q.last ++;
					idx++;
				}
			}
		}
	}
	/*do {
		if (q.size > 0)	{
			q.current = (cycle_timer() - th->tstart) / q.size;
			if (q.last <= q.current) {
				printf("sendto: %d, th->tstart: %llu\n", idx++, th->tstart);
				ret = sendto(sockfd, packet, sizeof(packet), 0, (struct sockaddr*) &servaddr, addr_len);
				if (ret > 0)
					q.last ++;
			}
		}
		recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*) &servaddr, &addr_len); 
	} while (!th->done);*/
	close(sockfd); 
	return 0;
}

void
print_result(void)
{
	stats_t total;
	int i = 0, cnt = 0, j = 0;
	uint64_t elapsed_usec = 0;
	double cpu_freq = 0;

	memset(&total, 0, sizeof(total));
	total.rtt_min = (uint64_t)-1;

	for (i = 0; i < num; i++) {
		total.rtt_total += threads[i].stats.rtt_total;
		if (total.rtt_min > threads[i].stats.rtt_min) {
			total.rtt_min = threads[i].stats.rtt_min;
		}
		if (total.rtt_max < threads[i].stats.rtt_max) {
			total.rtt_max = threads[i].stats.rtt_max;
		}
		total.nmeasured += threads[i].stats.nmeasured;
		//total.nslow     += threads[i].stats.nslow;
		total.ntimeouts += threads[i].stats.ntimeouts;
		total.nsent     += threads[i].stats.nsent;
		//total.nfailed   += threads[i].stats.nfailed;
		//total.nignore   += threads[i].nignore;
		total.nmade      += threads[i].stats.nmade;
	}

	for (i = 0; i < num; i++) {
		cpu_freq += threads[i].cpu_freq;
		elapsed_usec += (threads[i].tend - threads[i].tstart) / threads[i].cpu_freq;
	}

	elapsed_usec /= num;
	cpu_freq /= num;

	if (total.nsent > 0) {
		uint64_t avg;
		if (total.nmeasured == 0) avg = 0;
		else avg = (uint64_t)(total.rtt_total/total.nmeasured/cpu_freq);
		
		printf("\n\
		Requests sent  : %llu\n\
		Rate per second: %.0f\n\
		Measured RTTs  : %llu\n\
		RTT min/avg/max: %llu/%llu/%llu usec\n\
		Timeouts       : %llu\n\
		Deadline made  : %llu\n",
		total.nsent,
		(double)total.nsent*1000000/elapsed_usec,
		total.nmeasured,
		(uint64_t)(total.rtt_min/cpu_freq),
		avg,
		(uint64_t)(total.rtt_max/cpu_freq),
		total.ntimeouts,
		//total.nfailed,
		//total.nbogus,
		//totals.nignore,
		total.nmade);
	}

	if (total.nmeasured > SAMPLE_NUM)
    	cnt = SAMPLE_NUM;
	else
		cnt = total.nmeasured;
	
	for (i = 0; i < num; i++) {
    	for(j=1; j<cnt; j++) {
	    	if (threads[i].stats.samples[j]) printf("RTT: %llu\n", threads[i].stats.samples[j]);
		}
	}
}

int
main(int argc, char *argv[]) {
	int i  = 0;
	int ret;

	assert(argc > 1);
	getopts(argc, argv);

	for (i = 0; i < 2048; i++) {
		threads[i].idx = i;
		threads[i].done = 0;
	}
	
	for (i = 0; i < num; i++) {
		printf("pthread creation\n");
		ret = pthread_create(&threads[i].pt, NULL, thread_main, threads+i);
		if (ret != 0) {
			perror("thread creation failed");
		}
	}

	signal(SIGINT, stopthread);
	signal(SIGALRM, stopthread);

	if (duration > 0)
		alarm (duration);

	for (i = 0; i < num; i++) {
		ret = pthread_join(threads[i].pt, NULL);
		if (ret != 0) {
			perror("pthread join failed");
		}
	}
	print_result();

	return 0;
}

