/*
 *  Author : WangBoJing , email : 1989wangbojing@gmail.com
 * 
 *  Copyright Statement:
 *  --------------------
 *  This software is protected by Copyright and the information contained
 *  herein is confidential. The software may not be copied and the information
 *  contained herein may not be used or disclosed except with the written
 *  permission of Author. (C) 2017
 * 
 *

****       *****                                      *****
  ***        *                                       **    ***
  ***        *         *                            *       **
  * **       *         *                           **        **
  * **       *         *                          **          *
  *  **      *        **                          **          *
  *  **      *       ***                          **
  *   **     *    ***********    *****    *****  **                   ****
  *   **     *        **           **      **    **                 **    **
  *    **    *        **           **      *     **                 *      **
  *    **    *        **            *      *     **                **      **
  *     **   *        **            **     *     **                *        **
  *     **   *        **             *    *      **               **        **
  *      **  *        **             **   *      **               **        **
  *      **  *        **             **   *      **               **        **
  *       ** *        **              *  *       **               **        **
  *       ** *        **              ** *        **          *   **        **
  *        ***        **               * *        **          *   **        **
  *        ***        **     *         **          *         *     **      **
  *         **        **     *         **          **       *      **      **
  *         **         **   *          *            **     *        **    **
*****        *          ****           *              *****           ****
                                       *
                                      *
                                  *****
                                  ****



 *
 */


#ifndef __NTY_COROUTINE_H__
#define __NTY_COROUTINE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <netinet/tcp.h>

#include <sys/epoll.h>
#include <sys/poll.h>

#include <errno.h>

#include "nty_queue.h"
#include "nty_tree.h"

#define NTY_CO_MAX_EVENTS		(1024*1024)
#define NTY_CO_MAX_STACKSIZE	(16*1024) // {http: 16*1024, tcp: 4*1024}

#define BIT(x)	 				(1 << (x))
#define CLEARBIT(x) 			~(1 << (x))

#define CANCEL_FD_WAIT_UINT64	1

typedef void (*proc_coroutine)(void *);


typedef enum {
	NTY_COROUTINE_STATUS_WAIT_READ,
	NTY_COROUTINE_STATUS_WAIT_WRITE,
	NTY_COROUTINE_STATUS_NEW,
	NTY_COROUTINE_STATUS_READY,
	NTY_COROUTINE_STATUS_EXITED,
	NTY_COROUTINE_STATUS_BUSY,
	NTY_COROUTINE_STATUS_SLEEPING,
	NTY_COROUTINE_STATUS_EXPIRED,
	NTY_COROUTINE_STATUS_FDEOF,
	NTY_COROUTINE_STATUS_DETACH,
	NTY_COROUTINE_STATUS_CANCELLED,
	NTY_COROUTINE_STATUS_PENDING_RUNCOMPUTE,
	NTY_COROUTINE_STATUS_RUNCOMPUTE,
	NTY_COROUTINE_STATUS_WAIT_IO_READ,
	NTY_COROUTINE_STATUS_WAIT_IO_WRITE,
	NTY_COROUTINE_STATUS_WAIT_MULTI
} nty_coroutine_status;

typedef enum {
	NTY_COROUTINE_COMPUTE_BUSY,
	NTY_COROUTINE_COMPUTE_FREE
} nty_coroutine_compute_status;

typedef enum {
	NTY_COROUTINE_EV_READ,
	NTY_COROUTINE_EV_WRITE
} nty_coroutine_event;


LIST_HEAD(_nty_coroutine_link, _nty_coroutine);
TAILQ_HEAD(_nty_coroutine_queue, _nty_coroutine);

RB_HEAD(_nty_coroutine_rbtree_sleep, _nty_coroutine);
RB_HEAD(_nty_coroutine_rbtree_wait, _nty_coroutine);



typedef struct _nty_coroutine_link nty_coroutine_link;
typedef struct _nty_coroutine_queue nty_coroutine_queue;

typedef struct _nty_coroutine_rbtree_sleep nty_coroutine_rbtree_sleep;
typedef struct _nty_coroutine_rbtree_wait nty_coroutine_rbtree_wait;

// 协程上下文
typedef struct _nty_cpu_ctx {
	void *esp; //栈指针寄存器(extended stackpointer)
	void *ebp;
	void *eip;
	void *edi;
	void *esi;
	void *ebx;
	void *r1;
	void *r2;
	void *r3;
	void *r4;
	void *r5;
} nty_cpu_ctx;

// 调度器
typedef struct _nty_schedule {
	uint64_t birth;					//调度器的创建时间
	nty_cpu_ctx ctx;				//cpu上下文 todo:估计是当前正在运行协程的上下文
	void *stack;					//栈指针
	size_t stack_size;				//栈大小
	int spawned_coroutines;			//协程id生成器
	uint64_t default_timeout;		//默认超时时间
	struct _nty_coroutine *curr_thread; //当前协程
	int page_size;					//页大小

	int poller_fd;
	int eventfd;
	struct epoll_event eventlist[NTY_CO_MAX_EVENTS];
	int nevents;

	int num_new_events;
	nty_coroutine_link busy;
	nty_coroutine_rbtree_sleep sleeping;	//休眠树
	nty_coroutine_rbtree_wait waiting;		//等待树
	nty_coroutine_queue ready;				//就绪队列
} nty_schedule;

/*******************************
 * 协程作为一个核心结构体
 * 主要包括两部分内容
 * 一个是调度器相关的属性
 * 一个是协程本身的上下文
********************************/
typedef struct _nty_coroutine {
	// 调度相关属性
	uint64_t id;		 //协程id,用来唯一标识协程实例
	uint64_t birth;		 //协程的创建时间戳
	nty_schedule *sched; //调度器指针
	/// 协程的状态
	nty_coroutine_status status;
	RB_ENTRY(_nty_coroutine) sleep_node; 	//休眠树
	RB_ENTRY(_nty_coroutine) wait_node;		//等待树
	TAILQ_ENTRY(_nty_coroutine) ready_next;	//就绪队列
	uint64_t sleep_usecs; 					//休眠计数器

	// 协程上下文相关属性
	nty_cpu_ctx ctx; 			//cpu上下文
	proc_coroutine func;		//回调函数
	void *arg;					//回调函数参数
	void *stack;				//栈指针
	size_t stack_size;			//栈大小
	size_t last_stack_size;		// todo:
	/// 文件io
#if CANCEL_FD_WAIT_UINT64
	int fd;
	unsigned short events;  //POLL_EVENT
#else
	int64_t fd_wait;
#endif	
} nty_coroutine;

 

extern pthread_key_t global_sched_key;
static inline nty_schedule *nty_coroutine_get_sched(void) {
	return pthread_getspecific(global_sched_key);
}

static inline uint64_t nty_coroutine_diff_usecs(uint64_t t1, uint64_t t2) {
	return t2-t1;
}

static inline uint64_t nty_coroutine_usec_now(void) {
	struct timeval t1 = {0, 0};
	gettimeofday(&t1, NULL);

	return t1.tv_sec * 1000000 + t1.tv_usec;
}



int nty_epoller_create(void);


void nty_schedule_cancel_event(nty_coroutine *co);
void nty_schedule_sched_event(nty_coroutine *co, int fd, nty_coroutine_event e, uint64_t timeout);

void nty_schedule_desched_sleepdown(nty_coroutine *co);
void nty_schedule_sched_sleepdown(nty_coroutine *co, uint64_t msecs);

nty_coroutine* nty_schedule_desched_wait(int fd);
void nty_schedule_sched_wait(nty_coroutine *co, int fd, unsigned short events, uint64_t timeout);

void nty_schedule_run(void);

int nty_epoller_ev_register_trigger(void);
int nty_epoller_wait(struct timespec t);
int nty_coroutine_resume(nty_coroutine *co);
void nty_coroutine_free(nty_coroutine *co);
int nty_coroutine_create(nty_coroutine **new_co, proc_coroutine func, void *arg);
void nty_coroutine_yield(nty_coroutine *co);

void nty_coroutine_sleep(uint64_t msecs);


int nty_socket(int domain, int type, int protocol);
int nty_accept(int fd, struct sockaddr *addr, socklen_t *len);
ssize_t nty_recv(int fd, void *buf, size_t len, int flags);
ssize_t nty_send(int fd, const void *buf, size_t len, int flags);
int nty_close(int fd);
int nty_poll(struct pollfd *fds, nfds_t nfds, int timeout);
int nty_connect(int fd, struct sockaddr *name, socklen_t namelen);

ssize_t nty_sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t nty_recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);








#endif


