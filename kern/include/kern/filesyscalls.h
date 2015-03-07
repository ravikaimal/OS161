/*
 * filesyscalls.h
 *
 *  Created on: Feb 27, 2015
 *      Author: trinity
 */

#ifndef FILEsysCALLS_H_
#define FILEsysCALLS_H_

#include <types.h>

struct filehandle{
	off_t offset ; // offset into the file
	struct vnode *vnode ; // Actual pointer to the file
	int referenceCount ; //No of processes accessing the file.
	struct lock *lock ; // Lock for the offset.
	int openflags ; //
} ;
int sys_open(userptr_t ,userptr_t ,userptr_t ) ;
int sys_read(userptr_t ,userptr_t ,userptr_t ) ;
int sys_write(userptr_t ,userptr_t ,userptr_t ) ;
off_t sys_lseek(userptr_t ,userptr_t ,userptr_t ,userptr_t ) ;
int sys_close(userptr_t userpointer) ;
int sys_dup2(userptr_t userpointer1,userptr_t userpointer2) ;
int sys__getcwd(userptr_t userpointer1,userptr_t userpointer2); //char *buf, size_t buflen
int sys_chdir(userptr_t userpointer1); //const char *pathname
#endif /* FILEsysCALLS_H_ */
