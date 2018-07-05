#include "paging.h"
#include "swapops.h"
#include "stats.h"

/*  --------------------------------- PROBLEM 6 --------------------------------------
    Page fault handler.

    When the CPU encounters an invalid address mapping in a page table,
    it invokes the OS via this handler.

    Your job is to put a mapping in place so that the translation can
    succeed. You can use free_frame() to make an available frame.
    Update the page table with the new frame, and don't forget
    to fill in the frame table.

    Lastly, you must fill your newly-mapped page with data. If the page
    has never mapped before, just zero the memory out. Otherwise, the
    data will have been swapped to the disk when the page was
    evicted. Call swap_read() to pull the data back in.

    HINTS:
         - You will need to use the global variable current_process when
           setting the frame table entry.

    ----------------------------------------------------------------------------------
 */
void page_fault(vaddr_t address) {
    vpn_t vpn = vaddr_vpn(address);
    pte_t* pt_e = (pte_t *)(mem + (PTBR * PAGE_SIZE)) + vpn;
    pfn_t frameNum = free_frame();
    pt_e->valid = 1;
    pt_e->pfn = frameNum;
    fte_t* ft_e = frame_table + frameNum;
    ft_e->mapped = 1;
    ft_e->referenced = 1;
    ft_e->process = current_process;
    ft_e->vpn = vpn;
    if (!(pt_e->swap)) {
        for (int i = frameNum * PAGE_SIZE; i < (frameNum + 1) * PAGE_SIZE ; i++){
			mem[i] = 0;
		}
    }else{
        swap_read(pt_e, mem + frameNum * PAGE_SIZE);
    }
    stats.page_faults++;
}
