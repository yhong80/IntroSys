#include "types.h"
#include "pagesim.h"
#include "paging.h"
#include "swapops.h"
#include "stats.h"
#include "util.h"

pfn_t select_victim_frame(void);


/*  --------------------------------- PROBLEM 7 --------------------------------------
    Make a free frame for the system to use.

    You will first call the page replacement algorithm to identify an
    "available" frame in the system.

    In some cases, the replacement algorithm will return a frame that
    is in use by another page mapping. In these cases, you must "evict"
    the frame by using the frame table to find the original mapping and
    setting it to invalid. If the frame is dirty, write its data to swap!
 * ----------------------------------------------------------------------------------
 */
pfn_t free_frame(void) {
    pfn_t victim_pfn;
    victim_pfn = select_victim_frame();
	fte_t* ft_e = frame_table + victim_pfn;
     if (ft_e->mapped==1) {
        pcb_t* proc = ft_e->process;
        vpn_t vpn = ft_e->vpn;
        ft_e->mapped = 0;
        pte_t* pt_e = (pte_t*)(mem + (proc->saved_ptbr * PAGE_SIZE)) + vpn;
        pt_e->valid = 0;
        if (pt_e->dirty==1) {
            swap_write(pt_e, mem + (victim_pfn * PAGE_SIZE));
            pt_e->dirty = 0;
			stats.writebacks++;
        }
    }
    return victim_pfn;
}



/*  --------------------------------- PROBLEM 9 --------------------------------------
    Finds a free physical frame. If none are available, uses either a
    randomized or clock sweep algorithm to find a used frame for
    eviction.

    When implementing clock sweep, make sure you set the reference
    bits to 0 for each frame that had its referenced bit set. Your
    clock sweep should remember the index at which it leaves off and
    resume at the same place for each run (you may need to add a
    global or static variable for this).

    Return:
        The physical frame number of a free (or evictable) frame.

    HINTS: Use the global variables MEM_SIZE and PAGE_SIZE to calculate
    the number of entries in the frame table.
    ----------------------------------------------------------------------------------
*/
pfn_t select_victim_frame() {
    /* See if there are any free frames first */
    size_t num_entries = MEM_SIZE / PAGE_SIZE;
    for (size_t i = 0; i < num_entries; i++) {
        if (!frame_table[i].protected && !frame_table[i].mapped) {
            return i;
        }
    }

    if (replacement == RANDOM) {
        /* Play Russian Roulette to decide which frame to evict */
        pfn_t last_unprotected = NUM_FRAMES;
        for (pfn_t i = 0; i < num_entries; i++) {
            if (!frame_table[i].protected) {
                last_unprotected = i;
                if (prng_rand() % 2) {
                    return i;
                }
            }
        }
        /* If no victim found yet take the last unprotected frame
           seen */
        if (last_unprotected < NUM_FRAMES) {
            return last_unprotected;
        }
    } else if (replacement == CLOCKSWEEP) {
        static size_t pfn = 0;
		while (1) {
			if(frame_table[pfn].protected == 0){
				if(frame_table[pfn].referenced == 1){
					frame_table[pfn].referenced = 0;
				}else{
					size_t pfn_old = pfn;
					pfn = (size_t)((pfn + 1) % num_entries);
					return pfn_old;
				}
			}
			pfn = (size_t)((pfn + 1) % num_entries);
		}
    }

    /* If every frame is protected, give up. This should never happen
       on the traces we provide you. */
    panic("System ran out of memory\n");
    exit(1);
}
