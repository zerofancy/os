/*
 *  linux/arch/cris/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2000  Axis Communications AB
 *
 *  Authors:  Bjorn Wesen (bjornw@axis.com)
 *
 *  $Log: init.c,v $
 *  Revision 1.15  2001/01/10 21:12:10  bjornw
 *  loops_per_sec -> loops_per_jiffy
 *
 *  Revision 1.14  2000/11/22 16:23:20  bjornw
 *  Initialize totalhigh counters to 0 to make /proc/meminfo look nice.
 *
 *  Revision 1.13  2000/11/21 16:37:51  bjornw
 *  Temporarily disable initmem freeing
 *
 *  Revision 1.12  2000/11/21 13:55:07  bjornw
 *  Use CONFIG_CRIS_LOW_MAP for the low VM map instead of explicit CPU type
 *
 *  Revision 1.11  2000/10/06 12:38:22  bjornw
 *  Cast empty_bad_page correctly (should really be of * type from the start..
 *
 *  Revision 1.10  2000/10/04 16:53:57  bjornw
 *  Fix memory-map due to LX features
 *
 *  Revision 1.9  2000/09/13 15:47:49  bjornw
 *  Wrong count in reserved-pages loop
 *
 *  Revision 1.8  2000/09/13 14:35:10  bjornw
 *  2.4.0-test8 added a new arg to free_area_init_node
 *
 *  Revision 1.7  2000/08/17 15:35:55  bjornw
 *  2.4.0-test6 removed MAP_NR and inserted virt_to_page
 *
 *
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/bootmem.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/dma.h>
#include <asm/svinto.h>

static unsigned long totalram_pages;

struct pgtable_cache_struct quicklists;  /* see asm/pgalloc.h */

const char bad_pmd_string[] = "Bad pmd in pte_alloc: %08lx\n";

extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);
extern void tlb_init(void);

/*
 * empty_bad_page is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * the main point is that when a page table error occurs, we want to get
 * out of the kernel safely before killing the process, so we need something
 * to feed the MMU with when the fault occurs even if we don't have any
 * real PTE's or page tables.
 *
 * empty_bad_page_table is the accompanying page-table: it is initialized
 * to point to empty_bad_page writable-shared entries.
 *
 * empty_zero_page is a special page that is used for zero-initialized
 * data and COW.
 */

unsigned long empty_bad_page_table;
unsigned long empty_bad_page;
unsigned long empty_zero_page;

pte_t * __bad_pagetable(void)
{
	/* somehow it is enough to just clear it and not fill it with
	 * bad page PTE's...
	 */
	memset((void *)empty_bad_page_table, 0, PAGE_SIZE);

	return (pte_t *) empty_bad_page_table;
}

pte_t __bad_page(void)
{

	/* clear the empty_bad_page page. this should perhaps be
	 * a more simple inlined loop like it is on the other 
	 * architectures.
	 */

	memset((void *)empty_bad_page, 0, PAGE_SIZE);

	return pte_mkdirty(__mk_pte((void *)empty_bad_page, PAGE_SHARED));
}

static pte_t * get_bad_pte_table(void)
{
	pte_t *empty_bad_pte_table = (pte_t *)empty_bad_page_table;
	pte_t v;
	int i;

	v = __bad_page();

	for (i = 0; i < PAGE_SIZE/sizeof(pte_t); i++)
		empty_bad_pte_table[i] = v;

	return empty_bad_pte_table;
}

void __handle_bad_pmd(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_set(pmd, get_bad_pte_table());
}

void __handle_bad_pmd_kernel(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_set_kernel(pmd, get_bad_pte_table());
}

pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long offset)
{
        pte_t *pte;

        pte = (pte_t *) __get_free_page(GFP_KERNEL);
        if (pmd_none(*pmd)) {
                if (pte) {
                        clear_page(pte);
			pmd_set_kernel(pmd, pte);
                        return pte + offset;
                }
		pmd_set_kernel(pmd, get_bad_pte_table());
                return NULL;
        }
        free_page((unsigned long)pte);
        if (pmd_bad(*pmd)) {
                __handle_bad_pmd_kernel(pmd);
                return NULL;
        }
        return (pte_t *) pmd_page(*pmd) + offset;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

        pte = (pte_t *) __get_free_page(GFP_KERNEL);
        if (pmd_none(*pmd)) {
                if (pte) {
                        clear_page(pte);
			pmd_set(pmd, pte);
                        return pte + offset;
                }
		pmd_set(pmd, get_bad_pte_table());
                return NULL;
        }
        free_page((unsigned long)pte);
        if (pmd_bad(*pmd)) {
                __handle_bad_pmd(pmd);
                return NULL;
        }
        return (pte_t *) pmd_page(*pmd) + offset;
}

#ifndef CONFIG_NO_PGT_CACHE
struct pgtable_cache_struct quicklists;

/* trim the page-table cache if necessary */

int do_check_pgt_cache(int low, int high)
{
        int freed = 0;

        if(pgtable_cache_size > high) {
                do {
                        if(pgd_quicklist) {
                                free_pgd_slow(get_pgd_fast());
                                freed++;
                        }
                        if(pmd_quicklist) {
                                free_pmd_slow(get_pmd_fast());
                                freed++;
                        }
                        if(pte_quicklist) {
                                free_pte_slow(get_pte_fast());
                                 freed++;
                        }
                } while(pgtable_cache_size > low);
        }
        return freed;
}
#else
int do_check_pgt_cache(int low, int high)
{
        return 0;
}
#endif

void show_mem(void)
{
	int i,free = 0,total = 0,cached = 0, reserved = 0, nonshared = 0;
	int shared = 0;

	printk("\nMem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!page_count(mem_map+i))
			free++;
		else if (page_count(mem_map+i) == 1)
			nonshared++;
		else
			shared += page_count(mem_map+i) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages nonshared\n",nonshared);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
	printk("%ld pages in page table cache\n",pgtable_cache_size);
	show_buffers();
}

/*
 * The kernel is already mapped with a kernel segment at kseg_c so 
 * we don't need to map it with a page table. However head.S also
 * temporarily mapped it at kseg_4 so we should set up the ksegs again,
 * clear the TLB and do some other paging setup stuff.
 */

void __init 
paging_init(void)
{
	int i;
	unsigned long zones_size[MAX_NR_ZONES];

	printk("Setting up paging and the MMU.\n");
	
	/* clear out the init_mm.pgd that will contain the kernel's mappings */

	for(i = 0; i < PTRS_PER_PGD; i++)
		swapper_pg_dir[i] = __pgd(0);

	/* initialise the TLB (tlb.c) */

	tlb_init();

	/* see README.mm for details on the KSEG setup */

#ifdef CONFIG_CRIS_LOW_MAP

	/* Etrax-100 LX version 1 has a bug so that we cannot map anything
	 * across the 0x80000000 boundary, so we need to shrink the user-virtual
	 * area to 0x50000000 instead of 0xb0000000 and map things slightly
	 * different. The unused areas are marked as paged so that we can catch
	 * freak kernel accesses there.
	 *
	 * The Juliette chip is mapped at 0xa so we pass that segment straight
	 * through. We cannot vremap it because the vmalloc area is below 0x8
	 * and Juliette needs an uncached area above 0x8.
	 */

	*R_MMU_KSEG = ( IO_STATE(R_MMU_KSEG, seg_f, page ) | 
			IO_STATE(R_MMU_KSEG, seg_e, seg  ) |  /* uncached flash */
			IO_STATE(R_MMU_KSEG, seg_d, page ) | 
			IO_STATE(R_MMU_KSEG, seg_c, page ) | 
			IO_STATE(R_MMU_KSEG, seg_b, seg  ) |  /* kernel reg area */
			IO_STATE(R_MMU_KSEG, seg_a, seg  ) |  /* Juliette etc. */
			IO_STATE(R_MMU_KSEG, seg_9, page ) |
			IO_STATE(R_MMU_KSEG, seg_8, page ) |
			IO_STATE(R_MMU_KSEG, seg_7, page ) |  /* kernel vmalloc area */
			IO_STATE(R_MMU_KSEG, seg_6, seg  ) |  /* kernel DRAM area */
			IO_STATE(R_MMU_KSEG, seg_5, seg  ) |  /* cached flash */
			IO_STATE(R_MMU_KSEG, seg_4, page ) |  /* user area */
			IO_STATE(R_MMU_KSEG, seg_3, page ) |  /* user area */
			IO_STATE(R_MMU_KSEG, seg_2, page ) |  /* user area */
			IO_STATE(R_MMU_KSEG, seg_1, page ) |  /* user area */
			IO_STATE(R_MMU_KSEG, seg_0, page ) ); /* user area */

	*R_MMU_KBASE_HI = ( IO_FIELD(R_MMU_KBASE_HI, base_f, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_e, 0x8 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_d, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_c, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_b, 0xb ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_a, 0xa ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_9, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_8, 0x0 ) );
	
	*R_MMU_KBASE_LO = ( IO_FIELD(R_MMU_KBASE_LO, base_7, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_6, 0x4 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_5, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_4, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_3, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_2, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_1, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_0, 0x0 ) );
#else
	/* This code is for the hopefully corrected Etrax-100 LX version 2... */

	*R_MMU_KSEG = ( IO_STATE(R_MMU_KSEG, seg_f, seg  ) | /* cached flash */
			IO_STATE(R_MMU_KSEG, seg_e, seg  ) | /* uncached flash */
			IO_STATE(R_MMU_KSEG, seg_d, page ) | /* vmalloc area */
			IO_STATE(R_MMU_KSEG, seg_c, seg  ) | /* kernel area */
			IO_STATE(R_MMU_KSEG, seg_b, seg  ) | /* kernel reg area */
			IO_STATE(R_MMU_KSEG, seg_a, page ) | /* user area */
			IO_STATE(R_MMU_KSEG, seg_9, page ) |
			IO_STATE(R_MMU_KSEG, seg_8, page ) |
			IO_STATE(R_MMU_KSEG, seg_7, page ) |
			IO_STATE(R_MMU_KSEG, seg_6, page ) |
			IO_STATE(R_MMU_KSEG, seg_5, page ) |
			IO_STATE(R_MMU_KSEG, seg_4, page ) |
			IO_STATE(R_MMU_KSEG, seg_3, page ) |
			IO_STATE(R_MMU_KSEG, seg_2, page ) |
			IO_STATE(R_MMU_KSEG, seg_1, page ) |
			IO_STATE(R_MMU_KSEG, seg_0, page ) );

	*R_MMU_KBASE_HI = ( IO_FIELD(R_MMU_KBASE_HI, base_f, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_e, 0x8 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_d, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_c, 0x4 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_b, 0xb ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_a, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_9, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_8, 0x0 ) );
	
	*R_MMU_KBASE_LO = ( IO_FIELD(R_MMU_KBASE_LO, base_7, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_6, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_5, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_4, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_3, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_2, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_1, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_0, 0x0 ) );
#endif	

	*R_MMU_CONTEXT = ( IO_FIELD(R_MMU_CONTEXT, page_id, 0 ) );
	
	/* The MMU has been enabled ever since head.S but just to make
	 * it totally obvious we do it here as well.
	 */

	*R_MMU_CTRL = ( IO_STATE(R_MMU_CTRL, inv_excp, enable ) |
			IO_STATE(R_MMU_CTRL, acc_excp, enable ) |
			IO_STATE(R_MMU_CTRL, we_excp,  enable ) );
	
	*R_MMU_ENABLE = IO_STATE(R_MMU_ENABLE, mmu_enable, enable);

	/*
	 * initialize the bad page table and bad page to point
	 * to a couple of allocated pages
	 */

	empty_bad_page_table = (unsigned long)alloc_bootmem_pages(PAGE_SIZE);
	empty_bad_page = (unsigned long)alloc_bootmem_pages(PAGE_SIZE);
	empty_zero_page = (unsigned long)alloc_bootmem_pages(PAGE_SIZE);
	memset((void *)empty_zero_page, 0, PAGE_SIZE);

	/* All pages are DMA'able in Etrax, so put all in the DMA'able zone */

	zones_size[0] = ((unsigned long)high_memory - PAGE_OFFSET) >> PAGE_SHIFT;

	for (i = 1; i < MAX_NR_ZONES; i++)
		zones_size[i] = 0;

	/* Use free_area_init_node instead of free_area_init, because the former
	 * is designed for systems where the DRAM starts at an address substantially
	 * higher than 0, like us (we start at PAGE_OFFSET). This saves space in the
	 * mem_map page array.
	 */

	free_area_init_node(0, 0, 0, zones_size, PAGE_OFFSET, 0);
}

extern unsigned long loops_per_jiffy; /* init/main.c */
unsigned long loops_per_usec;

extern char _stext, _edata, _etext;
extern char __init_begin, __init_end;

void __init
mem_init(void)
{
	int codesize, reservedpages, datasize, initsize;
	unsigned long tmp;

	if(!mem_map)
		BUG();

	/* max/min_low_pfn was set by setup.c
	 * now we just copy it to some other necessary places...
	 *
	 * high_memory was also set in setup.c
	 */

	max_mapnr = num_physpages = max_low_pfn - min_low_pfn;
 
	/* this will put all memory onto the freelists */
        totalram_pages = free_all_bootmem();

	reservedpages = 0;
	for (tmp = 0; tmp < max_mapnr; tmp++) {
		/*
                 * Only count reserved RAM pages
                 */
		if (PageReserved(mem_map + tmp))
			reservedpages++;
	}

	codesize =  (unsigned long) &_etext - (unsigned long) &_stext;
        datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
        initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;
	
        printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data, "
	       "%dk init)\n" ,
	       (unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
	       max_mapnr << (PAGE_SHIFT-10),
	       codesize >> 10,
	       reservedpages << (PAGE_SHIFT-10),
	       datasize >> 10,
	       initsize >> 10
               );
	
	/* HACK alert - calculate a loops_per_usec for asm/delay.h here
	 * since this is called just after calibrate_delay in init/main.c
	 * but before places which use udelay. cannot be in time.c since
	 * that is called _before_ calibrate_delay
	 */

	loops_per_usec = (loops_per_jiffy * HZ) / 1000000;

	return;
}

/* free the pages occupied by initialization code */

void free_initmem(void)
{
#if 0
	/* currently this is a bad idea since the cramfs image is catted onto
	 * the vmlinux image, and the end of that image is not page-padded so
	 * part of the cramfs image will be freed here
	 */
        unsigned long addr;

        addr = (unsigned long)(&__init_begin);
        for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
                ClearPageReserved(virt_to_page(addr));
                set_page_count(virt_to_page(addr), 1);
                free_page(addr);
                totalram_pages++;
        }
        printk ("Freeing unused kernel memory: %dk freed\n", 
		(&__init_end - &__init_begin) >> 10);
#endif
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = max_mapnr;
	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages();
	val->bufferram = atomic_read(&buffermem_pages);
	while (i-- > 0)  {
		if (PageReserved(mem_map+i))
			continue;
		val->totalram++;
		if (!atomic_read(&mem_map[i].count))
			continue;
		val->sharedram += atomic_read(&mem_map[i].count) - 1;
	}
	val->mem_unit = PAGE_SIZE;
	val->totalhigh = 0;
	val->freehigh = 0;
}
