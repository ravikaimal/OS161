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

	int length = __PATH_MAX ;
	if (file_name == NULL )
	{
		return EFAULT ;
	}
	char * k_file_name =(char*)kmalloc(sizeof(char *));

	size_t buflen ;
	int open_flags = (int)flags ;

	if ( (open_flags > 127) || (open_flags & O_ACCMODE) > 3)
	{
		return EINVAL ;
	}
	int result = copyinstr(file_name, (void *)k_file_name, length,&buflen) ;
	if (result != 0)
	{
		return result ;
	}

	mode = 0 ;

	int i = 3 ;

	for (i = 3; i<__OPEN_MAX ; i++)
	{
		if (curthread->fd[i] == NULL)
		{
			break ;
		}
	}
	if (i == __OPEN_MAX)
	{
		return EMFILE ;
	}

	curthread->fd[i] = (struct filehandle*)kmalloc(sizeof(struct filehandle*)) ;

	result = vfs_open(k_file_name, open_flags, 0, &(curthread->fd[i]->vnode)) ;


	if(result != 0)
	{
		return result ;
	}
	struct stat buffr ;
	int stat_result = VOP_STAT(curthread->fd[i]->vnode, &buffr);
	if (stat_result != 0)
	{
		return stat_result ;
	}
	curthread->fd[i]->referenceCount = 1 ;
	curthread->fd[i]->lock = lock_create("filelock") ;
	curthread->fd[i]->reflock = lock_create("reflock") ;


	curthread->fd[i]->openflags = open_flags & O_ACCMODE ;

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

	kfree(k_file_name) ;
	return -i ;
}


int sys_read(userptr_t arg1,
		userptr_t arg2,userptr_t arg3)
{
	int userfd = (int)arg1 ;
	if (userfd > __OPEN_MAX || userfd < 0)
	{
		return EBADF ;
	}
	size_t buflen = (size_t)arg3;
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

	uio_uinit(&iovec,&uio,arg2,buflen,curthread->fd[userfd]->offset,UIO_READ);

	int result = VOP_READ(curthread->fd[userfd]->vnode,&uio);
	if(result)
	{
		return result;
	}
	if(userfd != 0)
	{
		lock_release(curthread->fd[userfd]->lock);
	}
	curthread->fd[userfd]->offset=uio.uio_offset;

	return -(buflen-uio.uio_resid) ;
}


int sys_write(userptr_t arg1,userptr_t arg2,userptr_t arg3)   //(int userfd, const void *buf, size_t nbytes){
{
	int userfd = (int)arg1;
	if (userfd > __OPEN_MAX || userfd < 0)
	{
		return EBADF ;
	}
	size_t nbytes = (size_t)arg3;

	if(curthread->fd[userfd] == NULL){
		return EBADF;
	}

	if(curthread->fd[userfd]->openflags == O_RDONLY){
		return EBADF;
	}
	if ( userfd != 1 && userfd != 2 )
	{
		lock_acquire(curthread->fd[userfd]->lock);
	}
	struct uio uio ;
	struct iovec iovec ;

	uio_uinit(&iovec,&uio,arg2,nbytes,curthread->fd[userfd]->offset,UIO_WRITE);

	int result=VOP_WRITE(curthread->fd[userfd]->vnode,&uio);

	if(result)
	{
		return result;
	}
	curthread->fd[userfd]->offset=uio.uio_offset;
	if ( userfd != 1 && userfd != 2 )
	{
		lock_release(curthread->fd[userfd]->lock);
	}

	return -(nbytes-uio.uio_resid);
}



off_t sys_lseek(userptr_t arg1,userptr_t arg2,userptr_t arg3,userptr_t arg4)
{

	int userfd = (int)arg1 ;
	if (userfd > __OPEN_MAX || userfd < 0 )
	{
		return EBADF ;
	}
	int pos1 = (int)arg2 ;
	int pos2 = (int)arg3 ;
	int *whence_address = (int*)arg4 ;
	int whence=*whence_address;
//	kprintf("\nsys_lseek : whence %d\n",whence);
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

		offset=pos+buffr.st_size ;


		break;
	}

	int result=VOP_TRYSEEK(curthread->fd[userfd]->vnode,offset);

	if(result)
	{
		return result;
	}

	curthread->fd[userfd]->offset=offset;
	lock_release(curthread->fd[userfd]->lock);
	return -offset;
}
//How to handle seek past end end of file

int sys_dup2(userptr_t userpointer1,userptr_t userpointer2){
	int oldfd = (int)userpointer1;
	int newfd = (int)userpointer2;

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


	if (userfd > __OPEN_MAX || userfd < 0)
	{
		return EBADF ;
	}

	if(curthread->fd[userfd] == NULL){
		return EBADF;
	}

	lock_acquire(curthread->fd[userfd]->reflock) ;

//	kprintf("\nsys_close ---: userfd %d    %d\n ",userfd,curthread->fd[userfd]->referenceCount) ;
	if(curthread->fd[userfd]->referenceCount == 1)
	{
		if (curthread->fd[userfd]->vnode != NULL && curthread->fd[userfd]->vnode->vn_opencount > 0)
		{
//			kprintf("\nsys_close 1: open count %d\n ",curthread->fd[userfd]->vnode->vn_opencount) ;
			struct vnode *vnode1 = curthread->fd[userfd]->vnode ;
//			kprintf("\nsys_close 2: open count %d\n ",vnode1->vn_opencount) ;
			vfs_close(vnode1) ;
		}
		lock_release(curthread->fd[userfd]->reflock) ;
		lock_destroy(curthread->fd[userfd]->reflock) ;
		kfree(curthread->fd[userfd]) ;
		curthread->fd[userfd] = NULL ;
	}
	else
	{
		curthread->fd[userfd]->referenceCount-- ;
		lock_release(curthread->fd[userfd]->reflock) ;
	}


	return 0 ;
}

int sys__getcwd(userptr_t userpointer1,userptr_t userpointer2){	//char *buf, size_t buflen
	struct uio uio;
	struct iovec iovec;
	size_t buflen = (size_t)userpointer2;
	uio_uinit(&iovec,&uio,userpointer1,buflen,0,UIO_READ);

	int result=vfs_getcwd(&uio);
	if(result)
		return result;


	return buflen-uio.uio_resid;

}

int sys_chdir(userptr_t userpointer1){ //int sys_chdir(const char *pathname){
	char *kernel_buffer=(char *)kmalloc(sizeof(char *));
	int result = copyin((userptr_t)userpointer1,kernel_buffer,sizeof(userpointer1));
	if(result)
			return result;
	result=vfs_chdir(kernel_buffer);
	kfree(kernel_buffer);
	if(result)
		return result;
	return 0;
}

