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
#include <synch.h>
#include <current.h>
#include <spl.h>
#include <mips/tlb.h>
#include <vfs.h>
#include <uio.h>
#include <kern/stat.h>
#include <vnode.h>
#include <vm.h>

short vm_initialized  = 0;
uint64_t localtime = 1 ;
off_t global_offset = 0 ;

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

void vm_bootstrap()
{
	paddr_t firstaddr, lastaddr,freeaddr;
	ram_getsize(&firstaddr, &lastaddr);
	int total_page_num = ((lastaddr - firstaddr)  / PAGE_SIZE) ;
	if(total_page_num == 193)
		total_page_num-=1;
	else if(total_page_num == 449)
		total_page_num-=2;
	else if(total_page_num == 961)
	        total_page_num-=5;
	coremap_list = (struct coremap*)PADDR_TO_KVADDR(firstaddr);
	struct coremap *head = coremap_list ;

	vm_initialized = 0 ;

	int page_num = 0 ;
	for (page_num = 1 ; page_num < total_page_num  ; page_num++ )
	{
		freeaddr = firstaddr + page_num * sizeof(struct coremap);
		coremap_list->next = (struct coremap*)PADDR_TO_KVADDR(freeaddr);
		coremap_list->status = 0x6 ;
		coremap_list->timestamp = 0 ;
		coremap_list->as = NULL ;
		coremap_list->swap = 0;
		coremap_list->pages = 0;
		coremap_list = coremap_list->next ;
	}
	coremap_list->next = NULL;
	coremap_list->status = 0x10 ;
	coremap_list->timestamp = 0 ;
	coremap_list=head;


	freeaddr = firstaddr + page_num * sizeof(struct coremap);
	paddr_t page_start = freeaddr & 0xfffff000 ;
	page_start = page_start + 0x1000 ;


	while(coremap_list->next != NULL)
	{
		coremap_list->pa = page_start ;
		coremap_list->va = PADDR_TO_KVADDR(page_start);
		page_start = page_start + 0x1000 ;
		coremap_list = coremap_list->next ;
	}
	coremap_list->next=NULL;

	coremap_list=head;
	vm_initialized = 1 ;
}

paddr_t page_alloc()
{
	struct coremap *local_coremap = coremap_list ;

	spinlock_acquire(&stealmem_lock);

	while(local_coremap->next != NULL)
	{
		if((local_coremap->status & 0x2) == 2 )
		{
			local_coremap->timestamp = localtime ;
			localtime++ ;
			local_coremap->pages=1;
			local_coremap->status = 1 ;
			local_coremap->as = NULL ;
			bzero((void *)PADDR_TO_KVADDR(local_coremap->pa),PAGE_SIZE);
			spinlock_release(&stealmem_lock);
			return local_coremap->pa ;
		}

		local_coremap = local_coremap->next ;
	}

	spinlock_release(&stealmem_lock);

	paddr_t pa = 0 ;
	pa = swap_out(1) ;
	return pa ;

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
	spinlock_acquire(&stealmem_lock);
	while(local_coremap->next != NULL && count!=npages){
		if((local_coremap->status & 0x3) == 2 ){
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
			local_coremap->timestamp = localtime ;
			localtime++ ;
			local_coremap->pages=0;
			local_coremap->status = 1 ;
			local_coremap->as = NULL;
			bzero((void *)PADDR_TO_KVADDR(local_coremap->pa),PAGE_SIZE);
			local_coremap=local_coremap->next;
			count++;
		}
		start->pages=count;
		spinlock_release(&stealmem_lock);
		return start->pa;
	}
	else
	{
		spinlock_release(&stealmem_lock);
		paddr_t pa = swap_kpages(npages) ;
		return pa ;
	}
	spinlock_release(&stealmem_lock);
	panic("alloc_npages could not find consecutive pages\n");
	return 0;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t alloc_kpages(int npages)
{
	paddr_t pa;
	if(vm_initialized){
		if(npages == 1){
			pa=page_alloc();
		}
		else{
			pa=alloc_npages(npages);
		}

		if(pa==0){
			panic("alloc_npages could not find an empty page\n");
			return 0;
		}
	}
	else{
		pa = getppages(npages);
		if (pa==0) {
			panic("getppages could not find an empty page\n");
			return 0;
		}

	}
	return PADDR_TO_KVADDR(pa);
}

void free_kpages(vaddr_t addr)					//Clear tlb entries remaining.
{
	struct coremap *local_coremap=coremap_list;
	spinlock_acquire(&stealmem_lock);
	while(local_coremap->next!=NULL){
		if(local_coremap->va == addr){
			break;
		}
		local_coremap=local_coremap->next;
	}
	int count=local_coremap->pages;
	while(count!=0){					//What other fields to reset - timestamp?
		local_coremap->pages=0;
		local_coremap->status = 6 ;
		local_coremap=local_coremap->next;
		count--;
	}
	spinlock_release(&stealmem_lock);
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

void vm_tlbshootdown(const struct tlbshootdown *ts)
{
    	int i, spl;

    /* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	uint32_t ehi, elo;
	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (ehi == ts->ts_vaddr)
		{
			tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
			break ;
		}
	}
	splx(spl);
}
int vm_fault(int faulttype, vaddr_t faultaddress)
{
	if(faultaddress == 0)
		return EFAULT; //panic("vm_fault : Got null fault address\n");
	// Check whether the faultaddress is valid
	struct addrspace *curaddrspace = curthread->t_addrspace ;

	int i = 0 ;
	for (i = 0 ; i< N_REGIONS ; i++)
	{
		if(curaddrspace->regions[i] != NULL )
		{
			vaddr_t region_end = curaddrspace->regions[i]->region_start+(4096*curaddrspace->regions[i]->npages);
			if (faultaddress >= curaddrspace->regions[i]->region_start && faultaddress <= region_end)
			{
				break ;
			}
		}
	}

	if(i == N_REGIONS)
	{
		return EFAULT; //EINVAL ;
	}

	//Check whether it is a write to a read only region
	if((faulttype == VM_FAULT_WRITE) && (curaddrspace->regions[i]->permissions == 0x4 || curaddrspace->regions[i]->permissions == 0x5))
	{
		return EFAULT ;		//EINVAL
	}

	faultaddress &= PAGE_FRAME;
	int spl = splhigh();
	uint32_t ehi, elo;

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress ;
		elo = page_fault(faultaddress) | TLBLO_DIRTY | TLBLO_VALID;
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}
	ehi = faultaddress ;
	elo = page_fault(faultaddress) | TLBLO_DIRTY | TLBLO_VALID;

	tlb_random(ehi, elo) ;

	splx(spl);
	return 0;
}

paddr_t page_fault(vaddr_t faultaddress)
{
	struct page_table_entry *pt_entry = curthread->t_addrspace->page_table ;
	struct page_table_entry *pt_entry_temp = NULL;

	while(pt_entry != NULL){
		if(pt_entry->va == faultaddress) // Additional checks to be implemented later
		{
			if(pt_entry->state == PAGE_IN_MEMORY){
					return pt_entry->pa ;
			}
			else{
				paddr_t p = swap_in(pt_entry->offset);
				pt_entry->pa = p;
				pt_entry->state = PAGE_IN_MEMORY ;
				return p;
			}			
		}
		pt_entry_temp = pt_entry ;
		pt_entry = pt_entry->next ;
	}

	paddr_t paddr = user_page_alloc() ;
	struct page_table_entry *pt_entry_temp2 = (struct page_table_entry*)kmalloc(sizeof(struct page_table_entry)) ;
	pt_entry_temp2->va =  faultaddress;
	pt_entry_temp2->pa =  paddr ;
	pt_entry_temp2->state = PAGE_IN_MEMORY ; 
	pt_entry_temp2->offset = -1;
	pt_entry_temp2->next = NULL ;


	if (pt_entry_temp != NULL)
	{
		pt_entry_temp->next = pt_entry_temp2 ;
	}
	else
	{
		curthread->t_addrspace->page_table = pt_entry_temp2 ;
	}
	return paddr ;
}


paddr_t user_page_alloc(){
	struct coremap *local_coremap = coremap_list ;
	if(spinlock_do_i_hold(&stealmem_lock)){
                panic("user_page_alloc : Lock already held");
	}
	spinlock_acquire(&stealmem_lock);
	while(local_coremap!=NULL){
		if((local_coremap->status & 0x2) == 2  ){
			local_coremap->timestamp = localtime;
			localtime++;
			local_coremap->status = 0 ;
			local_coremap->pages=1;
			local_coremap->as = curthread->t_addrspace ;
			bzero((void *)PADDR_TO_KVADDR(local_coremap->pa),PAGE_SIZE);
			spinlock_release(&stealmem_lock);
			return local_coremap->pa;
		}
		local_coremap = local_coremap->next ;
	}
	spinlock_release(&stealmem_lock);
	paddr_t pa = swap_out(0) ;
	return pa ;
}

void user_page_free(paddr_t pa)                                  //Free user page
{
	struct coremap *local_coremap=coremap_list;
	spinlock_acquire(&stealmem_lock);

	while(local_coremap!=NULL){
		if(local_coremap->pa == pa){
			local_coremap->status = 6;
			spinlock_release(&stealmem_lock);
			break;	
		}
		local_coremap=local_coremap->next;
	}
	if(local_coremap == NULL){
		spinlock_release(&stealmem_lock);
		panic("user_page_free : Could not free page\n");
	}
}


void
qzero(void *vblock, size_t len,int number)
{
	char *block = vblock;
	size_t i;

	if ((uintptr_t)block % sizeof(long) == 0 &&
			len % sizeof(long) == 0) {
		long *lb = (long *)block;
		for (i=0; i<len/sizeof(long); i++) {
			lb[i] = number;
		}
	}
	else {
		for (i=0; i<len; i++) {
			block[i] = 0;
		}
	}
}

char *read_zero(void *vblock, size_t len)
{

	char *block = vblock;
	char *return_data;
	size_t i;

	if ((uintptr_t)block % sizeof(long) == 0 &&
			len % sizeof(long) == 0) {
		long *lb = (long *)block;
		return_data = (char *)kmalloc(len/sizeof(long));
		for (i=0; i<len/sizeof(long); i++) {
			return_data[i] = lb[i];
		}
	}
	else {
		return_data = (char *)kmalloc(sizeof(char) * len);
		for (i=0; i<len; i++) {
			return_data[i] = block[i];
		}

	}
	return_data[i] = '\0' ;

	return return_data ;
}

void update_pagetable_entry(struct coremap *swap_coremap,off_t offset){
	struct addrspace *as = swap_coremap->as;
	struct page_table_entry *pt = as->page_table;
	paddr_t pa = swap_coremap->pa;
	while(pt!=NULL){
		if(pt->pa == pa){
			pt->state = PAGE_ON_DISK;
			pt->offset = offset;
			break;
		}
		pt=pt->next;
	}
}

paddr_t swap_in(off_t offset){                  //Check for empty page
	paddr_t pa;
	pa = user_page_alloc();
	int result = read_from_disk(PADDR_TO_KVADDR(pa),offset) ;

	if (result)
	{
		panic("\n swap failed \n") ;
	}
	return pa;
}


paddr_t swap_out(int flag){
	struct coremap *local_coremap = coremap_list;
	struct coremap *swap_coremap = NULL ;//coremap_list;
	uint16_t min = 65535;
	struct tlbshootdown *tlbsd = (struct tlbshootdown*)kmalloc(sizeof(struct tlbshootdown)) ;
	spinlock_acquire(&stealmem_lock);
	while(local_coremap!=NULL){
		if(local_coremap->status == 0 && local_coremap->timestamp <= min && local_coremap->as != curthread->t_addrspace && local_coremap->swap == 0){
			min = local_coremap->timestamp;
			swap_coremap = local_coremap;
		}
		local_coremap = local_coremap->next;
	}
	if(swap_coremap == NULL){				//Could not find page from another process. Now will evacuate page of current process.
		local_coremap = coremap_list;
		while(local_coremap!=NULL){
	                if(local_coremap->status == 0 && local_coremap->timestamp <= min && local_coremap->swap == 0){//has to be user page, min time and doesnt belong to current process
				min = local_coremap->timestamp;
				swap_coremap = local_coremap;
			}
			local_coremap = local_coremap->next;
		}
		if(swap_coremap == NULL){
			spinlock_release(&stealmem_lock);
			panic("Could not find page in other processes\n");
		}
	}	
	//if((swap_coremap->status & 0x4) == 0){
		struct addrspace *temp = swap_coremap->as;
		struct page_table_entry *pt = temp->page_table;
		while(pt!=NULL){
			if(pt->pa == swap_coremap->pa)
				break;
			pt=pt->next;
		}
		off_t off = 0;
		if(pt == NULL){
			swap_coremap->swap = 1;
			spinlock_release(&stealmem_lock);
			off = write_to_swap(PADDR_TO_KVADDR(swap_coremap->pa),-1) ;	//panic("swap_out : test panic \n");//off = write_to_swap(PADDR_TO_KVADDR(swap_coremap->pa),-1) ;
		}
		else{
			swap_coremap->swap = 1;
			spinlock_release(&stealmem_lock);
			off = write_to_swap(PADDR_TO_KVADDR(swap_coremap->pa),pt->offset) ;
		}
		if(off == -10)
			panic("swap_out : incorrect write to disk");
		spinlock_acquire(&stealmem_lock);
		swap_coremap->swap = 0;
		if(swap_coremap->as == curthread->t_addrspace){
			tlbsd->ts_vaddr = swap_coremap->va ;
			vm_tlbshootdown(tlbsd) ;
		}
		update_pagetable_entry(swap_coremap,off);
	//}
	bzero((void *)PADDR_TO_KVADDR(swap_coremap->pa),PAGE_SIZE);
	swap_coremap->timestamp = localtime;
	localtime++;
	swap_coremap->pages=1;
	if(flag){                               //We are swapping to allocate a kernel page.
		swap_coremap->status = 1;
		swap_coremap->as=NULL;
	}
	else{                                   //We are swapping to allocate user page
		swap_coremap->status = 0 ;
		swap_coremap->as=curthread->t_addrspace;
	}
	spinlock_release(&stealmem_lock);
	kfree(tlbsd);
	return swap_coremap->pa;
}

paddr_t swap_kpages(int npages){
	paddr_t pa = 0;
	int count = 0;
	struct coremap *local_coremap = coremap_list;
	struct coremap *start = coremap_list;
	//First find sequence of user allocated pages. Kernel pages cannot be swapped out.
	spinlock_acquire(&stealmem_lock);
	while(local_coremap != NULL && count!=npages){
		if((local_coremap->status & 0x3) == 0){                 //Last two bits - free and fixed should be 0.
			if(count == 0)
				start = local_coremap;
			count++;
		}
		else{
			count = 0;
		}
		local_coremap = local_coremap->next;
	}
	if(count == npages){
		local_coremap = start;
		count = 0;
		while(count!=npages){
			local_coremap->timestamp = localtime;
			localtime++;
			local_coremap->pages = 0;
			local_coremap->status = 1;
			if((local_coremap->status & 0x4) == 0){
				struct addrspace *temp = local_coremap->as;
				struct page_table_entry *pt = temp->page_table;
				while(pt!=NULL){
					if(pt->pa == local_coremap->pa)
						break;
					pt=pt->next;
			}
			off_t off = write_to_swap(PADDR_TO_KVADDR(local_coremap->pa),pt->offset) ;
			if(off == -10)
				panic("swap Kpages : \n");
			update_pagetable_entry(local_coremap,off);
			}
			bzero((void *)PADDR_TO_KVADDR(local_coremap->pa),PAGE_SIZE);
			local_coremap->as = NULL;
			local_coremap = local_coremap->next;
			count++;
		}
		start->pages = count;
		spinlock_release(&stealmem_lock);
		return start->pa;
	}
	spinlock_release(&stealmem_lock);
	panic("swap_kpages : Could not swap out %d pages\n",npages);
	return pa;
}


off_t write_to_swap(vaddr_t page,off_t offset)
{
	struct uio uio ;
	struct iovec iovec ;
	if(offset == -1)
		uio_kinit(&iovec,&uio,(void * )page,PAGE_SIZE,global_offset,UIO_WRITE);
	else
		uio_kinit(&iovec,&uio,(void * )page,PAGE_SIZE,offset,UIO_WRITE);

	int temp = uio.uio_offset;
	int result=VOP_WRITE(swap_file,&uio);

	if(result)
	{
		return -10;
	}
	if(offset == -1){
		global_offset = global_offset + PAGE_SIZE ;
	        return temp; 
	}
	else{
		return offset;
	}
}

int read_from_disk(vaddr_t page,off_t offset)
{
	struct uio uio ;
	struct iovec iovec;

	uio_kinit(&iovec,&uio,(void *)page,PAGE_SIZE,offset,UIO_READ);

	int result = VOP_READ(swap_file,&uio);
	if(result)
	{
		return result;
	}

	return 0 ;

}
