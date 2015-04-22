/*
 * vm.c
 *
 *  Created on: Apr 8, 2015
 *      Author: trinity
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <synch.h>
#include <current.h>
#include <mips/tlb.h>

short vm_initialized  = 0;
uint64_t localtime = 1 ;
struct lock *coremaplock ;

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

void vm_bootstrap()
{
	paddr_t firstaddr, lastaddr,freeaddr;
	ram_getsize(&firstaddr, &lastaddr);
	int total_page_num = (lastaddr - firstaddr)  / PAGE_SIZE;
	coremap_list = (struct coremap*)PADDR_TO_KVADDR(firstaddr);
	struct coremap *head = coremap_list ;

	vm_initialized = 0 ;

	int page_num = 0 ;
	for (page_num = 1 ; page_num < total_page_num  ; page_num++ )
	{
		freeaddr = firstaddr + page_num * sizeof(struct coremap);
		coremap_list->next = (struct coremap*)PADDR_TO_KVADDR(freeaddr);
		coremap_list->fixed = VM_NOT_FIXED ;
		coremap_list->page_free = PAGE_FREE ;
		coremap_list->clean = PAGE_CLEAN ;
		coremap_list->timestamp = 0 ;
		coremap_list = coremap_list->next ;
	}
//	for (page_num = (total_page_num /2)+1 ; page_num < total_page_num ; page_num++ )
//	{
//		freeaddr = firstaddr + page_num * sizeof(struct coremap);
//		coremap_list->next = (struct coremap*)PADDR_TO_KVADDR(freeaddr);
//		coremap_list->fixed = VM_NOT_FIXED ;
//		coremap_list->page_free = PAGE_FREE ;
//		coremap_list->clean = PAGE_CLEAN ;
//		coremap_list->timestamp = 0 ;
//		coremap_list = coremap_list->next ;
//	}
	coremap_list->next = NULL;
	coremap_list->fixed = VM_NOT_FIXED ;
	coremap_list->page_free = PAGE_FREE ;
	coremap_list->clean = PAGE_CLEAN ;
	coremap_list->timestamp = 0 ;
	coremap_list=head;

	freeaddr = firstaddr + page_num * sizeof(struct coremap);
	paddr_t page_start = freeaddr & 0xff000 ;
	page_start = page_start + 0x1000 ;


	while(coremap_list->next != NULL)
	{
		coremap_list->pa = page_start ;
		coremap_list->va = PADDR_TO_KVADDR(page_start);
		page_start = page_start + 0x1000 ;
		coremap_list = coremap_list->next ;
	}

	coremap_list=head;
	vm_initialized = 1 ;

	coremaplock = lock_create("coremaplock") ;
}

paddr_t page_alloc()
{
	struct coremap *local_coremap = coremap_list ;

	lock_acquire(coremaplock) ;

	while(local_coremap->next != NULL)
	{
		if(local_coremap->fixed && local_coremap->page_free )
		{
//			local_coremap->as = curthread->t_addrspace ;
			local_coremap->page_free = PAGE_NOT_FREE ;
			local_coremap->timestamp = localtime ;
			localtime++ ;
			local_coremap->clean = PAGE_DIRTY ;
			local_coremap->pages=1;
			bzero((void *)PADDR_TO_KVADDR(local_coremap->pa),PAGE_SIZE);
			lock_release(coremaplock) ;
			return local_coremap->pa ;
		}

		local_coremap = local_coremap->next ;
	}

	local_coremap = coremap_list ;
	uint64_t mintime = local_coremap->timestamp ;
	struct coremap *local_coremap_min = coremap_list ;
	while(local_coremap->next != NULL)
	{
		if(local_coremap->fixed && local_coremap->timestamp <= mintime  )
		{
			mintime = local_coremap->timestamp ;
			local_coremap_min = local_coremap ;
		}

		local_coremap = local_coremap->next ;
	}

//	local_coremap_min->as = curthread->t_addrspace ;
	local_coremap_min->page_free = PAGE_NOT_FREE ;
	local_coremap_min->timestamp = localtime ;
	localtime++ ;
	local_coremap_min->clean = PAGE_DIRTY ;
	local_coremap->pages=1;

	lock_release(coremaplock) ;
	//Implement Swapping
	return local_coremap_min->pa ;

}

static paddr_t getppages(unsigned long npages)
{
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);

	spinlock_release(&stealmem_lock);
	return addr;
}

paddr_t alloc_npages(int npages){
	struct coremap *local_coremap = coremap_list ;
	struct coremap *start = coremap_list;
	int count=0;

	lock_acquire(coremaplock) ;
	while(local_coremap->next != NULL && count!=npages){
		if(!local_coremap->fixed && local_coremap->page_free ){
			if(count == 0)
				start = local_coremap;
			count++;		//increment the number of free colntinuous pages
		}
		else
		{
			count=0;		//reset the counter
		}
		local_coremap=local_coremap->next;
	}
	if(count == npages){			//found npages continuous pages
		//change attributes of the pages
		//bzero all the pages
		local_coremap=start;
		count=0;
		while(count!=npages){
//			local_coremap->as = curthread->t_addrspace ;
			local_coremap->page_free = PAGE_NOT_FREE ;
			local_coremap->timestamp = localtime ;
			localtime++ ;
			local_coremap->clean = PAGE_DIRTY ;
			local_coremap->pages=0;
			bzero((void *)PADDR_TO_KVADDR(local_coremap->pa),PAGE_SIZE);
			local_coremap=local_coremap->next;
			count++;
		}
		start->pages=count;
		lock_release(coremaplock) ;
		return start->pa;
	}
	return 0;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t alloc_kpages(int npages)
{
	paddr_t pa;
	if(vm_initialized){
		pa=alloc_npages(npages);
		if(pa==0)
			return 0;
	}
	else{		
		pa = getppages(npages);
		if (pa==0) {
			return 0;
		}

	}
	return PADDR_TO_KVADDR(pa);
}



void free_kpages(vaddr_t addr)					//Clear tlb entries remaining.
{
	struct coremap *local_coremap=coremap_list;
	while(local_coremap->next!=NULL){
		if(local_coremap->va == addr){
			break;
		}
		local_coremap=local_coremap->next;
	}
	int count=local_coremap->pages;
	while(count!=0){					//What other fields to reset - timestamp?
		 local_coremap->page_free = PAGE_FREE ;
		 local_coremap->clean = PAGE_CLEAN ;
		 local_coremap->pages=0;
		 local_coremap=local_coremap->next;
		 count--;
	}
}

void vm_tlbshootdown_all(void)
{
		int i, spl;

		/* Disable interrupts on this CPU while frobbing the TLB. */
		spl = splhigh();

		for (i=0; i<NUM_TLB; i++) {
			tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
		}

		splx(spl);
}

	void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

	int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	(void)faulttype ;
	(void)faultaddress ;
	return 0 ;
}

