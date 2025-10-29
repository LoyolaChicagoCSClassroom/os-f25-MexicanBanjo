#include "paging.h"
#include <stdint.h>
#include <string.h> // for memset if available in your freestanding env
#include "page.h"

// Globals required by the assignment: aligned to 4096 and global (not on stack)
struct page_directory_entry pd[1024] __attribute__((aligned(4096)));
struct page pt[1024] __attribute__((aligned(4096)));

/*
 * map_pages:
 *   Maps the linked list of physical pages (pglist) starting at virtual address vaddr
 *   using the page-directory 'pd'. Returns the original vaddr mapped.
 *
 * NOTE: This implementation places a single second-level page table (pt) into the
 * directory at the directory index of vaddr. It only maps entries that lie within
 * that single 4 MiB directory slot. If the requested mapping crosses a 4 MiB boundary,
 * you must extend this implementation to allocate more page tables and add them
 * to the page directory.
 */
void *map_pages(void *vaddr, struct ppage *pglist, struct page_directory_entry *pd_ptr)
{
    uintptr_t va = (uintptr_t)vaddr;
    uintptr_t start_va = va;
    struct ppage *cur = pglist;

    // compute directory index for the starting virtual address
    uint32_t dir_idx = (va >> 22) & 0x3FF;

    // If directory entry not present, point it to our single 'pt' array.
    if (!pd_ptr[dir_idx].present) {
        // clear the page table before use
        for (int i = 0; i < 1024; ++i) {
            pt[i].present = 0;
            pt[i].rw = 0;
            pt[i].user = 0;
            pt[i].accessed = 0;
            pt[i].dirty = 0;
            pt[i].unused = 0;
            pt[i].frame = 0;
        }

        pd_ptr[dir_idx].present = 1;
        pd_ptr[dir_idx].rw = 1;    // writable
        pd_ptr[dir_idx].user = 0;  // supervisor
        pd_ptr[dir_idx].writethru = 0;
        pd_ptr[dir_idx].cachedisabled = 0;
        pd_ptr[dir_idx].accessed = 0;
        pd_ptr[dir_idx].pagesize = 0; // points to 4KB page table
        pd_ptr[dir_idx].frame = ((uint32_t)pt) >> 12;
    } else {
        // if present, ensure it actually points to `pt` in our simple model
        // (in a more complete implementation you'd locate the correct table)
    }

    // Map each page from the pglist into successive 4KB pages starting at vaddr
    while (cur) {
        uint32_t tbl_idx = (va >> 12) & 0x3FF;

        // If mapping crosses directory boundary - stop (or handle allocating new page table)
        uint32_t cur_dir_idx = (va >> 22) & 0x3FF;
        if (cur_dir_idx != dir_idx) {
            // This simple implementation only maps inside the same 4MB directory entry.
            // Caller must ensure mappings are inside same 4MB boundary or extend this function.
            break;
        }

        // fill page table entry
        uint32_t phys = (uint32_t) cur->physical_addr;
        pt[tbl_idx].present = 1;
        pt[tbl_idx].rw = 1;      // read-write
        pt[tbl_idx].user = 0;    // supervisor
        pt[tbl_idx].accessed = 0;
        pt[tbl_idx].dirty = 0;
        pt[tbl_idx].unused = 0;
        pt[tbl_idx].frame = phys >> 12;

        // advance
        va += 4096;
        cur = cur->next;
    }

    return (void*)start_va;
}

void loadPageDirectory(struct page_directory_entry *pd_ptr) {
    asm("mov %0,%%cr3" : : "r"(pd_ptr) : );
}

void enable_paging(void) {
    // Set CR0.PG (bit 31) and CR0.PE (bit 0) if PE is not already set.
    // Typically PE is already set when in protected mode; we OR both bits to be safe.
    asm volatile (
        "mov %%cr0, %%eax\n"
        "or $0x80000001, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : : "eax"
    );
}
