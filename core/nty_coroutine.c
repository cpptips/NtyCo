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

#include "nty_coroutine.h"

pthread_key_t global_sched_key;
static pthread_once_t sched_key_once = PTHREAD_ONCE_INIT;


int _switch(nty_cpu_ctx *new_ctx, nty_cpu_ctx *cur_ctx);

#ifdef __i386__
__asm__ (
"    .text                                  \n"
"    .p2align 2,,3                          \n"
".globl _switch                             \n"
"_switch:                                   \n"
"__switch:                                  \n"
"movl 8(%esp), %edx      # fs->%edx         \n"
"movl %esp, 0(%edx)      # save esp         \n"
"movl %ebp, 4(%edx)      # save ebp         \n"
"movl (%esp), %eax       # save eip         \n"
"movl %eax, 8(%edx)                         \n"
"movl %ebx, 12(%edx)     # save ebx,esi,edi \n"
"movl %esi, 16(%edx)                        \n"
"movl %edi, 20(%edx)                        \n"
"movl 4(%esp), %edx      # ts->%edx         \n"
"movl 20(%edx), %edi     # restore ebx,esi,edi      \n"
"movl 16(%edx), %esi                                \n"
"movl 12(%edx), %ebx                                \n"
"movl 0(%edx), %esp      # restore esp              \n"
"movl 4(%edx), %ebp      # restore ebp              \n"
"movl 8(%edx), %eax      # restore eip              \n"
"movl %eax, (%esp)                                  \n"
"ret                                                \n"
);
#elif defined(__x86_64__)

__asm__ (
"    .text                                  \n"
"       .p2align 4,,15                                   \n"
".globl _switch                                          \n"
".globl __switch                                         \n"
"_switch:                                                \n"
"__switch:                                               \n"
"       movq %rsp, 0(%rsi)      # save stack_pointer     \n"
"       movq %rbp, 8(%rsi)      # save frame_pointer     \n"
"       movq (%rsp), %rax       # save insn_pointer      \n"
"       movq %rax, 16(%rsi)                              \n"
"       movq %rbx, 24(%rsi)     # save rbx,r12-r15       \n"
"       movq %r12, 32(%rsi)                              \n"
"       movq %r13, 40(%rsi)                              \n"
"       movq %r14, 48(%rsi)                              \n"
"       movq %r15, 56(%rsi)                              \n"
"       movq 56(%rdi), %r15                              \n"
"       movq 48(%rdi), %r14                              \n"
"       movq 40(%rdi), %r13     # restore rbx,r12-r15    \n"
"       movq 32(%rdi), %r12                              \n"
"       movq 24(%rdi), %rbx                              \n"
"       movq 8(%rdi), %rbp      # restore frame_pointer  \n"
"       movq 0(%rdi), %rsp      # restore stack_pointer  \n"
"       movq 16(%rdi), %rax     # restore insn_pointer   \n"
"       movq %rax, (%rsp)                                \n"
"       ret                                              \n"
);
#endif


static void _exec(void *lt) {
#if defined(__lvm__) && defined(__x86_64__)
	__asm__("movq 16(%%rbp), %[lt]" : [lt] "=r" (lt));
#endif

	nty_coroutine *co = (nty_coroutine*)lt;
	co->func(co->arg);
	co->status |= (BIT(NTY_COROUTINE_STATUS_EXITED) | BIT(NTY_COROUTINE_STATUS_FDEOF) | BIT(NTY_COROUTINE_STATUS_DETACH));
	nty_coroutine_yield(co);

}

extern int nty_schedule_create(int stack_size);



void nty_coroutine_free(nty_coroutine *co) {
	if (co == NULL) return ;
	co->sched->spawned_coroutines --;
#if 1
	if (co->stack) {
		free(co->stack);
		co->stack = NULL;
	}
#endif
	if (co) {
		free(co);
	}

}

//todo
static void nty_coroutine_init(nty_coroutine *co) {

	void **stack = (void **)(co->stack + co->stack_size);

	stack[-3] = NULL;
	stack[-2] = (void *)co;

	co->ctx.esp = (void*)stack - (4 * sizeof(void*));
	co->ctx.ebp = (void*)stack - (3 * sizeof(void*));
	co->ctx.eip = (void*)_exec; //设置回调函数入口
	co->status = BIT(NTY_COROUTINE_STATUS_READY); // 准备就绪
	
}

// 让出CPU，即切换到主协程
void nty_coroutine_yield(nty_coroutine *co) {
	_switch(&co->sched->ctx, &co->ctx);
}

// todo
// 建议内核分配空间？ 
static inline void nty_coroutine_madvise(nty_coroutine *co) {

	size_t current_stack = (co->stack + co->stack_size) - co->ctx.esp;
	assert(current_stack <= co->stack_size);

	if (current_stack < co->last_stack_size &&
		co->last_stack_size > co->sched->page_size) {
		size_t tmp = current_stack + (-current_stack & (co->sched->page_size - 1));
		assert(madvise(co->stack, co->stack_size-tmp, MADV_DONTNEED) == 0);//todo
	}
	co->last_stack_size = current_stack;
}

int nty_coroutine_resume(nty_coroutine *co) {
	// 如果是协程第一次运行，则要进行初始化
	if (co->status & BIT(NTY_COROUTINE_STATUS_NEW)) {
		nty_coroutine_init(co);
	}

	nty_schedule *sched = nty_coroutine_get_sched();
	sched->curr_thread = co; 				//设置当前运行的协程为自己
	_switch(&co->ctx, &co->sched->ctx); 	// 切换上下文
	sched->curr_thread = NULL;

	nty_coroutine_madvise(co);
#if 1
	if (co->status & BIT(NTY_COROUTINE_STATUS_EXITED)) {
		if (co->status & BIT(NTY_COROUTINE_STATUS_DETACH)) {
			printf("nty_coroutine_resume --> \n");
			nty_coroutine_free(co);
		}
		return -1;
	} 
#endif
	return 0;
}


void nty_coroutine_sleep(uint64_t msecs) {
	nty_coroutine *co = nty_coroutine_get_sched()->curr_thread;

	if (msecs == 0) {
		TAILQ_INSERT_TAIL(&co->sched->ready, co, ready_next);
		nty_coroutine_yield(co);
	} else {
		nty_schedule_sched_sleepdown(co, msecs);
	}
}

void nty_coroutine_detach(void) {
	nty_coroutine *co = nty_coroutine_get_sched()->curr_thread;
	co->status |= BIT(NTY_COROUTINE_STATUS_DETACH);
}

static void nty_coroutine_sched_key_destructor(void *data) {
	free(data);
}

static void nty_coroutine_sched_key_creator(void) {
	assert(pthread_key_create(&global_sched_key, nty_coroutine_sched_key_destructor) == 0);
	assert(pthread_setspecific(global_sched_key, NULL) == 0);
	
	return ;
}


// coroutine --> 
// create 
// 协程的创建 
int nty_coroutine_create(nty_coroutine **new_co, proc_coroutine func, void *arg) {
    // key被创建之后，所有线程都可以访问它，各个线程可以往key中写入不同的值
    // 一键多值技术，可以使各个线程通过key来访问数据的时候，访问的是不同的数据
    // 实现此功能有一个关键的数据结构TSD池，相当于一个数组，对应有各个线程的私有值
	// 只初始化一次
	assert(pthread_once(&sched_key_once, nty_coroutine_sched_key_creator) == 0);

	// 获取协程调度器，如果没有则创建
	nty_schedule *sched = nty_coroutine_get_sched();
	if (sched == NULL) {
		nty_schedule_create(0);
		sched = nty_coroutine_get_sched();
		if (sched == NULL) {
			printf("Failed to create scheduler\n");
			return -1;
		}
	}

	// 为协程申请空间
	nty_coroutine *co = calloc(1, sizeof(nty_coroutine));
	if (co == NULL) {
		printf("Failed to allocate memory for new coroutine\n");
		return -2;
	}
	int ret = posix_memalign(&co->stack, getpagesize(), sched->stack_size);//以页对齐分配堆栈空间
	if (ret) {
		printf("Failed to allocate stack for new coroutine\n");
		free(co);
		return -3;
	}

	// 为协程赋值
	co->sched = sched;
	co->stack_size = sched->stack_size;			// 协程栈大小默认为调度器中的值，如有修改也应同步修改上面分配空间
	co->status = BIT(NTY_COROUTINE_STATUS_NEW); // 每个状态对应一个位，方便计算
	co->id = sched->spawned_coroutines ++;		// 生成id
	co->func = func;	// 回调函数						
	co->arg = arg;		// 回调函数参数	
	//文件io 
#if CANCEL_FD_WAIT_UINT64
	co->fd = -1;
	co->events = 0;
#else
	co->fd_wait = -1;
#endif
	
	co->birth = nty_coroutine_usec_now();	// 创建时间戳
	*new_co = co;							// 用于参数返回

	TAILQ_INSERT_TAIL(&co->sched->ready, co, ready_next);//插入到就绪队列中
	return 0;
}




