#define _GNU_SOURCE

//#ifdef EOS_EXPR
//#undef EOS_EXPR
//#endif

#define EOS_EXPR

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
#include <sched.h>
#include "epoll_helper.h"
#include "cpu.h"

#define BASE_PORT 11211
#define SAMPLE_NUM (10000+1)
#define succ(n, size) (((n)+1) % (size))
#define KEYPREFIX "kkkkkkkkk"
#define HI_STIME 5000
#define LO_STIME 1

typedef struct stats_t {
	uint64_t rtt_total,
             rtt_min,
			 rtt_max;
	uint64_t nmeasured,
             ntimeouts,
			 nsent,
			 nignore,
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
	pthread_t    pt;
	stats_t      stats;
	int          stime;
	int          cport;
	int          sport;
	double       cpu_freq;
	uint64_t     tstart;
	uint64_t     tend;
	uint64_t     deadline;
	uint16_t     rl;

	volatile int done;
	volatile int stop;
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
thread_t threads[2048];
static int nb_hi = 0;
pthread_attr_t attr[2048];
static int duration = 0;
int HI_RATE = 20;
int LO_RATE = 50;
int malicious = 0;

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
stoprecv(int sig)
{
	int  i = 0;
	printf("stop recv\n");
	for (i = 0; i < num; i++) {
		threads[i].stop = 1;
	}
}

void
stopthread(int sig)
{
	int i;

	printf("stop thread\n");
	for (i = 0; i < num; i++) {
		threads[i].done = 1;
		//threads[i].stop = 1;
	}

	signal(SIGALRM, stoprecv);
	alarm(10);
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
stats_update_rtts(stats_t *st, uint64_t tsent, uint64_t treply, double cpufreq, uint64_t deadline)
{
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
		assert(rtt >= st->rtt_min);
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
	/* request wheel is empty. Ignore this reply. */
	//	if (!quiet) {
	//		fprintf(stderr, "Got a UDP reply with id %d for key %d with empty "
	//				"request queue!\n", (int)rs.rqid, k);
	//	}
		w->th->stats.nignore++;
	//	return;

	}
	match = (w->tail + (uint32_t)rqdistance) % w->size;

	/* verify that _match_ is in [tail..last] modulo w->size. If it is not,
	   the reply is for a request that's no longer in the queue. The timedout
	   counter has already been incremented in rqwheel_append_request(). */
	if (w->tail <= last) {
		if (match < w->tail || last < match) {
			w->th->stats.nignore++;
			return;
		}
	} else if (last < match && match < w->tail) {
		w->th->stats.nignore++;
		return;
	}

	rq = &w->rqs[match];

	/* request ids wrapped around AND the matching request is no longer in
	   the queue. The request has already been counted as timed out in
	   rqwheel_append_request() that bumped the request record off
	   the queue. */
	if ((uint16_t)rq->id != rs.rqid) {
		/*if (!quiet) {
			printf("Got reply for request id %u, expected %u\n",
			(unsigned)rs.rqid, (unsigned)(rq->id & 0xffff));
		}*/
		w->th->stats.nignore++;
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
		w->th->stats.nignore++;
		return;
	}

	rq->npartsleft--;

	/* if (rs.partno == 0 && k < 0 && !quiet) { */
	/*   fprintf(stderr, "'get' request for key %d failed\n", rq->key); */
	/* } */

	if (rq->npartsleft > 0) {
		w->th->stats.nignore++;
		return;
	}

	/* we got all reply parts, mark request completed */

	rq->treply = cycle_timer();

	stats_update_rtts(&w->th->stats, rq->tsent, rq->treply,
					   w->th->cpu_freq, w->th->deadline);

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
		} else if (strcmp(argv[count], "-rl") == 0) {
			if (++count < argc)
				LO_RATE = atoi(argv[count]);
		} else if (strcmp(argv[count], "-rh") == 0) {
			if (++count < argc)
				HI_RATE = atoi(argv[count]);
		} else if (strcmp(argv[count], "-d") == 0) {
			if (++count < argc)
				duration = atoi(argv[count]);
		} else if (strcmp(argv[count], "-h") == 0){
			if (++count < argc)
				nb_hi = atoi(argv[count]);
		} else if (strcmp(argv[count], "-m") == 0){
			if (++count < argc)
				malicious = atoi(argv[count]);
		} else {
			assert(0);
		}
		count ++;
	}
	//printf("num: %d, rate: %d, duration: %d\n", num, rate_limit, duration);
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
compose(char *buf, int bufsize, int spin_time)
{
	//printf("spin_time: %d\n", spin_time);
	int ret = snprintf(buf, bufsize, "get " KEYPREFIX "-%06d\r\n", spin_time);
	return ret;
}

unsigned long long old;
static void *
thread_main(void* arg) 
{
	thread_t *th = (thread_t *)arg;
	quantum_t q;
	int sockfd; 
	struct sockaddr_in servaddr, cliaddr;
	int ret = 0;
	int wait = 0;
	udphdr_t udphdr;
	socklen_t addr_len = sizeof(struct sockaddr_in);
	char buf[256];
	rqwheel_t *w;
	int dgsize;
	int idx = 0;

	memset(&(th->stats), 0, sizeof(stats_t));
	th->stats.rtt_min = -1;
	//printf("ignore: %d\n", th->stats.nignore);
	w = (rqwheel_t *)malloc(sizeof(rqwheel_t));
	th->tstart = cycle_timer();
	block_signals();
	th->cpu_freq = get_cpu_frequency();
	//printf("rate: %d\n", th->rl);
	quantum_init(&q, (th->rl > 0) ?
					  th->cpu_freq * 1000000 / th->rl : 0);

	rqwheel_init(w);
	w->th = th;

	//printf("thread main-> quantum size: %lld, rate: %d, freq: %lf\n", q.size, th->rl, th->cpu_freq);
	sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
	if (sockfd < 0) {
		perror("socket creation failed");
		exit(0); 
	} 
	memset(&servaddr, 0, sizeof(servaddr)); 
	memset(&cliaddr, 0, sizeof(cliaddr)); 

	cliaddr.sin_family = AF_INET;
	cliaddr.sin_addr.s_addr = inet_addr("10.10.1.1");
	cliaddr.sin_port = htons(th->cport);
	//printf("client port: %d\n", BASE_PORT + th->idx);

	if (bind(sockfd, (struct sockaddr *)&cliaddr, addr_len) < 0) {
		perror("bind fail");
		exit(0);
	}

	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = inet_addr("10.10.1.2");
	servaddr.sin_port = htons(th->sport);
	//printf("sport: %d, cport: %d\n", th->sport, th->cport);
	setnonblocking(sockfd);

	int epfd = epoll_create(2);
	if (epfd < 0) {
		perror("epoll creation failed");
		exit(-1);
	}

	static struct epoll_event event[2];
	addfd(epfd, sockfd);

	compose_packet(buf, idx);
	dgsize = compose(buf+8, sizeof(buf)-8, th->stime) + 8;
	ret = sendto(sockfd, buf, dgsize, 0, (struct sockaddr*) &servaddr, addr_len);
	if (ret < 0) {
		perror("fail");	
	}
	//printf("deadline: %lu\n", th->deadline);
	idx++;
	/*for (i=0; i < dgsize; i++) {
		printf("buffer: %x\n", (unsigned long)buf[i]);
	}*/
	rqwheel_append_request(w);
	assert(ret > 0);
	q.last ++;

	while (!th->done) {
		wait = epoll_wait(epfd, event, 2, 1);
		if (wait) {
			while (!th->done) {
				ret = recvfrom(sockfd, buf, 256, 0, (struct sockaddr*) &servaddr, &addr_len);
				if (ret < 0) {
					assert(errno == EAGAIN);
					break;
				}

				//assert(ret > 0);
				parse_reply(buf, ret, &udphdr);
				rqwheel_note_udp_reply(w, udphdr);
				//printf("overehad: %llu, cpufreq:%lf\n", ee-ss, th->cpu_freq);
			}
		} else {
			assert(q.size > 0);
			q.current = (cycle_timer() - th->tstart) / q.size;
			while (q.last < q.current) {
				compose_packet(buf, idx);
				dgsize = compose(buf+8, sizeof(buf)-8, th->stime) + 8;
				//do {
			//if (th->sport)
				//printf("[stime: %llu,inter_arrival, q.size]: %llu, %llu, %llu us\n", th->stime, inter_arrival, q.size);
					ret = sendto(sockfd, buf, dgsize, 0, (struct sockaddr*) &servaddr, addr_len);
				//} while (errno == EAGAIN && !th->done);
				idx++;
				rqwheel_append_request(w);
				assert(ret > 0);
				q.last ++;
				//q.last = q.current;
			}
			//printf("-------\n");
		}
	}
	while (!th->stop) {
		ret = recvfrom(sockfd, buf, 256, 0, (struct sockaddr*) &servaddr, &addr_len);
		if (ret < 0) {
			assert(errno == EAGAIN);
			continue;
		}
		parse_reply(buf, ret, &udphdr);
		rqwheel_note_udp_reply(w, udphdr);
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
printout(stats_t *total, int is_hi, int start, int end)
{
	uint64_t avg;
	FILE * pfile;
	int i = 0, j=0, cnt = 0;
	uint64_t elapsed_usec = 0;
	double cpu_freq = 0;

	for (i = start; i < end; i++) {
		cpu_freq += threads[i].cpu_freq;
		elapsed_usec += (threads[i].tend - threads[i].tstart) / threads[i].cpu_freq;
	}

	elapsed_usec /= num;
	cpu_freq /= num;

	if (is_hi) 
		pfile=fopen("hi.log", "w+");
	else
		pfile=fopen("lo.log", "w+");

	if (total->nsent > 0) {
		if (total->nmeasured == 0) avg = 0;
		else avg = (uint64_t)(total->rtt_total/total->nmeasured/cpu_freq);
		
		fprintf(pfile,
"\n\
Requests sent  : %lu\n\
Rate per second: %.0f\n\
Measured RTTs  : %lu\n\
RTT min/avg/max: %lu/%lu/%lu usec\n\
Timeouts       : %lu\n\
Ignored pkts   : %lu\n\
Deadline made  : %lu\n",
		total->nsent,
		(double)total->nsent*1000000/elapsed_usec,
		total->nmeasured,
		(uint64_t)(total->rtt_min/cpu_freq),
		avg,
		(uint64_t)(total->rtt_max/cpu_freq),
		total->ntimeouts,
		//total.nfailed,
		//total.nbogus,
		total->nignore,
		total->nmade);
	}

	if (total->nmeasured > SAMPLE_NUM)
    	cnt = SAMPLE_NUM;
	else
		cnt = total->nmeasured;
	
	printf("SAMPLE_NUM: %d, start: %d, end: %d\n", SAMPLE_NUM, start, end);
	int it = 0;
	for (i = start; i < end; i++) {
    	for(j=1; j<cnt; j++) {
	    	if (threads[i].stats.samples[j]) {
				fprintf(pfile,"RTT: %lu\n", threads[i].stats.samples[j]);
				it ++;
			}
		}
	}
	printf("it: %d\n", it);
	fclose(pfile);
}

void
print_result(void)
{
	stats_t total[2];
	int i = 0, t = 0;

	memset(&total, 0, sizeof(stats_t)*2);
	total[0].rtt_min = (uint64_t)-1;
	total[1].rtt_min = (uint64_t)-1;

	for (i = 0; i < num; i++) {
		if (threads[i].stime == 11211)	continue;
		if (threads[i].stime > 200) t = 1;
		else t = 0;

		total[t].rtt_total += threads[i].stats.rtt_total;
		if (total[t].rtt_min > threads[i].stats.rtt_min) {
			total[t].rtt_min = threads[i].stats.rtt_min;
		}
		if (total[t].rtt_max < threads[i].stats.rtt_max) {
			total[t].rtt_max = threads[i].stats.rtt_max;
		}
		total[t].nmeasured += threads[i].stats.nmeasured;
		//total.nslow     += threads[i].stats.nslow;
		total[t].ntimeouts += threads[i].stats.ntimeouts;
		total[t].nsent     += threads[i].stats.nsent;
		//total.nfailed   += threads[i].stats.nfailed;
		total[t].nignore   += threads[i].stats.nignore;
		total[t].nmade      += threads[i].stats.nmade;
	}

	printout(&total[0], 0, nb_hi, num);
	printout(&total[1], 1, 0, nb_hi);
}

int
main(int argc, char *argv[]) {
	int i  = 0;
	int ret;
	int cpu_num = 48;
	cpu_set_t cpuset;

	assert(argc > 1);
	getopts(argc, argv);
	
	for (i = 0; i < 2048; i++) {
		threads[i].done = 0;
		threads[i].stop = 0;
		threads[i].cport = BASE_PORT + i;
#ifdef EOS_EXPR
		if (i < nb_hi) {
			threads[i].stime = HI_STIME;
			threads[i].sport = BASE_PORT + HI_STIME;
			threads[i].deadline = (unsigned long long)500000 ;
	//		threads[i].deadline = (unsigned long long)1000000 / HI_RATE;
			threads[i].rl = HI_RATE;

		} else if (i >= nb_hi && i < (nb_hi + malicious)) {
			threads[i].stime = 11211;
			threads[i].sport = BASE_PORT + 11211;
			threads[i].deadline= (unsigned long long)500000;
			threads[i].rl = 1;
		} else {
			threads[i].stime = LO_STIME;
			threads[i].sport = BASE_PORT + LO_STIME;
			threads[i].deadline = (unsigned long long)5000;
		//	threads[i].deadline = (unsigned long long)1000000 / LO_RATE;
			threads[i].rl = LO_RATE;
		}
#else
		if (i < nb_hi) {
			threads[i].stime = HI_STIME;
			threads[i].sport = threads[i].cport;
			//threads[i].deadline = (unsigned long long)1000000 / HI_RATE;
			threads[i].deadline = (unsigned long long)500000;
			threads[i].rl = HI_RATE;
		} else if (i >= nb_hi && i < (nb_hi + malicious)){
			threads[i].stime = 11211;
			threads[i].sport = threads[i].cport;
			threads[i].deadline = (unsigned long long)500000;
			threads[i].rl = 1;
		} else {
			threads[i].stime = LO_STIME;
			threads[i].sport = threads[i].cport;
			threads[i].deadline = (unsigned long long)5000;
			//threads[i].deadline = (unsigned long long)1000 * 2;
			threads[i].rl = LO_RATE;
		}
#endif
	}

	CPU_ZERO(&cpuset);
	for (i = 0; i < cpu_num; i++) {
		CPU_SET(i, &cpuset);
	}
	
	for (i = 0; i < num; i++) {
		//printf("pthread creation\n");
		pthread_attr_init(&attr[i]);
		ret = pthread_attr_setaffinity_np(&attr[i], sizeof(cpu_set_t), &cpuset);
		assert(ret == 0);
		ret = pthread_create(&threads[i].pt, &attr[i], thread_main, threads+i);
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

