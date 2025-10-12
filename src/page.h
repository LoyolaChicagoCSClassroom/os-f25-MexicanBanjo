#ifndef PAGE_H
#define PAGE_H

struct ppage {
    struct ppage *next;
    struct ppage *prev;
    void *physical_addr;
};

// Initializes the list of free physical pages
void init_pfa_list(void);

// Allocates npages from free list and returns a linked list of allocated pages
struct ppage *allocate_physical_pages(unsigned int npages);

// Frees a list of physical pages (returns to free list)
void free_physical_pages(struct ppage *ppage_list);

#endif
