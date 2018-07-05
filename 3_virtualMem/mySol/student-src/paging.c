#include "paging.h"
#include "page_splitting.h"
#include "swapops.h"
#include "stats.h"

 /* The frame table pointer. You will set this up in system_init. */
fte_t *frame_table;



/*  --------------------------------- PROBLEM 2 --------------------------------------
    In this problem, you will initialize the frame table.

    The frame table will be located at physical address 0 in our simulated
    memory. You will first assign the frame_table global variable to point to
    this location in memory. You should zero out the frame table, in case for
    any reason physical memory is not clean.

    You should then mark the first entry in the frame table as protected. We do
    this because we do not want our free frame allocator to give out the frame
    used by the frame table.

    HINTS:
        You will need to use the following global variables:
        - mem: Simulated physical memory already allocated for you.
        - PAGE_SIZE: The size of one page.
        You will need to initialize (set) the following global variable:
        - frame_table: a pointer to the first entry of the frame table

    -----------------------------------------------------------------------------------
*/
void system_init(void) {
    frame_table = (fte_t*) mem;
    memset(frame_table, 0, PAGE_SIZE);
    frame_table->protected = 1;
	frame_table->mapped = 1;
}

  /*--------------------------------- PROBLEM 3 --------------------------------------
    This function gets called every time a new process is created.
    You will need to allocate a new page table for the process in memory using the
    free_frame function so that the process can store its page mappings. Then, you
    will need to store the PFN of this page table in the process's PCB.

    HINTS:
        - Look at the pcb_t struct defined in pagesim.h to know what to set inside.
        - You are not guaranteed that the memory returned by the free frame allocator
        is empty - an existing frame could have been evicted for our new page table.
        - As in the previous problem, think about whether we need to mark any entries
        in the frame_table as protected after we allocate memory for our page table.
    -----------------------------------------------------------------------------------
*/
void proc_init(pcb_t *proc) {
    pfn_t frameNum = free_frame();
    memset(mem + (frameNum * PAGE_SIZE), 0, PAGE_SIZE);
    proc->saved_ptbr = frameNum;
    fte_t* ft_e = frame_table + frameNum;
    ft_e->protected = 1;
    ft_e->process = proc;


}

/*  --------------------------------- PROBLEM 4 --------------------------------------
    Swaps the currently running process on the CPU to another process.

    Every process has its own page table, as you allocated in proc_init. You will
    need to tell the processor to use the new process's page table.

    HINTS:
        - Look at the global variables defined in pagesim.h. You may be interested in
        the definition of pcb_t as well.
    -----------------------------------------------------------------------------------
 */
void context_switch(pcb_t *proc) {
	current_process = proc;
    PTBR = proc->saved_ptbr;
}

/*  --------------------------------- PROBLEM 5 --------------------------------------
    Takes an input virtual address and returns the data from the corresponding
    physical memory address. The steps to do this are:

    1) Translate the virtual address into a physical address using the page table.
    2) Go into the memory and read/write the data at the translated address.

    Parameters:
        1) address     - The virtual address to be translated.
        2) rw          - 'r' if the access is a read, 'w' if a write
        3) data        - One byte of data to write to our memory, if the access is a write.
                         This byte will be NULL on read accesses.

    Return:
        The data at the address if the access is a read, or
        the data we just wrote if the access is a write.

    HINTS:
        - You will need to use the macros we defined in Problem 1 in this function.
        - You will need to access the global PTBR value. This will tell you where to
        find the page table. Be very careful when you think about what this register holds!
        - On a page fault, simply call the page_fault function defined in page_fault.c.
        You may assume that the pagefault handler allocated a page for your address
        in the page table after it returns.
        - Make sure to set the referenced bit in the frame table entry since we accessed the page.
        - Make sure to set the dirty bit in the page table entry if it's a write.
        - Make sure to update the stats variables correctly (see stats.h)
    -----------------------------------------------------------------------------------
 */
uint8_t mem_access(vaddr_t address, char rw, uint8_t data) {
    vpn_t vpn = vaddr_vpn(address);
    uint16_t offset = vaddr_offset(address);
    pte_t* pt_e = (pte_t *)(mem + (PTBR * PAGE_SIZE)) + vpn;
    if (pt_e->valid == 0)
		page_fault(address);    
    pfn_t pfn = pt_e->pfn;
    fte_t* ft_e = frame_table + pfn;
    ft_e->referenced  = 1;
    uint8_t* pAddr = (uint8_t*)(mem + (paddr_t)((pfn << OFFSET_LEN) | offset));
    if (rw == 'r') {
        data = *pAddr;
        stats.reads++;
    } else {
        *pAddr = data;
        pt_e->dirty = 1;
        stats.writes++;
    }
    stats.accesses++;
    return data;
}

/*  --------------------------------- PROBLEM 8 --------------------------------------
    When a process exits, you need to free any pages previously occupied by the
    process. Otherwise, every time you closed and re-opened Microsoft Word, it
    would gradually eat more and more of your computer's usable memory.

    To free a process, you must clear the "mapped" bit on every page the process
    has mapped. If the process has swapped any pages to disk, you must call
    swap_free() using the page table entry pointer as a parameter.

    You must also clear the "protected" bits for the page table itself.
    -----------------------------------------------------------------------------------
*/
void proc_cleanup(pcb_t *proc) {
    pfn_t page_table = proc->saved_ptbr;
    for (size_t i = 0; i < NUM_PAGES; i++) {
        pte_t* pt_e = (pte_t*)(mem + (page_table * PAGE_SIZE)) + i;
        if (pt_e->valid == 1) {
            fte_t* ft_e = frame_table + pt_e->pfn;
            ft_e->referenced = 0;
            ft_e->mapped = 0;
            pt_e->valid = 0;
        }
        if (pt_e->swap)
			swap_free(pt_e);
    }
    fte_t* fte = frame_table + page_table;
    fte->protected = 0;
}
