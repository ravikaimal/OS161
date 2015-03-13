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
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <vnode.h>
#include <syscall.h>
#include <synch.h>
#include <test.h>
#include <kern/filesyscalls.h>
#include <copyinout.h>

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname,char **args)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new thread. */
	KASSERT(curthread->t_addrspace == NULL);

	/* Create a new address space. */
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_addrspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_addrspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		return result;
	}

	char *path = kstrdup("con:");
	curthread->fd[0] = (struct filehandle *)kmalloc(sizeof(struct filehandle *));
	result = vfs_open(path, O_RDONLY, 0664, &(curthread->fd[0]->vnode)) ;
	if (result != 0)
	{
		return EFAULT ;
	}
	curthread->fd[0]->openflags =  O_RDONLY ;
	curthread->fd[0]->referenceCount = 1 ;
	curthread->fd[0]->reflock = lock_create("reflock") ;

	char *path1 = kstrdup("con:");

	curthread->fd[1] = (struct filehandle *)kmalloc(sizeof(struct filehandle *));
	result = vfs_open(path1, O_WRONLY, 0664, &(curthread->fd[1]->vnode)) ;
	if (result != 0)
	{
		return EFAULT ;
	}
	curthread->fd[1]->openflags =  O_WRONLY ;
	curthread->fd[1]->referenceCount = 1 ;
	curthread->fd[1]->reflock = lock_create("reflock") ;

	char *path2 = kstrdup("con:");

	curthread->fd[2] = (struct filehandle *)kmalloc(sizeof(struct filehandle *));
	result = vfs_open(path2, O_WRONLY, 0664, &(curthread->fd[2]->vnode)) ;
	curthread->fd[2]->referenceCount = 1 ;
	if (result != 0)
	{
		return EFAULT ;
	}
	curthread->fd[2]->openflags =  O_WRONLY ;
	curthread->fd[2]->reflock = lock_create("reflock") ;

//	kprintf("\n runprogram : calculating number of arguments \n") ;

	int argc = 0 ;
	if (args != NULL)
	{
		while (args[argc] != NULL)
		{
//			kprintf("\n runprogram : argument %s \n",args[argc]) ;
			argc++ ;
		}
	}

//	kprintf("\n runprogram : calculated %d \n",argc) ;

	vaddr_t index[25] ;
	int k = 0 ;

	int i = argc-1 ;
	size_t bytes_copied ;

	while(i>= 0)
	{
		int length = strlen(args[i]) ;
		int num0 = (4 - (length % 4)) ;

		int j = 0 ;
		char *temp1 = (char *)kmalloc((length+num0)*sizeof(char)) ;
		strcpy(temp1,args[i]) ;

		while(j<num0)
		{
			strcat(temp1,"\0") ;
			j++ ;
		}

		stackptr = stackptr - ((length+num0)*sizeof(char)) ;


		result = copyoutstr(temp1,(userptr_t) stackptr,(length+num0)*sizeof(char),&bytes_copied) ;
		if (result)
		{
			return result ;
		}

		index[k] = (vaddr_t )stackptr;
		k++ ;

		i-- ;
	}

	i = 0 ;

	stackptr = stackptr - sizeof(int) ;
	stackptr = stackptr - sizeof(int) ;
	k-- ;
	while(i<=k)
	{

		result = copyout(&index[i],(userptr_t) stackptr,sizeof(int)) ;
		if (result)
		{
			return result ;
		}

		i++ ;
		if (i<=k){
		stackptr = stackptr - sizeof(int) ;
		}

	}


	/* Warp to user mode. */
	enter_new_process(argc,(userptr_t)stackptr /*userspace addr of argv*/,
			stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}


