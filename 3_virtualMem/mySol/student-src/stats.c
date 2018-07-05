#include "paging.h"
#include "stats.h"

/* The stats. See the definition in stats.h. */
stats_t stats;

/*  --------------------------------- PROBLEM 10 --------------------------------------
    Calculate any remaining statistics to print out.

    You will need to include code to increment many of these stats in
    the functions you have written for other parts of the project.

    Use this function to calculate any remaining stats, such as the
    average access time (AAT).

    You may find the #defines in the stats.h file useful.
    -----------------------------------------------------------------------------------
*/
void compute_stats() {
    double access_t =(double)(stats.accesses * MEMORY_READ_TIME);
    double rDisk_t = (double)(stats.page_faults * DISK_PAGE_READ_TIME);
    double wDisk_t = (double)(stats.writebacks) * DISK_PAGE_WRITE_TIME;
    stats.aat = (access_t + rDisk_t + wDisk_t) / stats.accesses;
}
