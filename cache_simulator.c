/**
 * @file cache_simulator.c
 *
 * @brief Implementation of a cache simulator. The simulator is implemented 
 *        with doubly linked list to keep track of the LRU.
 * @author Wenqi Deng <wenqid@andrew.cmu.edu>
 */

#include "cachelab.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ADDRESS_BITS 64

/* Represent each line using a struct */
typedef struct {
    int valid_bit;     /* whether the line is valid */
    int dirty_bit;     /* whether the line is modified */
    unsigned long tag; /* the tag of the line */
} cache_line;

/* Use a queue (implemented with doubly linked list) to keep track of LRU */
typedef struct queue_node {
    struct queue_node *prev;
    cache_line *line;
    struct queue_node *next;
} queue_node_t;

typedef struct queue_set {
    queue_node_t *head; /* head of the queue, most recently used node */
    queue_node_t *tail; /* tail of the queue, least recently used node */
    int curr_line_num;  /* current number of nodes in the set */
} queue_set_t;

/** initialize global variables
 *  hit: count number of hits.
 *  miss: count number of misses.
 *  eviction: count number of evictions.
 *  dirty_count: count number of lines which the dirty bit is set to 1.
 *  dirty_eviction: count number of evictions that the dirty bit is set to 1.
 *  s   number of set bits
 *  S   number of set, can be calculated by S = 2^s
 *  b   number of bits that represent byte offset
 *  B   number of bytes in a block, can be calculated by B = 2^b
 *  t   number of tag bits, t = 64 - s - b
 *  E   number of line per set
 */
int hit = 0;
int miss = 0;
int eviction = 0;
int dirty_count = 0;
int dirty_eviction = 0;
int s, E, b, S, B, t;

/** @brief update the number of hit, miss, eviction and dirty eviction and
 *         store in the global variables, with the given tag and set index.
 *
 *  @param[in]     cache          Pointer to the dynamically allocated cache.
 *  @param[in]     curr_tag       Tag bits computed using the address.
 *  @param[in]     curr_set_num   Set index computed using the address.
 *  @param[in]     dirty          Set to 0 if it's a load operation,
 *                                to 1 if it's a store operation.
 */
void count(queue_set_t *cache, unsigned long curr_tag,
           unsigned long curr_set_num, int dirty) {
    int operation_complete = 0;

    // check hit by searching for the line with the same tag
    queue_node_t *check_node = cache[curr_set_num].head;
    while (check_node != NULL) {
        cache_line *check_line = check_node->line;
        if (check_line != NULL && check_line->valid_bit != 0 &&
            check_line->tag == curr_tag) {
            hit += 1;
            // if the operation is store, mark the dirty bit of the line
            if (dirty == 1)
                check_line->dirty_bit = 1;
            // move the node to the most recently used
            queue_node_t *prev_node = check_node->prev;
            if (prev_node != NULL) {
                // if the current node is the least recently used
                if (cache[curr_set_num].tail == check_node) {
                    cache[curr_set_num].tail = prev_node;
                } else {
                    // if not the least recently used, the next node != NULL
                    check_node->next->prev = prev_node;
                }
                prev_node->next = check_node->next;
                check_node->prev = NULL;
                check_node->next = cache[curr_set_num].head;
                cache[curr_set_num].head->prev = check_node;
                cache[curr_set_num].head = check_node;
            }

            // mark the operation as complete
            operation_complete = 1;
            break;
        }
        check_node = check_node->next;
    }

    // if not hit, then there is a miss
    if (operation_complete == 0) {
        miss += 1;
        // create a new line that will be inserted to the cache
        cache_line *new_line = malloc(sizeof(cache_line));
        new_line->tag = curr_tag;
        new_line->valid_bit = 1;
        new_line->dirty_bit = dirty;
        queue_node_t *new_node = malloc(sizeof(queue_node_t));
        new_node->line = new_line;
        if (cache[curr_set_num].curr_line_num >= E) {
            eviction += 1;
            queue_node_t *evict_node = cache[curr_set_num].tail;
            if (evict_node != NULL && evict_node->prev != NULL) {
                // if E is greater than 1, the previous node exists
                queue_node_t *prev_node = evict_node->prev;
                evict_node->prev = NULL;
                prev_node->next = NULL;
                cache[curr_set_num].tail = prev_node;
            } else if (cache[curr_set_num].curr_line_num == 1) {
                // if E is equal to 1
                cache[curr_set_num].head = NULL;
                cache[curr_set_num].tail = NULL;
            }
            if (evict_node->line->dirty_bit == 1) {
                dirty_eviction += 1;
            }
            // free the evicted lines and nodes
            free(evict_node->line);
            free(evict_node);
            cache[curr_set_num].curr_line_num -= 1;
        }
        // insert the new node
        new_node->prev = NULL;
        new_node->next = cache[curr_set_num].head;
        if (cache[curr_set_num].head != NULL) {
            // if there is any node in the set before
            cache[curr_set_num].head->prev = new_node;
        } else {
            // if there is no node in the set before
            cache[curr_set_num].tail = new_node;
        }
        cache[curr_set_num].head = new_node;
        cache[curr_set_num].curr_line_num += 1;
        operation_complete = 1;
    }
}

/** @brief free cache while counting how many dirty bits exist,
 *         store in the globle variable dirty_count
 *
 *  @param[in]     cache     Pointer to the dynamically allocated cache.
 */
void free_cache(queue_set_t *cache) {
    if (cache != NULL) {
        /* Free the queue elements and the line structs */
        for (int i = 0; i < S; i++) {
            queue_node_t *qHead = cache[i].head;
            while (qHead != NULL) {
                if (qHead->line != NULL) {
                    if (qHead->line->dirty_bit == 1) {
                        dirty_count += 1;
                    }
                    free(qHead->line);
                }
                queue_node_t *qNext = qHead->next;
                free(qHead);
                qHead = qNext;
            }
        }
    }
    free(cache);
}

int main(int argc, char **argv) {
    // initialize static variables
    char *file_path;
    int opt;

    // get parameters about the cache and the path to the trace
    while (-1 != (opt = getopt(argc, argv, "s:E:b:t:"))) {
        switch (opt) {
        case 's':
            s = atoi(optarg);
            break;
        case 'E':
            E = atoi(optarg);
            break;
        case 'b':
            b = atoi(optarg);
            break;
        case 't':
            file_path = malloc((strlen(optarg) + 1) * sizeof(char));
            if (file_path == NULL) {
                printf("Error in memory allocation\n");
                return 0;
            }
            strcpy(file_path, optarg);
            break;
        default:
            printf("Argument not valid\n");
            break;
        }
    }

    // update the number of sets, byte offset and tag bits
    S = 1 << s;
    B = 1 << b;
    t = ADDRESS_BITS - (s + b);

    // create cache: an array of sets
    queue_set_t *cache = malloc(((size_t)S) * sizeof(queue_set_t));
    for (int i = 0; i < S; i++) {
        cache[i].head = NULL;
        cache[i].tail = NULL;
        cache[i].curr_line_num = 0;
    }

    // read the operations in the trace file
    FILE *pFile = fopen(file_path, "r");
    char operation;
    unsigned long address;
    int size;

    while (fscanf(pFile, "%c %lx,%d", &operation, &address, &size) > 0) {
        unsigned long curr_tag = address >> (s + b);
        unsigned long curr_set_num = (address << t) >> (b + t);
        if (t == ADDRESS_BITS)
            curr_set_num = 0;
        if (operation == 'L') {
            count(cache, curr_tag, curr_set_num, 0);
        } else if (operation == 'S') {
            count(cache, curr_tag, curr_set_num, 1);
        }
    }
    fclose(pFile);
    csim_stats_t *stats = malloc(sizeof(csim_stats_t));
    free_cache(cache);

    // write the result into the struct stats
    stats->misses = (unsigned long)miss;
    stats->hits = (unsigned long)hit;
    stats->evictions = (unsigned long)eviction;
    stats->dirty_evictions = (unsigned long)(dirty_eviction * B);
    stats->dirty_bytes = (unsigned long)(dirty_count * B);
    printSummary(stats);

    // free the memory allocated
    free(stats);
    free(file_path);
    return 0;
}
