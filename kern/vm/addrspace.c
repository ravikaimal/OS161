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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
	int i = 0;

	for (i = 0 ;i<N_REGIONS ; i++)
	{
		as->regions[i] = NULL ;
	}

	as->page_table = NULL	;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	int i =0 ;

	for (i = 0 ; i<N_REGIONS ;i++)
	{
		if (old->regions[i] != NULL)
		{
			newas->regions[i] = (struct region*)kmalloc(sizeof(struct region*)) ;
			newas->regions[i]->npages = old->regions[i]->npages ;
			newas->regions[i]->permissions = old->regions[i]->permissions ;
			newas->regions[i]->region_start = old->regions[i]->region_start ;
//			newas->regions[i]->region_end = old->regions[i]->region_end ;
			newas->heap_end  = old->heap_end ;
		}
		else
		{
			newas->regions[i] = NULL ;
		}
	}

	struct page_table_entry *page_table_temp = old->page_table ;
	struct page_table_entry *page_table_temp2 = NULL ;
	struct page_table_entry *page_table_temp3 = NULL ;
//	struct page_table_entry *page_table_start = NULL ;
	while(page_table_temp != NULL ){
		page_table_temp3 = page_table_temp2 ;
//		if (page_table_temp2 != NULL)
//		{
//			kfree(page_table_temp2) ;
//		}
		page_table_temp2 = (struct page_table_entry*)kmalloc(sizeof(struct page_table_entry*)) ;
		page_table_temp2->pa = user_page_alloc() ;
//		page_table_temp2->va = PADDR_TO_KVADDR(page_table_temp2->pa) ;
		memmove((void *)PADDR_TO_KVADDR(page_table_temp2->pa),(const void *)PADDR_TO_KVADDR(page_table_temp->pa),PAGE_SIZE);
		page_table_temp2->va = page_table_temp->va ;
		page_table_temp2->state = page_table_temp->state ;


		if(page_table_temp3 != NULL)
		{
			page_table_temp3->next = page_table_temp2 ;
		}
		else
		{
			newas->page_table = page_table_temp2 ;
		}

		page_table_temp = page_table_temp->next ;

	}
//	kfree(page_table_temp2) ;
//	newas->page_table = page_table_start

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
	
	int i = 0 ;
	for(i = 0 ; i < N_REGIONS ; i++)
	{
		kfree(as->regions[i]) ;
	}

	struct page_table_entry * temp1 = as->page_table ;
	struct page_table_entry * temp2 = as->page_table ;
//	struct page_table_entry * temp2 = NULL ;

	while(temp2 != NULL &&  temp1->next != (void*)0xdeadbeef)
	{
		temp2 = temp1->next ;
		user_page_free(temp1->pa) ;
		kfree(temp1) ;
		temp1 = temp2 ;
	}

//	while(temp1->next != NULL )
//	{
//		temp2 = temp1->next ;
//		user_page_free(temp1->pa) ;
//		kfree(temp1) ;
//		temp1 = temp2 ;
//	}

	kfree(as);
}

void
as_activate(struct addrspace *as)
{
	vm_tlbshootdown_all() ;
	(void)as ;
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	int i=0;
	for(i=0;i<N_REGIONS;i++){
		if(as->regions[i]==NULL)
			break;
	}
	if(i==N_REGIONS)
		return EUNIMP;
	 
	size_t npages;
	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;
	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;
	npages = sz / PAGE_SIZE;

	as->regions[i]=(struct region *)kmalloc(sizeof(struct region *));
	as->regions[i]->region_start=vaddr;
	as->regions[i]->npages=npages;
//	as->regions[i]->region_end= as->regions[i]->region_start+(PAGE_SIZE*as->regions[i]->npages) -1 ;
	as->regions[i]->permissions= readable | writeable | executable ;

	return 0;
}

int as_define_heap(struct addrspace *as){
	vaddr_t max_address=0;
	int i=0;
	for(i=0;i<N_REGIONS;i++){
		if(as->regions[i] == NULL)
			continue;
		if((as->regions[i]->region_start+(4096*as->regions[i]->npages)>max_address)){
			max_address = as->regions[i]->region_start+(4096*as->regions[i]->npages);
		}
	}
	for(i=0;i<N_REGIONS;i++){
		if(as->regions[i] == NULL)
			break;
	}
	if(i==N_REGIONS)
		return EUNIMP;
	as->regions[i]=(struct region *)kmalloc(sizeof(struct region *));
	as->regions[i]->region_start= (max_address & 0xfffff000 ) + 0x1000 ;
	as->regions[i]->permissions= 70 ;//Binary converted value
	as->regions[i]->npages = 1 ;
//	as->regions[i]->region_end= as->regions[i]->region_start+(PAGE_SIZE*as->regions[i]->npages) - 1;
	as->heap_end = as->regions[i]->region_start ; //+(PAGE_SIZE*as->regions[i]->npages) - 1; ;
	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 Set code region as writeable so the code can be written to it.
	 Allocate pages now as per second approach-what to allocate? and allocate to where?
	 */
	int i ;
	for(i=0;i<N_REGIONS;i++){
		if(as->regions[i]!=NULL && (as->regions[i]->permissions == 0x4 || as->regions[i]->permissions == 0x5))
		{
			as->regions[i]->permissions = 0x2 ;
		}
	}


	return 0;
}



int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 Set code region as read only so no one can modify the code.
	 */
	int i ;
	for(i=0;i<N_REGIONS;i++){
		if(as->regions[i]!=NULL && (as->regions[i]->permissions == 0x2 ))
		{
			as->regions[i]->permissions = 0x5 ;
		}
	}
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */
	int i ;
	for(i=0;i<N_REGIONS;i++){
		if(as->regions[i] == NULL)
		{
			break ;
		}
	}
	as->regions[i]=(struct region *)kmalloc(sizeof(struct region *));
	as->regions[i]->region_start= USERSTACK - 4096*4 ;
	as->regions[i]->permissions=6;
	as->regions[i]->npages = 4 ;
//	as->regions[i]->region_end  = USERSTACK ;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}


