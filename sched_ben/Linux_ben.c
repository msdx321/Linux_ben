#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bitmap.h>
#include <assert.h>
#include "rbtree.h"
#include "ps_list.h"

#define MAX_LEN              1000
#define RAND_SZ              1000
#define test_len             1000
#define LVL1_BITMAP_SZ       1
#define LVL2_BITMAP_SZ       32
#define DEADLINE_QUANTUM_LEN (100 * 2700)
#define WINDOW_SZ            1024
#define MIN_PERIOD           (500 * 2700)

int NUM_THD = 0;
struct dummy_thd {
	unsigned long long deadline;
	int                prio_idx;
	struct ps_list     list;
} CHACHE_ALIGNED;

struct sched_bitmap {
	unsigned long long  now_quantized;
	unsigned long       lvl1[LVL1_BITMAP_SZ];
	unsigned long       lvl2[LVL2_BITMAP_SZ];
	struct ps_list_head r[WINDOW_SZ];
} __attribute__((aligned(64)));

struct fprr_bitmap {
	unsigned long       lvl1[LVL1_BITMAP_SZ];
	unsigned long       lvl2[LVL2_BITMAP_SZ];
	struct ps_list_head r[WINDOW_SZ];
} __attribute__((aligned(64)));

static unsigned long long   deadline[RAND_SZ];
static int                  prio[32];
static struct dummy_thd     thd[MAX_LEN];
static struct sched_bitmap  runqueue;
static struct sched_bitmap *rq;
static struct fprr_bitmap   fprr;

unsigned long long ro[test_len];
unsigned long long io[test_len];

struct mynode mn[MAX_LEN];
static unsigned long long flush_array[204800];

struct rb_root mytree = RB_ROOT;

static inline void
flush_cache(void)
{
	unsigned long long i = 0;

	while (i < (unsigned long long)204800) {
		flush_array[i] = 0;
		i = i+ (unsigned long long)1;
	}
}

static inline unsigned long long
ben_tsc(void)
{
	//unsigned long a, d, c;

	//__asm__ __volatile__("rdtsc" : "=a" (a), "=d" (d), "=c" (c) : : );

	//return ((unsigned long long)d << 32) | (unsigned long long)a;

	unsigned long long a;
	__asm__ __volatile__("rdtsc":"=a" (a));

	return a;
}

static void
gen_rand(void)
{
	int i, temp = 0;

	for (i = 0; i < RAND_SZ; i++) {
		temp = random() % 10000;
		deadline[i] = (unsigned long long)temp * (unsigned long long)27;
	}

	for (i = 0; i < 32; i++) {
		prio[i] = random() % 32;
	}
}

static void
init(void)
{
	int i = 0;

	memset(deadline, 0, sizeof(unsigned long long) * RAND_SZ);
	memset(prio, 0, sizeof(int)*32);
	memset(thd, 0, sizeof(struct dummy_thd) * NUM_THD);
	memset(&runqueue, 0, sizeof(struct sched_bitmap));
	memset(&fprr, 0, sizeof(struct fprr_bitmap));
	rq = &runqueue;
	gen_rand();

	for (i = 0; i < WINDOW_SZ; i++) {
		ps_list_head_init(&rq->r[i]);
	}

	for (i = 0; i < 32; i++) {
		ps_list_head_init(&fprr.r[i]);
	}
}

static inline void
output(FILE *fp, char* head)
{
	int i = 0;

	assert(fp != NULL);
	for (i = 0; i < test_len; i++) {
		fprintf(fp, "%s: %llu %llu\n", head, ro[i], io[i]);
	}
}

static inline unsigned int
fprr_sched(void)
{
	unsigned int first_lvl1, first_lvl2, pos;

	first_lvl1 = __builtin_ctzl(fprr.lvl1[0]);
	first_lvl2 = __builtin_ctzl(fprr.lvl2[first_lvl1]);
	pos = (first_lvl1 * LVL2_BITMAP_SZ) + first_lvl2;
	assert(!ps_list_head_empty(&fprr.r[pos]));

	return pos;
}

static inline void
fprr_insert(unsigned int pos, struct dummy_thd *thd)
{
	assert(pos < 32);

	bitmap_set(fprr.lvl2, pos);
	bitmap_set(fprr.lvl1, pos/LVL2_BITMAP_SZ);
	//ps_list_rem_d(thd);
	ps_list_head_append_d(&fprr.r[pos], thd);
	assert(!ps_list_head_empty(&fprr.r[pos]));
}

static inline void
fprr_remove(unsigned int pos, struct dummy_thd *thd)
{
	assert(pos < 32);

	ps_list_rem_d(thd);
	if (ps_list_head_empty(&fprr.r[pos])) {
		bitmap_unset(fprr.lvl2, pos);
		if (fprr.lvl2[pos/LVL2_BITMAP_SZ] == 0) {
			bitmap_unset(fprr.lvl1, pos/LVL2_BITMAP_SZ);
		}
	}

/*	if (fprr.l[pos] == 0) {
		bitmap_unset(&fprr.fprr, pos);
	}*/
}

static void
fprr_ben(void)
{
	int i = 0;
	unsigned int pos = 0;
	unsigned long long e, s;
	struct dummy_thd *t;

	for (i = 0; i < NUM_THD; i++) {
		thd[i].prio_idx = prio[i%32];
		assert(thd[i].prio_idx < 32);

		fprr_insert(thd[i].prio_idx, &thd[i]);
	}

	for (i = 0; i < test_len; i++) {
		pos = fprr_sched();
		t = ps_list_head_first_d(&fprr.r[pos], struct dummy_thd);
		assert(!ps_list_head_empty(&fprr.r[pos]));

		flush_cache();
		s = ben_tsc();
		fprr_remove(pos, t);
		e = ben_tsc();
		ro[i] = (e-s);

		t->prio_idx = prio[i%32];

		flush_cache();
		s = ben_tsc();
		fprr_insert(pos, t);
		e = ben_tsc();
		io[i] = (e-s);
	}
}

/*static inline unsigned long long
current_offset(void)
{
	return ben_tsc() / DEADLINE_QUANTUM_LEN;
}*/

static inline unsigned long
rotr32(unsigned long value, unsigned int rotation)
{
	return value >> rotation | value << (32 - rotation);
}

static inline unsigned int
first_bit_lvl1(unsigned long lvl1, unsigned int rotation)
{
	unsigned long rotated;
	unsigned int ret = 0;

	assert(rotation < 32);
	rotated = rotr32(lvl1, rotation);

	ret = __builtin_ctzl(rotated);
	if (ret < (LVL2_BITMAP_SZ - rotation))
		ret += rotation;
	else
		ret -= (LVL2_BITMAP_SZ -rotation);
	
	return ret;
}

static inline int
__find_first_bit(unsigned long bitmap)
{
	return __builtin_ctzl(bitmap);
}

void
bitmap_update_offset()
{
	unsigned long long curr_offset = current_offset();
	unsigned long long now_offset;

	for (now_offset = rq->now_quantized; now_offset < curr_offset; now_offset++) {
		unsigned long now = now_offset % WINDOW_SZ;
		unsigned long next = (now_offset + 1) % WINDOW_SZ;

/*		if (rq->l[now] != 0) {
			assert(rq->l[now] != 0);
			rq->l[next] = rq->l[now] + rq->l[next];
			assert(rq->l[next] != 0);
			rq->l[now] = 0;
			bitmap_unset(rq->lvl2, now);
			if (&rq->lvl2[now/LVL2_BITMAP_SZ] == 0) bitmap_unset(rq->lvl1, now/LVL2_BITMAP_SZ);
			assert(rq->l[now] == 0);
			bitmap_set(rq->lvl2, next);
			bitmap_set(rq->lvl1, next/LVL2_BITMAP_SZ);
			assert(rq->l[next] != 0);
		}*/
		if (!ps_list_head_empty(&rq->r[now])) {
			ps_list_head_append_all(&rq->r[next], &rq->r[now]);
			assert(ps_list_head_empty(&rq->r[now]));
			assert(!ps_list_head_empty(&rq->r[next]));

			bitmap_unset(rq->lvl2, now);
			if (&rq->lvl2[now/LVL2_BITMAP_SZ] == 0) bitmap_unset(rq->lvl1, now/LVL2_BITMAP_SZ);
			bitmap_set(rq->lvl2, next);
			bitmap_set(rq->lvl1, next/LVL2_BITMAP_SZ);
		}
	}
	rq->now_quantized = curr_offset;
}

unsigned long long b_g;

static inline unsigned int
bitmap_sched(void)
{
	unsigned int first_lvl1, first_lvl2, pos;
	unsigned int rotation;
	unsigned long long e, s;

	s = ben_tsc();
	bitmap_update_offset();
	e = ben_tsc();
	printf("##: %llu\n", e-s);
	rotation = rq->now_quantized % WINDOW_SZ;
	first_lvl1 = first_bit_lvl1(rq->lvl1[0], rotation/LVL2_BITMAP_SZ);
	first_lvl2 = __builtin_ctzl(rq->lvl2[first_lvl1]);
	pos = (first_lvl1 * LVL2_BITMAP_SZ) + first_lvl2;
	//assert(rq->l[pos] != 0);
	//printf("pos: %d\n", pos);
	assert(!ps_list_head_empty(&rq->r[pos]));

	return pos;
}

static inline void
bitmap_remove(unsigned int pos, struct dummy_thd *thd)
{
	assert(pos < WINDOW_SZ);

	assert(!ps_list_head_empty(&rq->r[pos]));
	//assert(rq->l[pos] != 0);
	bitmap_update_offset();
	ps_list_rem_d(thd);
	if (ps_list_head_empty(&rq->r[pos])) {
		bitmap_unset(rq->lvl2, pos);
		if (rq->lvl2[pos/LVL2_BITMAP_SZ] == 0) {
			bitmap_unset(rq->lvl1, pos/LVL2_BITMAP_SZ);
		}
	}
	/*rq->l[pos]--;
	if (rq->l[pos] == 0) {
		bitmap_unset(rq->lvl2, pos);
		if (rq->lvl2[pos/LVL2_BITMAP_SZ] == 0) {
			bitmap_unset(rq->lvl1, pos/LVL2_BITMAP_SZ);
		}
	}*/
}

static inline void
bitmap_insert(struct dummy_thd *thd)
{
	unsigned int dl_quantized;

	bitmap_update_offset();
	dl_quantized = thd->deadline / DEADLINE_QUANTUM_LEN;
	dl_quantized %= WINDOW_SZ;
	bitmap_set(rq->lvl2, dl_quantized);
	bitmap_set(rq->lvl1, dl_quantized/LVL2_BITMAP_SZ);

	//rq->l[dl_quantized] ++;
	//assert(rq->l[dl_quantized] != 0);
	ps_list_head_append_d(&rq->r[dl_quantized], thd);
	assert(!ps_list_head_empty(&rq->r[dl_quantized]));
}

void
bitmap_init(void)
{
	unsigned int i = 0;
	b_g = 0;

	//rq->now_quantized = ben_tsc() / DEADLINE_QUANTUM_LEN;
	rq->now_quantized = 0;
	/*for (i = 0; i < WINDOW_SZ; i++) {
		rq->l[i] = 0;
	}*/
	rq->lvl1[0] = 0;
	for (i = 0; i < LVL2_BITMAP_SZ; i++) {
		rq->lvl2[i] = 0;
	}
}

static inline void
bitmap_ben(void)
{
	int i = 0;
	unsigned int pos = 0;
	unsigned long long s, e;
	struct dummy_thd *temp;
	//unsigned long long u = 0, p = 0, a = 0;

	for (i = 0; i < NUM_THD; i++) {
		thd[i].deadline = deadline[i%RAND_SZ];
	}

	bitmap_init();
	for (i = 0; i < NUM_THD; i++) {
		bitmap_insert(&thd[i]);
	}

	for (i = 0; i < test_len; i++) {
		pos = bitmap_sched();
		temp = ps_list_head_first_d(&rq->r[pos], struct dummy_thd);
		
		//flush_cache();
		s = ben_tsc();
		bitmap_remove(pos, temp);
		e = ben_tsc();
		assert(ro[i] == 0);
		ro[i] = (e-s);
		
		thd[i%NUM_THD].deadline = (i+1)*MIN_PERIOD + deadline[i%RAND_SZ];

		//flush_cache();
		s = ben_tsc();
		bitmap_insert(&thd[i%NUM_THD]);
		e = ben_tsc();
		assert(io[i] == 0);
		io[i] = (e-s);
	}
}

extern struct mynode* my_search(struct rb_root *root, unsigned long long deadline);
extern int my_insert(struct rb_root *root, struct mynode *data);
extern struct mynode* my_min(struct rb_root *root);

static inline void
rbtree_ben(void)
{
	int i = 0, ret;
	unsigned long long s, e;
	struct mynode *data, *tmp1, *tmp2;
	struct mynode *tst[NUM_THD];

	for ( i = 0; i < NUM_THD; i++) {
		tst[i] = (struct mynode *)malloc(sizeof(struct mynode));
		tst[i]->deadline = deadline[i%RAND_SZ];
		ret = my_insert(&mytree, tst[i]);
		assert(ret == 1);
		data = my_search(&mytree, tst[i]->deadline);
		assert(data->deadline == tst[i]->deadline);
	}

	for (i = 0; i < test_len; i++) {
		data = my_min(&mytree);
		assert(data);

		flush_cache();
		s = ben_tsc();
		if (data->cnt > 0) {
			data->cnt--;
			e = ben_tsc();
		} else {
			rb_erase(&data->node, &mytree);
			e = ben_tsc();
			free(data);
		}
		ro[i] = (e-s);

		tmp1 = (struct mynode *)malloc(sizeof(struct mynode));
		tmp1->deadline = deadline[i%RAND_SZ] + MIN_PERIOD*(i+1);

		flush_cache();
		s = ben_tsc();
		my_insert(&mytree, tmp1);
		e = ben_tsc();
		io[i] = (e-s);
		
		tmp2 = my_search(&mytree, tmp1->deadline);
		assert(tmp1 == tmp2);
	}

	for (i = 0; i < NUM_THD; i++) {
		free(tst[i]);
	}
}

static inline void
clear_res()
{
	memset(&ro, 0, sizeof(unsigned long long)*test_len);
	memset(&io, 0, sizeof(unsigned long long)*test_len);
}

int main (int argc, char* argv[])
{
	init();
	NUM_THD = atoi(argv[1]);
	char filename[20];
	FILE *fp;

	sprintf(filename, "logs/%s%d.log", argv[1], NUM_THD);
	fp = fopen(filename, "w");

	printf("NUM_THD: %d\n", NUM_THD);
	
	if (strcmp(argv[1], "fprr") == 0) {
		printf("benchmark for fprr\n");
		clear_res();
		fprr_ben();
		output(fp, "fprr");
	} else if (strcmp(argv[1], "rbtree") == 0) {
		printf("\nbenchmark for rbtree\n");
		clear_res();
		rbtree_ben();
		output(fp, "rbtree");
	} else if (strcmp(argv[1], "bitmap") == 0) {
		printf("\nbenchmark for bitmap\n");
		clear_res();
		bitmap_ben();
		output(fp, "bitmap");
	}
	fclose(fp);
	return 0;
}
