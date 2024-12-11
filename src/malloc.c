#include "malloc.h"
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define ALIGN4(s)         (((((s) - 1) >> 2) << 2) + 4)
#define BLOCK_DATA(b)     ((b) + 1)
#define BLOCK_HEADER(ptr) ((struct _block *)(ptr) - 1)

void initStatistics(void) __attribute__((constructor));

/* Statistics counters */
static int atexit_registered = 0;
static int num_mallocs       = 0;
static int num_frees         = 0;
static int num_reuses        = 0;
static int num_grows         = 0;
static int num_splits        = 0;
static int num_coalesces     = 0;
static int num_blocks        = 0;
static int num_requested     = 0;
static int max_heap          = 0;
char used = 'A';
int reset = 0;

// int atexit_registered = 0;
// int num_mallocs       = 0;
// int num_frees         = 0;
// int num_reuses        = 0;
// int num_grows         = 0;
// int num_splits        = 0;
// int num_coalesces     = 0;
// int num_blocks        = 0;
// int num_requested     = 0;
// int max_heap          = 0;


// void resetStatistics(void) {
//     num_mallocs = 0;
//     num_frees = 0;
//     num_reuses = 0;
//     num_grows = 0;
//     num_splits = 0;
//     num_coalesces = 0;
//     num_blocks = 0;
//     num_requested = 0;
//     max_heap = 0;
// }

// void initStatistics(void) {
//     // Initialize statistics
//     num_mallocs = 0;
//     num_frees = 0;
//     num_reuses = 0;
//     num_grows = 0;
//     num_splits = 0;
//     num_coalesces = 0;
//     num_blocks = 0;
//     num_requested = 0;
//     max_heap = 0;
//     printf("Statistics initialized.\n");
// }

/*
 * \brief printStatistics
 *
 * Prints the heap statistics upon process exit. Registered via atexit()
 */
void printStatistics(void) {
    printf("USED: %c\n", used);
    printf("\nheap management statistics\n");
    printf("mallocs:\t%d\n", num_mallocs);
    printf("frees:\t\t%d\n", num_frees);
    printf("reuses:\t\t%d\n", num_reuses);
    printf("grows:\t\t%d\n", num_grows);
    printf("splits:\t\t%d\n", num_splits);
    printf("coalesces:\t%d\n", num_coalesces);
    printf("blocks:\t\t%d\n", num_blocks);
    printf("requested:\t%d\n", num_requested);
    printf("max heap:\t%d\n", max_heap);
}

/* Structure for a block of memory */
struct _block {
    size_t size;         /* Size of the allocated block of memory in bytes */
    struct _block *next; /* Pointer to the next block of allocated memory */
    struct _block *prev; /* Pointer to the previous block of allocated memory */
    bool free;           /* Is this block free? */
    char padding[3];     /* Padding for alignment */
};

/* Pointer to the head of the heap list */
struct _block *heapList = NULL;

/*
 * \brief findFreeBlock
 *
 * Finds a free block of heap memory based on the allocation strategy.
 */
struct _block *findFreeBlock(struct _block **last, size_t size) {
    struct _block *curr = heapList;

#if defined FIT && FIT == 0
    /* First fit */
    used = 'F';
    while (curr && !(curr->free && curr->size >= size)) {
        *last = curr;
        curr = curr->next;
    }
    if (curr) {
        num_reuses++;  // Increment reuse counter when a block is reused
    }
#endif

#if defined BEST && BEST == 0
    /* Best fit */
    used = 'B';
    struct _block *best = NULL;
    while (curr) {
        if (curr->free && curr->size >= size) {
            if (!best || curr->size < best->size) {
                best = curr;
            }
        }
        *last = curr;
        curr = curr->next;
    }
    curr = best;
    if (curr) {
        num_reuses++;  // Increment reuse counter when a block is reused
    }
#endif

#if defined WORST && WORST == 0
    /* Worst fit */
    used = 'W';
    struct _block *worst = NULL;
    while (curr) {
        if (curr->free && curr->size >= size) {
            if (!worst || curr->size > worst->size) {
                worst = curr;
            }
        }
        *last = curr;
        curr = curr->next;
    }
    curr = worst;
    if (curr) {
        num_reuses++;
    }
#endif

#if defined NEXT && NEXT == 0
    /* Next fit */
    used = 'N';
    static struct _block *last_allocated = NULL;

    if (!heapList) {
        // If the heapList is uninitialized, no free blocks exist yet
        return NULL;
    }

    struct _block *start = last_allocated ? last_allocated->next : heapList;
    *last = NULL; // Ensure last is initialized properly

    // Perform a circular search for a free block
    do {
        if (start->free && start->size && start->size >= size) {
            last_allocated = start; // Update last_allocated for future allocations
            num_reuses++;
            return start;
        }
        start = start->next ? start->next : heapList; // Wrap around to the beginning if needed
    } while (start != (last_allocated ? last_allocated->next : heapList));

    // If no suitable block was found, return NULL
    return NULL;
#endif

    return curr;
}


/*
 * \brief growHeap
 *
 * Requests additional heap memory from the OS.
 */
struct _block *growHeap(struct _block *last, size_t size) {
    struct _block *curr = (struct _block *)sbrk(0);
    struct _block *prev = (struct _block *)sbrk(sizeof(struct _block) + size);

    assert(curr == prev);

    if (curr == (struct _block *)-1) {
        return NULL; // sbrk failed
    }

    // Initialize the new block
    curr->size = size;
    curr->free = false;
    curr->next = NULL;
    curr->prev = last;

    if (last) {
        last->next = curr; // Link the new block to the last block
    }

    num_grows++;
    num_blocks++;

    max_heap += sizeof(struct _block) + size;

    return curr;
}

/*
 * \brief malloc
 *
 * Allocates memory on the heap.
 */
void *malloc(size_t size) {

    if (atexit_registered == 0) {
        atexit_registered = 1;
        atexit(printStatistics);
    }

    size = ALIGN4(size);

    if (size == 0) {
        return NULL;
    }

    struct _block *last = NULL;
    struct _block *next = NULL;

    if (heapList == NULL) {
        // Initialize heapList on first allocation
        next = growHeap(NULL, size);
        heapList = next;
        if (next) {
            num_blocks++;
        }
    } else {
        // Find a free block or grow the heap
        next = findFreeBlock(&last, size);
        if (next == NULL) {
            next = growHeap(last, size);
        }
    }

    if (next == NULL) {
        return NULL; // Allocation failed
    }

    // Split the block if there's enough space for a new block
    if (next->size >= size + sizeof(struct _block) + 4) {
        struct _block *split_block = (struct _block *)((char *)BLOCK_DATA(next) + size);
        split_block->size = next->size - size - sizeof(struct _block);
        split_block->free = true;
        split_block->next = next->next;
        split_block->prev = next;

        if (split_block->next) {
            split_block->next->prev = split_block;
        }

        next->size = size;
        next->next = split_block;
        num_splits++;
    }

    next->free = false;
    num_mallocs++;
    num_requested += size;

    return BLOCK_DATA(next);
}

/*
 * \brief free
 *
 * Frees the allocated memory block and coalesces adjacent free blocks.
 */
void free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    struct _block *curr = BLOCK_HEADER(ptr);
    assert(curr->free == 0); // Ensure the block is not already freed
    curr->free = true;
    num_frees++;

    // Coalesce with next block if it's free
    if (curr->next && curr->next->free) {
        curr->size += curr->next->size + sizeof(struct _block);
        struct _block *next_next = curr->next->next;
        curr->next = next_next;
        if (next_next) {
            next_next->prev = curr;
        }
        num_coalesces++;
    }

    // Coalesce with previous block if it's free
    if (curr->prev && curr->prev->free) {
        struct _block *prev_block = curr->prev;
        prev_block->size += curr->size + sizeof(struct _block);
        prev_block->next = curr->next;
        if (curr->next) {
            curr->next->prev = prev_block;
        }
        num_coalesces++;
    }
}

void *calloc(size_t nmemb, size_t size) {
    size_t total_size = nmemb * size;
    void *ptr = malloc(total_size);

    if (ptr) {
        memset(ptr, 0, total_size);
    }

    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }

    struct _block *curr = BLOCK_HEADER(ptr);

    if (size <= curr->size) {
        return ptr;
    }

    void *new_ptr = malloc(size);

    if (new_ptr) {
        memcpy(new_ptr, ptr, curr->size);
        free(ptr);
    }

    return new_ptr;
}
