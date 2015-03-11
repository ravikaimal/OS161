#include <types.h>
#include <kern/errno.h>
#include <kern/limits.h>
#include <lib.h>
#include <copyinout.h>
#include <current.h>
#include <thread.h>
#include <mips/trapframe.h>
#include <spl.h>
#include <addrspace.h>
#include <vm.h>
#include <synch.h>
#include <kern/procsyscalls.h>
#include <kern/filesyscalls.h>
#include <kern/wait.h>
#include <kern/limits.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <syscall.h>

pid_t sys_fork(struct trapframe *tf)
{
	int i=2;
	for(i=2;i<__PID_MAX_LOCAL;i++)
	{
		if(process_table[i] == NULL)
		{
			break;
		}
	}
	if(i == __PID_MAX_LOCAL)
	{
		return EMPROC;
	}
	process_table[i] = (struct process *)kmalloc(sizeof(struct process)) ;
	process_table[i]->exit_lock = lock_create("exit-lock") ;
	process_table[i]->exit_cv = cv_create("exit-cv") ;
	//	pid_t pid= (pid_t) i;
	tf->tf_a1 = i ;
	tf->tf_a2 = curthread->pid ;
	//	kprintf("sys_fork :pid %d ",(int)pid) ;
	//    tf->tf_a0 = (uint32_t)curthread->t_addrspace ;
	struct trapframe *newtrapframe =kmalloc(sizeof(struct trapframe ));
	memcpy(newtrapframe,tf ,sizeof(struct trapframe ));
	int result ;

	struct addrspace *newaddrspace;
	result = as_copy(curthread->t_addrspace,&newaddrspace);

	if(result)
	{
		return result;
	}

	struct thread *newthread ;

	//	int splresult = splhigh() ;
	//
	//	if (splresult)
	//	{
	//		return splresult ;
	//	}

	//	kprintf("\n pid : %d\n",(int)tf->tf_a1);

	result = thread_fork("childthread", child_process_entry, newtrapframe,(unsigned long)newaddrspace,&newthread);

	//	kprintf("\nsys_fork : Result of thread_copy : %d\n",result);
	if (result)
	{
		return result ;
	}

	tf->tf_v0 = i;
	tf->tf_a3 = 0;

	//	result = splx(splresult) ;
	//
	//	if (result )
	//	{
	//		return result ;
	//	}

	process_table[i]->currentthread = newthread ;
	process_table[i]->ppid = curthread->pid ;

	return -i ;
}

void child_process_entry(void *data1, unsigned long data2)
{
	//	kprintf("\nChild has started running\n") ;
	struct trapframe *tf = (struct trapframe *)data1 ;


	//	kprintf("\n child_process_entry pid : %d\n",(int)curthread->pid);

	//	data2 = 0 ;

	//	int result = as_copy((struct addrspace *)data2,&(curthread->t_addrspace));

	curthread->t_addrspace = (struct addrspace *)data2 ;


	//	kprintf("\nchild_process_entry as copy result %d \n",result) ;
	as_activate(curthread->t_addrspace) ;
	curthread->pid = (pid_t)tf->tf_a1 ;
	//	kprintf("\nchild_process_entry : pid %d \n",curthread->pid) ;


	process_table[curthread->pid]->ppid = (pid_t)tf->tf_a2 ;

	//	kprintf("\nchild_process_entry : ppid %d \n",process_table[curthread->pid]->ppid ) ;

	struct trapframe usertf ;

	memcpy(&usertf,tf ,sizeof(struct trapframe ));
	usertf.tf_v0 = 0 ;
	usertf.tf_a3 = 0 ;
	usertf.tf_epc += 4 ;
	mips_usermode(&usertf) ;

}

pid_t getpid(void)
{
	//	kprintf(" getpid : %d",(int)curthread->pid);
	return -curthread->pid ;
}

void sys_exit(int exit_code){
	pid_t pid = curthread->pid ;
	lock_acquire(process_table[pid]->exit_lock) ;
	process_table[pid]->exited = true ;
	process_table[pid]->exitcode=_MKWAIT_EXIT(exit_code);
	//close file descriptors
	int i=0;
	for(i=0;i<__OPEN_MAX;i++){
		if(curthread->fd[i] != NULL)
		{
			sys_close((userptr_t)i);
		}

	}

	cv_broadcast(process_table[pid]->exit_cv,process_table[pid]->exit_lock) ;
	lock_release(process_table[pid]->exit_lock) ;

	thread_exit();
}

pid_t waitpid(pid_t pid, int *status, int options){
//	kprintf("\nwaitpid : %d\n",(int)pid);

	if (pid <=0 || pid >= __PID_MAX_LOCAL)
	{
		return ESRCH ;
	}
	if(options != 0)
	{
		return EINVAL;
	}
	int *kernel_status ;
	int result = copyin((userptr_t)status,kernel_status,sizeof(int *));
	if (result)
	{
		return result ;
	}
//	kprintf("\npassed copyin\n");
	if (pid == curthread->pid)
	{
		return ECHILD ;
	}
	if(process_table[pid] == NULL)
	{
		return ESRCH;
	}
	if (process_table[pid]->ppid != curthread->pid)	//curthread->pid
	{
		return ECHILD ;
	}
	pid_t ppid = process_table[curthread->pid]->ppid ;

	if (ppid == pid)
	{
		return ECHILD ;
	}

//	if (ppid == process_table[pid]->ppid)
//	{
//		return ECHILD ;
//	}


	if(process_table[pid]->exited){ //check whether it is exited.
//		*status=process_table[pid]->exitcode;
		result = copyout(&process_table[pid]->exitcode,(userptr_t)status,sizeof(int)) ;
		if (result)
		{
			return result ;
		}
		process_table[pid] = NULL ;
		return -pid;
	}
	lock_acquire(process_table[pid]->exit_lock) ;
	while(!process_table[pid]->exited ){
		cv_wait(process_table[pid]->exit_cv,process_table[pid]->exit_lock) ;
	}
//	*status=process_table[pid]->exitcode;
	result = copyout(&process_table[pid]->exitcode,(userptr_t)status,sizeof(int)) ;

	if (result)
	{
		return result ;
	}

	//	kprintf("\nstatus 2 %d \n",*status);
	lock_release(process_table[pid]->exit_lock) ;
	process_table[pid] = NULL ;

	return -pid;
}

int execv(const char *program, char **args)
{
	if (program == NULL )
	{
		return EFAULT ;
	}

	char * kernel_pgm = (char *)kmalloc(sizeof(char *)) ;
	size_t bytes_copied  ;

	int result = copyinstr((const userptr_t)program,kernel_pgm,__PATH_MAX,&bytes_copied) ;

	if (result )
	{
		return result ;
	}

	if (strlen(kernel_pgm) == 0)
	{
		return EINVAL ;
	}

	if (strlen(kernel_pgm) >= __PATH_MAX)
	{
		return ENAMETOOLONG ;
	}

	char **kargs ;

	int argc = 0 ;

	while(args[argc] != NULL )
	{
		result = copyinstr((const userptr_t)args[argc],kargs[argc],strlen(args[argc]),&bytes_copied) ;

		if (result)
		{
			return result ;
		}
		if (strlen(kargs[argc]) %4 != 0)
		{
			unsigned int i = 0;
			for (i = 0 ; i < strlen(kargs[argc]) %4 ; i++)
			{
				kargs[argc] = strcat(kargs[argc],"\0" ) ;
			}
		}
		argc++;
	}

	struct vnode *v;
	vaddr_t entrypoint, stackptr;

	result = vfs_open(kernel_pgm, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	KASSERT(curthread->t_addrspace == NULL);

	/* Create a new address space. */
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	as_activate(curthread->t_addrspace);

	result = load_elf(v, &entrypoint);
	if (result) {
		vfs_close(v);
		return result;
	}

	vfs_close(v);

	result = as_define_stack(curthread->t_addrspace, &stackptr);
	if (result) {
		return result;
	}

	result = copyout(kargs,(userptr_t)stackptr+16,sizeof(kargs)) ;
	if (result) {
		return result;
	}

	stackptr = stackptr - sizeof(kargs) ;

	enter_new_process(argc,(userptr_t) stackptr , stackptr, entrypoint);

	panic("enter_new_process returned\n");
	return EINVAL;

}


