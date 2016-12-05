/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <limits.h>
#include <copyinout.h>
#include "opt-A2.h"

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */

int
runprogram(char *progname,char **args,unsigned long no_arg)
{
	DEBUG(DB_SYSCALL,"runprogram no_arg %lu \n",no_arg);
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}



	/* We should be a new process. */
	if(curproc_getas()){
		as_deactivate();
	}
	//KASSERT(curproc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}
	/* Switch to it and activate it. */
	struct addrspace * pre_as = curproc_setas(as);
	if (pre_as != NULL) {
		as_destroy(pre_as);
	}
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

#if OPT_A2
	DEBUG(DB_SYSCALL,"runprogram point 1 \n");
	if(no_arg == 0){
		enter_new_process(0, NULL,stackptr, entrypoint);
	}else{
		if(!args){ // only if args is not define
			char *tem[1] = {NULL};
			args = tem;
		}
		//check length available
		int args_len = 0;
		DEBUG(DB_SYSCALL,"runprogram point 5 \n");
		for (unsigned long i = 0; i < no_arg; i++) {
			args_len += strlen(args[i]) + 1; // +1 for \0
			DEBUG(DB_SYSCALL,"check length i: %lu \n",i);
		}
		DEBUG(DB_SYSCALL,"runprogram point 6 \n");
		//get memery space for args
		size_t ptr_size = sizeof(userptr_t);
		int arg_mem = (sizeof(char**) * (no_arg+1));
		if(arg_mem % ptr_size != 0){
			arg_mem += ptr_size - (arg_mem % ptr_size);
		}	
		int args_mem = (sizeof(char) * args_len);
		if(args_mem % ptr_size != 0){
			args_mem += ptr_size - (args_mem % ptr_size);
		}	
		args_mem += arg_mem;

		 //move the stack pointer back
		 stackptr -= args_mem;

		 // copy argument values back on the user stack
		char *argv[no_arg + 1]; 
		userptr_t c_argv = (userptr_t)stackptr;
		userptr_t c_val = (userptr_t)(stackptr + arg_mem);

		size_t got;
		int offset = 0;

		DEBUG(DB_SYSCALL,"runprogram point 3 \n");
		for (unsigned long i = 0; i < no_arg; i++) {
			char * arg = args[i];
			userptr_t dest = (userptr_t)((char *)c_val + offset);
			result = copyoutstr(arg, dest, strlen(arg) + 1, &got);
			if (result) {
				return result;
			}
			argv[i] = (char *)dest;
			offset += got;
		}

		argv[no_arg] = NULL;
		DEBUG(DB_SYSCALL,"runprogram point 4 \n");
		result = copyout(argv, c_argv, (no_arg + 1) * sizeof(char *));
		if (result) {
			// Address space copy error
			return result;
		}
		DEBUG(DB_SYSCALL,"runprogram point 2 \n");
		enter_new_process(no_arg, c_argv,stackptr, entrypoint);
	}
#else
	enter_new_process(0, NULL ,stackptr, entrypoint);
#endif

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

