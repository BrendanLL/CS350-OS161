
#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include "opt-A2.h"
#include <limits.h>
#include <synch.h>
#include <copyinout.h>
#include <array.h>
#include <test.h>
#include <mips/trapframe.h>

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {
  //DEBUG(DB_SYSCALL,"EXIT!!!!!!!!!!!\n");
  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  //cv_broadcast(p->p_cv,p->p_wait_lock);
  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);

  // as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  //notice all children
  
  //as = curproc_setas(NULL);
  //as_destroy(as);
  as_deactivate();
  as = curproc_setas(NULL);
  as_destroy(as);
    proc_remthread(curthread);
  #if OPT_A2
  //kprintf("abcd %d \n", array_num(&p->p_children));
  for (unsigned int i = array_num(&p->p_children); i > 0 ; i--) {
      struct proc *childproc = array_get(&p->p_children,i-1);
      lock_release(childproc->p_exit_lock);
      array_remove(&p->p_children,i-1);
  }
  array_cleanup(&p->p_children);


  p->canexit = true;
  p->exitcode = _MKWAIT_EXIT(exitcode);
  lock_acquire(p->p_wait_lock);
  cv_broadcast(p->p_cv,p->p_wait_lock);
  lock_release(p->p_wait_lock);
  lock_acquire(p->p_exit_lock);
  //cv_broadcast(p->p_cv,p->p_wait_lock);
  lock_release(p->p_exit_lock);
  //cv_broadcast(p->p_cv,p->p_wait_lock);
  //DEBUG(DB_SYSCALL,"AFter lock!!!!!!!!!!!\n");
  #endif //OPT_A2
    /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  DEBUG(DB_SYSCALL,"destroy 1\n");
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}



/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  #if OPT_A2
    *retval = curproc->p_pid;
  #else
    *retval = 1;
  #endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */
  struct proc *p = NULL;
  struct proc *tem_p;
  for(unsigned int i = 0;i < array_num(&curproc->p_children);i++){
    tem_p = array_get(&curproc->p_children,i);
    if(tem_p->p_pid == pid){
      p = tem_p;
    }
  }
  DEBUG(DB_SYSCALL,"input %d output %d",pid,p->p_pid);
  if(p==NULL){
    //pid proc not found
    DEBUG(DB_SYSCALL,"syscall: waitpid not find");
    return ESRCH;
  }
  if(p == curproc){
    DEBUG(DB_SYSCALL,"syscall: proc cant wait itself");
    return ECHILD;
  }
  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  lock_acquire(p->p_wait_lock);
  //kprintf ("loop!!!!!!!!!!!!\n");
    while(!p->canexit){
      //kprintf("wait for cv call \n");
      cv_wait(p->p_cv,p->p_wait_lock);
      //kprintf("get cv call \n");
    }
  //kprintf ("loopend!!!!!!!!!!!!\n");
  lock_release(p->p_wait_lock);

  exitstatus = p->exitcode;
  result = copyout((void *)&exitstatus,status,sizeof(int));

  if (result) {
    return(result);
  }

  *retval = pid;
  return(0);
}

#if OPT_A2
int 
sys_fork(pid_t *retval,struct trapframe *tf)
{
  //Create process structure for child process
  struct proc *childproc =  proc_create_runprogram(curproc->p_name);
  if (childproc == NULL){//check for errors
    //return error message
    *retval = -1;
    DEBUG(DB_SYSCALL, "error: sys_fork could not create new process.\n");
    return ENPROC;
  }
  //Create and copy address space (and data) from parent to child
  //Attach the newly created address space to the child process structure
  as_copy(curproc_getas(),&(childproc->p_addrspace));
  if(childproc->p_addrspace == NULL){
    //error
    DEBUG(DB_SYSCALL,"syscall: sys_fork addrspace copy fail\n");
    proc_destroy(childproc);
    return ENOMEM;
  }
  struct trapframe *childtf = kmalloc(sizeof(struct trapframe));
  memcpy(childtf,tf,sizeof(struct trapframe));

  //Assign PID to child process and create the parent/child relationship
  /*if(childproc->p_pid == PID_MAX){
    DEBUG(DB_SYSCALL,"syscall: no room for new pid:%d\n",childproc->p_pid);
    return ECHILD;
  }
  array_add(&curproc->p_children,childproc,NULL);*/
  //Create thread for child process (need a safe way to pass the trapframe to the child thread).
  int thread_fork_fail = thread_fork(curthread->t_name, childproc, &enter_forked_process, childtf, 0);
  if(thread_fork_fail){
    DEBUG(DB_SYSCALL, "sys_fork: curren thread fork fail\n");
    proc_destroy(childproc); // removes address space as well
    kfree(childtf);
    childtf = NULL;
    return thread_fork_fail;
  }
  //Child thread needs to put the trapframe onto the stack and modify it so that it returns the current value (and executes the next instruction)

  //Call mips_usermode in the child to go back to userspace
  struct proc *cproc = curproc;
  array_add(&cproc->p_children,childproc,NULL);
  lock_acquire(childproc->p_exit_lock);
  *retval = childproc->p_pid;
  return 0;
}

int sys_execv(char* program, char **args,int32_t *retval) {
  (void) args;
  size_t p_namelen = strlen((char *)program)+1;
  char c_program[p_namelen];//copied promgram name
  int no_arg=0;
  int copy_err;
  int execv_err;

  //copy the promgram path into kernel and check if successed
  copy_err = copyinstr((const_userptr_t)program, c_program, p_namelen, NULL);
  if(copy_err){
    // return a error code
    *retval =  copy_err;
    return -1;
  }

  //estimate space for arguments and number of args
  size_t argslen = 0;
  while(args[no_arg]){
    argslen += strlen(((char**)args)[no_arg])+1; //1 for \0
    no_arg ++;
  }

  //copy args into kernel
  char* c_args[no_arg];
  char arg_char_len[argslen];
  int offset = 0; 
  for (int i = 0; i < no_arg; ++i)
  {
    size_t got;
    size_t arglen = strlen(((char**)args)[i])+1;
    char * haha = arg_char_len + offset;
    copy_err = copyinstr((const_userptr_t)args[i],haha,arglen,&got);
    if(copy_err){
       *retval =  copy_err;
        return -1;
    }
    c_args[i] = haha;
    offset += got;
  }
  DEBUG(DB_SYSCALL,"Going to runprogram no_arg %d \n",no_arg);
  execv_err = runprogram(c_program,c_args,no_arg);
  //should not be into here
  return 1;
}
#else
#endif
