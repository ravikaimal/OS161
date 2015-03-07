/*
 * filesyscalls.c

 *
 *  Created on: Feb 27, 2015
 *      Author: trinity
 */
#include <kern/filesyscalls.h>
#include <types.h>
#include <kern/errno.h>
#include <kern/limits.h>
#include <copyinout.h>
#include <kern/seek.h>
#include <lib.h>
#include <kern/fcntl.h>
#include <current.h>
#include <vfs.h>
#include <synch.h>
#include <kern/stat.h>
#include <uio.h>
#include <vnode.h>



int sys_open(userptr_t file_name,userptr_t flags,userptr_t mode)
{
//	kprintf("\n sys_open : File Name %s \n",(char *)file_name) ;

	int length = __PATH_MAX ;
	if (file_name == NULL )
	{
		//length = strlen((char *)file_name) ;
		return EFAULT ;
	}
	char * k_file_name =(char*)kmalloc(sizeof(char *));  // = kstrdup((char *)file_name) ;//= (char*)kmalloc(sizeof(char *));

//	length = strlen((char *)k_file_name) ;
//
//	if(length == 0)
//	{
//		return EINVAL ;
//	}

//	kprintf("\n length : %d \n",length) ;
	size_t buflen ;
	//	kprintf("sys_open:copyin filename: %s\n",(char *)file_name) ;
	int open_flags = (int)flags ;
	kprintf("\n Open Flags %d %d \n",open_flags,(open_flags & O_ACCMODE)) ;

	if ( (open_flags > 127) || (open_flags & O_ACCMODE) > 3)
	{
		return EINVAL ;
	}
	int result = copyinstr(file_name, (void *)k_file_name, length,&buflen) ;
		kprintf("sys_open:copyin filename: %d\n",result) ;
	if (result != 0)
	{
		return result ;
	}
	kprintf("\n File Name %s \n",k_file_name) ;

	mode = 0 ;

	int i = 3 ;

	for (i = 3; i<__OPEN_MAX ; i++)
	{
		if (curthread->fd[i] == NULL)
		{
			break ;
		}
	}
	//Return if max number of file handles have reached.
	if (i == __OPEN_MAX)
	{
		return EMFILE ;
	}
	//	kprintf("i: %d\n",i) ;

	curthread->fd[i] = (struct filehandle*)kmalloc(sizeof(struct filehandle*)) ;
	//struct vnode *vnode = kmalloc(sizeof(struct vnode *)) ;

	result = vfs_open(k_file_name, open_flags, 0, &(curthread->fd[i]->vnode)) ;

	//	kprintf("sys_open:vfs open: %d\n",result) ;

	if(result != 0)
	{
		return result ;
	}
	////////
	struct stat buffr ;
	int stat_result = VOP_STAT(curthread->fd[i]->vnode, &buffr);
	if (stat_result != 0)
	{
		return stat_result ;
	}
	//		curthread->fd[i]->offset =  buf->st_size ;
	//	=pos+buffr.st_size ;
	//	kprintf("\n sys_open : file stat 1 %d \n",(int)buffr.st_size) ;
	//	kprintf("\n sys_lseek : file stat  2 %d \n",(int)pos) ;
	//	kprintf("\n sys_lseek : file stat  3 %d \n",(int)offset) ;
	////////////////
	curthread->fd[i]->referenceCount++ ;
	curthread->fd[i]->lock = lock_create("filelock") ;


	curthread->fd[i]->openflags = open_flags & O_ACCMODE ;
	//	kprintf("sys_open:setting open flags %d for i %d  name : %s \n",curthread->fd[i]->openflags,i,curthread->t_name ) ;

	if ((open_flags ^ 1) == O_APPEND)
	{
		struct stat *buf;
		int stat_result = VOP_STAT(curthread->fd[i]->vnode, buf);

		if (stat_result != 0)
		{
			return stat_result ;
		}
		curthread->fd[i]->offset =  buf->st_size ;
	}
	//	kprintf("sys_open:FInal REturn: %d\n",result) ;
	//	kprintf("\n****Struct filehandle data****");
	//	kprintf("\n1. Offset %d",(int)curthread->fd[i]->offset);
	//	kprintf("\n2. Reference Count %d",curthread->fd[i]->referenceCount);
	//	kprintf("\n3. Openflags %d",curthread->fd[i]->openflags);
	//	kprintf("\n************************OPEN DONE*********************************\n") ;

	return -i ;
}


int sys_read(userptr_t arg1,
		userptr_t arg2,userptr_t arg3)
{
	int userfd = (int)arg1 ;
	if (userfd > __OPEN_MAX)
	{
		return EBADF ;
	}
	//	kprintf("\nsys_read : userfd : %d\n",userfd);
	size_t buflen = (size_t)arg3;
	//	int result = copyin((const_userptr_t)arg3, &buflen, sizeof(size_t)) ;
	//	kprintf("sys_read : uio_uinit reading %d \n",(int)buflen) ;
	if(curthread->fd[userfd] == NULL){
		return EBADF;
	}
	if(curthread->fd[userfd]->openflags == O_WRONLY){
		return EBADF;
	}
	if (userfd != 0 )
	{
		lock_acquire(curthread->fd[userfd]->lock);
	}
	struct uio uio ;
	struct iovec iovec;

	//	kprintf("sys_read : uio_uinit reading %u \n",(unsigned)buflen) ;
	//iovec.iov_ubase=(userptr_t)vaddr;-----------------------------?? from loadelf.c
	//	char *kernel_buffer=(char *)kmalloc(sizeof(char *));
	//	kprintf("sys_read : offset before uio_unit %d \n",(int)curthread->fd[userfd]->offset); ;
	uio_uinit(&iovec,&uio,arg2,buflen,curthread->fd[userfd]->offset,UIO_READ);
	//uio.uio_iovcnt=1;	// set in kinit
	//uio.uio_resid=buflen;	// set in kinit
	//uio.uio_segflag	set in kinit
	//uio.uio_space=curthread->t_addrspace; set in kinit

	int result = VOP_READ(curthread->fd[userfd]->vnode,&uio);
	//	kprintf("sys_read : VOP_READ %d \n",result) ;
	if(result)
	{
		return result;	//What return values? ------------??
	}
	curthread->fd[userfd]->offset=uio.uio_offset;
	//	kprintf("sys_read : curthread->fd[userfd]->offset %d \n",(int)curthread->fd[userfd]->offset) ;
	if(userfd != 0)
	{
		lock_release(curthread->fd[userfd]->lock);
	}
	//	result = copyout(kernel_buffer,(userptr_t)arg2,buflen);		//handles EFAULT
	//	if (result)
	//	{
	//		return result ;
	//	}
	//	kfree(kernel_buffer);

	//	kprintf("sys_read : buflen-uio.uio_resid %d \n",buflen-uio.uio_resid) ;
	return -(buflen-uio.uio_resid) ;
}


int sys_write(userptr_t arg1,userptr_t arg2,userptr_t arg3)   //(int userfd, const void *buf, size_t nbytes){
{
	int userfd = (int)arg1;
	if (userfd > __OPEN_MAX)
	{
		return EBADF ;
	}
	if (userfd == 1 )
	{
		//		kprintf("\n sys_write: userfd %d\n",userfd) ;
	}
	//	char *kernel_buffer=(char *)kmalloc(sizeof(char *));
	//	size_t bytes_copied=0;
	//	int result = copyin((userptr_t)arg2,(void *)kernel_buffer,(size_t)arg3);

	//	if(userfd == 1){
	//		kprintf("\nsys_write : Contents of buffer are : %s",kernel_buffer);
	//		kprintf("\nsys_write : Copyin result : %d",result);
	//	}
	//	if(result)
	//	{
	//		return result;
	//	}
	size_t nbytes = (size_t)arg3;
	if (userfd == 1  )
	{
		//				kprintf("\n sys_write: nbytes %d \n",nbytes) ;
	}

	if(curthread->fd[userfd] == NULL){
		return EBADF;
	}
	if (userfd == 1 ){
		//				kprintf("\n sys_write: After checking fd == nULL \n") ;
	}

	if(curthread->fd[userfd]->openflags == O_RDONLY){
		return EBADF;
	}
	if (userfd == 1 )
	{
		//				kprintf("\n sys_write: After checking flags  == RDONLY") ;
	}
	if ( userfd != 1 && userfd != 2 )
	{
		lock_acquire(curthread->fd[userfd]->lock);
		//		kprintf("\n Lock Acquired") ;
	}
	struct uio uio ;
	struct iovec iovec ;

	uio_uinit(&iovec,&uio,arg2,nbytes,curthread->fd[userfd]->offset,UIO_WRITE);

	int result=VOP_WRITE(curthread->fd[userfd]->vnode,&uio);
	//	kprintf("\n sys_write: VOP_WRITE  result %d",result) ;
	if (userfd == 1 )
	{
		//				kprintf("\n sys_write: VOP_WRITE  result %d",result) ;
	}

	if(result)
	{
		return result;
	}
	curthread->fd[userfd]->offset=uio.uio_offset;
	if (userfd != 1 && userfd != 2 )
	{
		//		kprintf("\n sys_write: offset %d",(int)uio.uio_offset) ;
	}
	if ( userfd != 1 && userfd != 2 )
	{
		lock_release(curthread->fd[userfd]->lock);
	}
	return -(nbytes-uio.uio_resid);
}



off_t sys_lseek(userptr_t arg1,userptr_t arg2,userptr_t arg3,userptr_t arg4)
{

	int userfd = (int)arg1 ;
	if (userfd > __OPEN_MAX)
	{
		return EBADF ;
	}
	int pos1 = (int)arg2 ;
	int pos2 = (int)arg3 ;
	int *whence_address = (int*)arg4 ;
	int whence=*whence_address;
	kprintf("\nsys_lseek : whence %d\n",whence);
	off_t pos = 0 | pos1 ;
	pos = pos << 32 | pos2 ;

	if(curthread->fd[userfd] == NULL){
		return EBADF;
	}
	if (userfd != 1 && userfd != 2 )
	{
		//		kprintf("\n sys_lseek: After curthread->fd[userfd] == NULL ") ;
	}
	if(whence!=SEEK_SET && whence!=SEEK_CUR && whence!=SEEK_END){
		return EINVAL;
	}
	if (userfd != 1 && userfd != 2 )
	{
		//		kprintf("\n sys_lseek : After whence!=SEEK_SET || whence!=SEEK_CUR || whence!=SEEK_END") ;
	}
	//Check if seeking is not supported??
	int offset=0;
	lock_acquire(curthread->fd[userfd]->lock);
	struct stat buffr ;
	int stat_result ;

	if (userfd != 1 && userfd != 2 )
	{
		//		kprintf("\n sys_lseek: SEEK POSITION %d ",whence) ;
	}


	switch(whence)
	{
	case SEEK_SET:
		if (pos<0)
		{
			return EINVAL ;
		}
		offset=pos;
		break;
	case SEEK_CUR:
		if (pos+curthread->fd[userfd]->offset < 0)
		{
			return EINVAL ;
		}
		offset=pos+curthread->fd[userfd]->offset;
		break;
	case SEEK_END:
		//		if (pos+curthread->fd[userfd]->offset < 0)
		//		{
		//			return EINVAL ;
		//		}
		if (userfd != 1 && userfd != 2 )
		{
			//			kprintf("\n sys_lseek : file stat\n") ;
		}
		stat_result = VOP_STAT(curthread->fd[userfd]->vnode, &buffr);
		if (stat_result != 0)
		{
			return stat_result ;
		}
		//		curthread->fd[i]->offset =  buf->st_size ;
		offset=pos+buffr.st_size ;
		//		kprintf("\n sys_lseek : file size %d \n",(int)buffr.st_size) ;
		//		kprintf("\n sys_lseek : file pos %d \n",(int)pos) ;
		//		kprintf("\n sys_lseek : file offset %d \n",(int)offset) ;
		if (userfd != 1 && userfd != 2 )
		{
			//			kprintf("\n sys_lseek : offset  >> %d",offset) ;
		}
		break;
	}
	if (userfd != 1 && userfd != 2 )
	{
		//		kprintf("\n sys_lseek: OFFSET Before VOP_TRYSEEK %d",offset) ;
	}
	int result=VOP_TRYSEEK(curthread->fd[userfd]->vnode,offset);
	if (userfd == 3)
	{
		//		kprintf("\n sys_lseek:  VOP_TRYSEEK return valye %d ",result) ;
		//		kprintf("\n sys_lseek:  VOP_TRYSEEK new  offset %lu ",(long)offset) ;
	}
	if(result)
	{

		return result;
	}
	if (userfd != 1 && userfd != 2 )
	{
		//		kprintf("\n sys_lseek: Before VOP_TRYSEEK") ;
	}
	curthread->fd[userfd]->offset=offset;
	lock_release(curthread->fd[userfd]->lock);
	return -offset;
}
//How to handle seek past end end of file

int sys_dup2(userptr_t userpointer1,userptr_t userpointer2){
	int oldfd = (int)userpointer1;
	int newfd = (int)userpointer2;
	//	kprintf("\nsys_dup2 : oldfd %d",oldfd);
	//	kprintf("\nsys_dup2 : newfd %d",newfd);
	if (oldfd >= __OPEN_MAX || newfd >= __OPEN_MAX || newfd < 0 || oldfd < 0)
	{
		return EBADF ;
	}
	if(curthread->fd[oldfd] == NULL){
		return EBADF;
	}
	if(newfd>__OPEN_MAX){
		return EBADF;
	}
	if(curthread->fd[newfd]!=NULL)	//already open
	{
		///************************		sys_close(newfd);
	}
	else
	{
		curthread->fd[newfd]=(struct filehandle *)kmalloc(sizeof(struct filehandle *));
	}
	curthread->fd[newfd]=curthread->fd[oldfd];
	curthread->fd[oldfd]->referenceCount++ ;
	return newfd;
}


//is lock needed?
//How to check if process limit was reached??

int sys_close(userptr_t userpointer)
{

	int userfd = (int)userpointer;

	//	kprintf("\n Closing %d \n",userfd) ;

	if (userfd > __OPEN_MAX)
	{
		return EBADF ;
	}

	if(curthread->fd[userfd] == NULL){
		return EBADF;
	}

	//	kprintf(" sys_close : curthread->fd[userfd]->referenceCount >> %d \n",curthread->fd[userfd]->referenceCount) ;
	if(curthread->fd[userfd]->referenceCount == 1)
	{
		//		kprintf(" sys_close : closing the vnode >> \n") ;
		vfs_close(curthread->fd[userfd]->vnode) ;

		curthread->fd[userfd] = NULL ;
	}
	else
	{
		curthread->fd[userfd]->referenceCount-- ;
	}

	return 0 ;
}

int sys__getcwd(userptr_t userpointer1,userptr_t userpointer2){	//char *buf, size_t buflen
	struct uio uio;
	struct iovec iovec;
	size_t buflen = (size_t)userpointer2;
//	char *kernel_buffer=(char *)kmalloc(sizeof(char *));
	uio_uinit(&iovec,&uio,userpointer1,buflen,0,UIO_READ);

	int result=vfs_getcwd(&uio);
	if(result)
		return result;

//	copyout(kernel_buffer,userpointer1,buflen);
//	kfree(kernel_buffer);

	return buflen-uio.uio_resid;

}

int sys_chdir(userptr_t userpointer1){ //int sys_chdir(const char *pathname){
	char *kernel_buffer=(char *)kmalloc(sizeof(char *));
	int result = copyin((userptr_t)userpointer1,kernel_buffer,sizeof(userpointer1));
	if(result)
			return result;
	result=vfs_chdir(kernel_buffer);
//	int result=vfs_chdir(userpointer1);
	kfree(kernel_buffer);
	if(result)
		return result;
	return 0;
}

