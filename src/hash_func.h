#include "uthash.h"
#include <stdio.h>
#include <assert.h>

struct port2pid {
	unsigned long long      iport;
	int                     in_fd[2];
	int                     out_fd[2];
	UT_hash_handle          hh;
};

struct fd2port {
	int                     fd;
	unsigned long long      iport;
	UT_hash_handle          hh;
};

void
add_port2pid (struct port2pid **p2p, unsigned long long iport, int *in_fd, int *out_fd)
{
	struct port2pid *p;

	p = malloc(sizeof(struct port2pid));
	p->iport = iport;
	p->in_fd[0] = in_fd[0];
	p->in_fd[1] = in_fd[1];
	p->out_fd[0] = out_fd[0];
	p->out_fd[1] = out_fd[1];

	HASH_ADD_LL(*p2p, iport, p);
	return;
}

struct port2pid*
find_port2pid(struct port2pid **p2p, unsigned long long iport)
{
	struct port2pid *p;

	HASH_FIND_LL(*p2p, &iport, p);
	return p;
}

void
add_fd2port (struct fd2port **f2p, int fd, unsigned long long iport)
{
	struct fd2port *f;

	f = malloc(sizeof(struct fd2port));
	f->fd = fd;
	f->iport = iport;

	HASH_ADD_INT(*f2p, fd, f);
	return;
}

struct fd2port*
find_fd2port(struct fd2port **f2p, int fd)
{
	struct fd2port *f;

	HASH_FIND_INT(*f2p, &fd, f);
	return f;
}

