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
	//	pid_t pid= (pid_t) i;
//	tf->tf_a1 = i ;
//	tf->tf_a2 = curthread->pid ;
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

	tf->tf_v0 = newthread->pid;
	tf->tf_a3 = 0;

	//	result = splx(splresult) ;
	//
	//	if (result )
	//	{
	//		return result ;
	//	}


	return -(newthread->pid) ;
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
//	curthread->pid = (pid_t)tf->tf_a1 ;
	//	kprintf("\nchild_process_entry : pid %d \n",curthread->pid) ;


//	process_table[curthread->pid]->ppid = (pid_t)tf->tf_a2 ;

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


	if(process_table[pid]->exited){ //check whether it is exited.
		result = copyout(&process_table[pid]->exitcode,(userptr_t)status,sizeof(int)) ;
		if (result)
		{
			return result ;
		}
		if(pid != 0){
			process_table[pid] = NULL ;
		}
		return -pid;
	}
	lock_acquire(process_table[pid]->exit_lock) ;
	while(!process_table[pid]->exited ){
		cv_wait(process_table[pid]->exit_cv,process_table[pid]->exit_lock) ;
	}
	result = copyout(&process_table[pid]->exitcode,(userptr_t)status,sizeof(int)) ;

	if (result)
	{
		return result ;
	}

	lock_release(process_table[pid]->exit_lock) ;
	if (pid != 0){
		process_table[pid] = NULL ;
	}

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
	int argc = 0 ;

	if (args == NULL)
	{
		return EFAULT ;
	}

	while (true)
	{
		char *temp = (char *)kmalloc(25*sizeof(char)) ;
		result = copyinstr((const userptr_t)args[argc],temp,25*sizeof(char),&bytes_copied) ;
		if (result)
		{
			break ;
		}
		argc++ ;
	}


	char **temp = (char **)kmalloc(argc*sizeof(char *)) ;

	int i = 0 ;


	while(i<argc)
	{
		temp[i] = (char *)kmalloc(25*sizeof(char)) ;
		result = copyinstr((const userptr_t)args[i],temp[i],25*sizeof(char),&bytes_copied) ;
		if (result)
		{
			return result ;
		}

		i++ ;
	}

	struct vnode *v;
	vaddr_t entrypoint, stackptr;

	result = vfs_open(kernel_pgm, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}


	/* Create a new address space. */
	struct addrspace *addrspace_copy=curthread->t_addrspace;
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {
		vfs_close(v);
		curthread->t_addrspace = addrspace_copy;
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


	i = i -1 ;
	vaddr_t index[25] ;
	int k = 0 ;

	while(i>= 0)
	{
		int length = strlen(temp[i]) ;
		int num0 = (4 - (length % 4)) ;

		int j = 0 ;
		char *temp1 = (char *)kmalloc((length+num0)*sizeof(char)) ;
		strcpy(temp1,temp[i]) ;

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


	enter_new_process(argc,(userptr_t) stackptr , stackptr, entrypoint);

	panic("enter_new_process returned\n");
	return EINVAL;

}

pid_t wait_pid(pid_t pid, int *status, int options){
	options = 0 ;
	lock_acquire(process_table[pid]->exit_lock) ;
	while(!process_table[pid]->exited ){
		cv_wait(process_table[pid]->exit_cv,process_table[pid]->exit_lock) ;
	}
	*status=process_table[pid]->exitcode;
	lock_release(process_table[pid]->exit_lock) ;
	if (pid != 0){
		process_table[pid] = NULL ;
	}

	return 0;
}

void sysexit(int exit_code){
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
