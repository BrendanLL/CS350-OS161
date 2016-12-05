/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <vfs.h>
#include <synch.h>
#include <kern/fcntl.h>  
#include <array.h>
#include "opt-A2.h"
#include <limits.h>
/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/*
 * Mechanism for making the kernel menu thread sleep while processes are running
 */
#ifdef UW
/* count of the number of processes, excluding kproc */
static volatile unsigned int proc_count;
/* provides mutual exclusion for proc_count */
/* it would be better to use a lock here, but we use a semaphore because locks are not implemented in the base kernel */ 
static struct semaphore *proc_count_mutex;
/* used to signal the kernel menu thread when there are no processes */
struct semaphore *no_proc_sem;   
#endif  // UW


/*
 * Create a proc structure.
 */
pid_t base_pid = PID_MIN;

#if OPT_A2

unsigned int get_index_proc(pid_t pid){	
	if(procs_list == NULL) return PID_MAX;
	(void)pid;
	for(unsigned int i=0;i<array_num(procs_list);++i){
		struct proc *p = array_get(procs_list,i);
		//DEBUG(DB_SYSCALL,"get_index %d %d pid:%d match p->pid %d\n",i,array_num(procs_list),pid,p->p_pid);
		if(p->p_pid == pid) return i;
	}
	return PID_MAX;
}

pid_t find_pid(){ 
	//DEBUG(DB_SYSCALL,"find_pid \n");
	if(!procs_list) return PID_MAX;
	for(unsigned i = PID_MIN; i < PID_MAX - PID_MIN;i++){
		//DEBUG(DB_SYSCALL,"check %d is avali\n",i);
		if(get_index_proc(i)==PID_MAX){
			//DEBUG(DB_SYSCALL,"find! \n");
			//base_pid++;
			//if(base_pid > PID_MAX-1) base_pid=PID_MIN;
			return i;
		}
	}
	//return PID_MAX if not find*/
	//DEBUG(DB_SYSCALL,"not find \n");
	return PID_MAX;
}
#endif

static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	threadarray_init(&proc->p_threads);
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

#ifdef UW
	proc->console = NULL;
#endif // UW

#if OPT_A2
	
	proc->p_pid = find_pid();
	if(proc->p_pid==PID_MAX){
		DEBUG(DB_SYSCALL,"no pid space");
	}
	proc->canexit = false;
	proc->exitcode = 0;
	

	proc->p_exit_lock = lock_create("p_exit_lock");
	if(proc->p_exit_lock==NULL){ //lock crate false
		kfree(proc->p_name);
		kfree(proc);
		DEBUG(DB_SYSCALL,"no lock space");
		return NULL;
	}

	proc->p_cv = cv_create("p_cv");
	if(proc->p_cv==NULL){ //lock crate false
		DEBUG(DB_SYSCALL,"no cv lock space");
		lock_destroy(proc->p_exit_lock);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}
	
	proc->p_wait_lock = lock_create("p_wait_lock");
	if(proc->p_wait_lock==NULL){ //lock crate false
		DEBUG(DB_SYSCALL,"no lock space");
		lock_destroy(proc->p_exit_lock);
		cv_destroy(proc->p_cv);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}
	proc->p_children = *array_create();
	array_init(&proc->p_children);
	if(procs_list == NULL){
		procs_list = array_create();
		array_init(procs_list);
		DEBUG(DB_SYSCALL,"all porc init: size %d",array_num(procs_list));
	}
	array_add(procs_list,proc,NULL);
	DEBUG(DB_SYSCALL,"add proc pid: %d index:%d \n",proc->p_pid,get_index_proc(proc->p_pid));
	
#else
#endif //OPT_A2
	return proc;
}

/*
 * Destroy a proc structure.
 */
void
proc_destroy(struct proc *proc)
{
	DEBUG(DB_SYSCALL,"start destroy children leave:%d \n",array_num(&proc->p_children));
	/*
         * note: some parts of the process structure, such as the address space,
         *  are destroyed in sys_exit, before we get here
         *
         * note: depending on where this function is called from, curproc may not
         * be defined because the calling thread may have already detached itself
         * from the process.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);
/*
	array_remove(procs_list, get_index_proc(proc->p_pid));
		if(array_num(procs_list)==0){
			DEBUG(DB_SYSCALL,"array cleanup\n");
			array_cleanup(procs_list);
			array_destroy(procs_list);
			procs_list = NULL;
		}*/
	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}


#ifndef UW  // in the UW version, space destruction occurs in sys_exit, not here
	if (proc->p_addrspace) {
		/*
		 * In case p is the currently running process (which
		 * it might be in some circumstances, or if this code
		 * gets moved into exit as suggested above), clear
		 * p_addrspace before calling as_destroy. Otherwise if
		 * as_destroy sleeps (which is quite possible) when we
		 * come back we'll be calling as_activate on a
		 * half-destroyed address space. This tends to be
		 * messily fatal.
		 */
		struct addrspace *as;

		as_deactivate();
		as = curproc_setas(NULL);
		as_destroy(as);
	}
#endif // UW

#ifdef UW
	if (proc->console) {
	  vfs_close(proc->console);
	}
#endif // UW
	//DEBUG(DB_SYSCALL,"thread cleanup\n");
	threadarray_cleanup(&proc->p_threads);
	//DEBUG(DB_SYSCALL,"spinlock cleanup\n");
	spinlock_cleanup(&proc->p_lock);
#if OPT_A2
	//DEBUG(DB_SYSCALL,"remove array_num: %d, index %d,pid: %d",array_num(procs_list),get_index_proc(proc->p_pid),proc->p_pid);
	struct proc * pp = array_get(procs_list,get_index_proc(proc->p_pid));
	if(pp->p_pid!=PID_MAX){
		array_remove(procs_list, get_index_proc(proc->p_pid));
	}else{
		DEBUG(DB_SYSCALL,"no such proc\n");
	}
	if(array_num(procs_list)==0){
		DEBUG(DB_SYSCALL,"array cleanup\n");
		array_cleanup(procs_list);
		array_destroy(procs_list);
		procs_list = NULL;
	}
	DEBUG(DB_SYSCALL,"children leave2:%d\n",array_num(&proc->p_children));
	array_cleanup(&proc->p_children);
	DEBUG(DB_SYSCALL,"children cleanup finish\n");
	lock_destroy(proc->p_exit_lock);
	lock_destroy(proc->p_wait_lock);
	cv_destroy(proc->p_cv);
#endif
	kfree(proc->p_name);
	kfree(proc);

#ifdef UW
	/* decrement the process count */
        /* note: kproc is not included in the process count, but proc_destroy
	   is never called on kproc (see KASSERT above), so we're OK to decrement
	   the proc_count unconditionally here */
	P(proc_count_mutex); 
	KASSERT(proc_count > 0);
	proc_count--;
	/* signal the kernel menu thread if the process count has reached zero */
	if (proc_count == 0) {
	  V(no_proc_sem);
	}
	V(proc_count_mutex);
#endif // UW
	

}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
  kproc = proc_create("[kernel]");
  if (kproc == NULL) {
    panic("proc_create for kproc failed\n");
  }
#ifdef UW
  proc_count = 0;
  proc_count_mutex = sem_create("proc_count_mutex",1);
  if (proc_count_mutex == NULL) {
    panic("could not create proc_count_mutex semaphore\n");
  }
  no_proc_sem = sem_create("no_proc_sem",0);
  if (no_proc_sem == NULL) {
    panic("could not create no_proc_sem semaphore\n");
  }
#endif // UW 
#if OPT_A2
  //procs_list = array_create();
  //array_init(procs_list);
  /*array_setsize(procs_list, PID_MAX - PID_MIN+1);
  for(unsigned int i=0;i<PID_MAX - PID_MIN+1;i++){
			array_set(procs_list,i,NULL);
		}*/
  /* initial the new element pid_list its lock
  procs_list = array_create();
  procs_lock = lock_create("pid_list_lock");
  array_setsize(pid_list,PID_MAX);
  array_init(procs_list);*/
#else
#endif
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */


struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *proc;
	char *console_path;

	proc = proc_create(name);
	if (proc == NULL) {
		return NULL;
	}

#ifdef UW
	/* open the console - this should always succeed */
	console_path = kstrdup("con:");
	if (console_path == NULL) {
	  panic("unable to copy console path name during process creation\n");
	}
	if (vfs_open(console_path,O_WRONLY,0,&(proc->console))) {
	  panic("unable to open the console during process creation\n");
	}
	kfree(console_path);
#endif // UW
	  
	/* VM fields */

	proc->p_addrspace = NULL;

	/* VFS fields */

#ifdef UW
	/* we do not need to acquire the p_lock here, the running thread should
           have the only reference to this process */
        /* also, acquiring the p_lock is problematic because VOP_INCREF may block */
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		proc->p_cwd = curproc->p_cwd;
	}
#else // UW
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		proc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);
#endif // UW

#ifdef UW
	/* increment the count of processes */
        /* we are assuming that all procs, including those created by fork(),
           are created using a call to proc_create_runprogram  */
	P(proc_count_mutex); 
	proc_count++;
	V(proc_count_mutex);
#endif // UW

#if OPT_A2
	//inital locks
	
	// initialize children proc array
	/*if(procs_list == NULL){
		procs_list = array_create();
		//array_setsize(PID_MAX);
		array_init(procs_list);
	}
	proc->p_pid = find_pid();
	array_add(procs_list,proc,NULL);
	proc->p_children = *array_create();
	array_init(&proc->p_children); */
#else
#endif
	return proc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int result;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	result = threadarray_add(&proc->p_threads, t, NULL);
	spinlock_release(&proc->p_lock);
	if (result) {
		return result;
	}
	t->t_proc = proc;
	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	unsigned i, num;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	/* ugh: find the thread in the array */
	num = threadarray_num(&proc->p_threads);
	for (i=0; i<num; i++) {
		if (threadarray_get(&proc->p_threads, i) == t) {
			//DEBUG(DB_SYSCALL,"remove thread array_num: %d",array_num(&proc->p_threads));
			threadarray_remove(&proc->p_threads, i);
			spinlock_release(&proc->p_lock);
			t->t_proc = NULL;
			return;
		}
	}
	/* Did not find it. */
	spinlock_release(&proc->p_lock);
	panic("Thread (%p) has escaped from its process (%p)\n", t, proc);
}

/*
 * Fetch the address space of the current process. Caution: it isn't
 * refcounted. If you implement multithreaded processes, make sure to
 * set up a refcount scheme or some other method to make this safe.
 */
struct addrspace *
curproc_getas(void)
{
	struct addrspace *as;
#ifdef UW
        /* Until user processes are created, threads used in testing 
         * (i.e., kernel threads) have no process or address space.
         */
	if (curproc == NULL) {
		return NULL;
	}
#endif
	spinlock_acquire(&curproc->p_lock);
	as = curproc->p_addrspace;
	spinlock_release(&curproc->p_lock);
	return as;
}

/*
 * Change the address space of the current process, and return the old
 * one.
 */
struct addrspace *
curproc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
