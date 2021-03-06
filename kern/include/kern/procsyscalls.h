/*
 * procsyscalls.h
 *
 *  Created on: Mar 6, 2015
 *      Author: trinity
 */

#ifndef PROCSYSCALLS_H_
#define PROCSYSCALLS_H_

#include <types.h>

pid_t sys_fork(struct trapframe *tf) ;
pid_t getpid(void) ;
void child_process_entry(void *data1, unsigned long num) ;
void sys_exit(int exit_code) ;
pid_t waitpid(pid_t pid, int *status, int options) ;
int execv(const char *program, char **args) ;
void sysexit(int exit_code) ;
pid_t wait_pid(pid_t pid, int *status, int options) ;
vaddr_t sbrk(intptr_t amount) ;

#endif /* PROCSYSCALLS_H_ */
