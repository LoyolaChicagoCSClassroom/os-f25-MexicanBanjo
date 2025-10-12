#include "page.h"

#define PAGE_SIZE_MB 2
#define NUM_PAGES 128   // or however many your assignment specifies

static struct ppage physical_page_array[NUM_PAGES];
static struct ppage *free_page_list = 0;

void init_pfa_list(void) {
    for (int i = 0; i < NUM_PAGES; i++) {
        physical_page_array[i].physical_addr = (void *)(i * PAGE_SIZE_MB * 1024 * 1024);
        physical_page_array[i].prev = (i > 0) ? &physical_page_array[i - 1] : 0;
        physical_page_array[i].next = (i < NUM_PAGES - 1) ? &physical_page_array[i + 1] : 0;
    }
    free_page_list = &physical_page_array[0];
}

struct ppage *allocate_physical_pages(unsigned int npages) {
    if (!free_page_list) return 0;

    struct ppage *allocd_list = free_page_list;
    struct ppage *iter = free_page_list;

    for (unsigned int i = 1; i < npages && iter->next; i++) {
        iter = iter->next;
    }

    free_page_list = iter->next;
    if (free_page_list)
        free_page_list->prev = 0;

    iter->next = 0;
    return allocd_list;
}

void free_physical_pages(struct ppage *ppage_list) {
    if (!ppage_list) return;

    struct ppage *tail = ppage_list;
    while (tail->next)
        tail = tail->next;

    // attach freed pages back to the head of the free list
    tail->next = free_page_list;
    if (free_page_list)
        free_page_list->prev = tail;

    free_page_list = ppage_list;
}

