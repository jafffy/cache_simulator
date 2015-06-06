/*
 * cache.c
 */


#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

#include "cache.h"
#include "main.h"

/* cache configuration parameters */
static int cache_split = 0;
static int cache_usize = DEFAULT_CACHE_SIZE;
static int cache_isize = DEFAULT_CACHE_SIZE; 
static int cache_dsize = DEFAULT_CACHE_SIZE;
static int cache_block_size = DEFAULT_CACHE_BLOCK_SIZE;
static int words_per_block = DEFAULT_CACHE_BLOCK_SIZE / WORD_SIZE;
static int cache_assoc = DEFAULT_CACHE_ASSOC;
static int cache_writeback = DEFAULT_CACHE_WRITEBACK;
static int cache_writealloc = DEFAULT_CACHE_WRITEALLOC;

/* cache model data structures */
static Pcache icache;
static Pcache dcache;
static cache c1;
static cache c2;
static cache_stat cache_stat_inst;
static cache_stat cache_stat_data;

/************************************************************/
void set_cache_param(param, value)
  int param;
  int value;
{

  switch (param) {
  case CACHE_PARAM_BLOCK_SIZE:
    cache_block_size = value;
    words_per_block = value / WORD_SIZE;
    break;
  case CACHE_PARAM_USIZE:
    cache_split = FALSE;
    cache_usize = value;
    break;
  case CACHE_PARAM_ISIZE:
    cache_split = TRUE;
    cache_isize = value;
    break;
  case CACHE_PARAM_DSIZE:
    cache_split = TRUE;
    cache_dsize = value;
    break;
  case CACHE_PARAM_ASSOC:
    cache_assoc = value;
    break;
  case CACHE_PARAM_WRITEBACK:
    cache_writeback = TRUE;
    break;
  case CACHE_PARAM_WRITETHROUGH:
    cache_writeback = FALSE;
    break;
  case CACHE_PARAM_WRITEALLOC:
    cache_writealloc = TRUE;
    break;
  case CACHE_PARAM_NOWRITEALLOC:
    cache_writealloc = FALSE;
    break;
  default:
    printf("error set_cache_param: bad parameter value\n");
    exit(-1);
  }

}
/************************************************************/

/************************************************************/
void init_cache()
{
  // Use c1 as Unified cache
  c1.size = cache_usize;
  c1.associativity = 0;
  c1.n_sets = c1.size / cache_block_size;
  c1.index_mask = c1.n_sets - 1;
  c1.index_mask_offset = LOG2(c1.index_mask); // what is it...

  c1.LRU_head = (Pcache_line*)malloc(sizeof(Pcache_line)*c1.n_sets);
  {
    int i;
	for (i = 0; i < c1.n_sets; ++i) {
	  c1.LRU_head[i] = NULL;
	}
  }
  c1.LRU_tail = c1.LRU_head + c1.n_sets - 1;

  // statistics setting 
  memset(&cache_stat_inst, 0, sizeof(cache_stat));
  memset(&cache_stat_data, 0, sizeof(cache_stat));
}
/************************************************************/

/************************************************************/
void destroy_cache()
{
	int i;
	assert(c1.LRU_head);

	for (i = 0; i < c1.n_sets; ++i) {
		if (c1.LRU_head[i] != NULL) {
			free(c1.LRU_head[i]);
			c1.LRU_head[i] = NULL;
		}
	}

	free(c1.LRU_head);
	c1.LRU_head = NULL;
}
/************************************************************/

enum E_CACHE_STAT {
  ECS_ACCESSES,
  ECS_MISSES,
  ECS_REPLACEMENTS,
  ECS_DEMAND_FETCHES,
  ECS_COPIES_BACK,
  ECS_COUNT
};

void increment_cache_stat(enum E_CACHE_STAT cache_stat, unsigned access_type)
{
  Pcache_stat stat = access_type == TRACE_INST_LOAD ? &cache_stat_inst : &cache_stat_data;

  switch (cache_stat) {
  case ECS_ACCESSES:
    stat->accesses++;
  	break;
  case ECS_MISSES:
    stat->misses++;
	break;
  case ECS_REPLACEMENTS:
    stat->replacements++;
	break;
  case ECS_DEMAND_FETCHES:
    stat->demand_fetches++;
	break;
  case ECS_COPIES_BACK:
    stat->copies_back++;
	break;
  }
}

static int cold_miss = 0;

/************************************************************/
/* handle an access to the cache */
void perform_access(addr, access_type)
  unsigned addr, access_type;
{
  int idx, tag;

  increment_cache_stat(ECS_ACCESSES, access_type);

  // calculate index and tag
  idx = (addr & c1.index_mask);
  tag = (addr & ~c1.index_mask) >> c1.index_mask_offset;

  // cold miss
  if (c1.LRU_head[idx] == NULL) {
	increment_cache_stat(ECS_MISSES, access_type);
	increment_cache_stat(ECS_DEMAND_FETCHES, access_type);

	c1.LRU_head[idx] = (Pcache_line)malloc(sizeof(cache_line));
	c1.LRU_head[idx]->tag = tag;
	c1.LRU_head[idx]->dirty = cache_writeback ? TRUE : FALSE;
  }

  if (access_type == 0 || access_type == 2) { // read
	if (c1.LRU_head[idx]->tag != tag) {
	  increment_cache_stat(ECS_MISSES, access_type);
	  increment_cache_stat(ECS_DEMAND_FETCHES, access_type);
	  increment_cache_stat(ECS_REPLACEMENTS, access_type);

	  c1.LRU_head[idx]->tag = tag;

	  if (cache_writeback && c1.LRU_head[idx]->dirty) {
		increment_cache_stat(ECS_COPIES_BACK, access_type);

	    c1.LRU_head[idx]->dirty = FALSE;
	  }
	}
  } else if (access_type == 1) { // write
	  if (c1.LRU_head[idx]->tag != tag) {
		increment_cache_stat(ECS_MISSES, access_type);

		if (cache_writealloc) {
		  c1.LRU_head[idx]->tag = tag;
		}

		if (cache_writeback && c1.LRU_head[idx]->dirty) {
		  increment_cache_stat(ECS_COPIES_BACK, access_type);
		} else if (cache_writeback == FALSE) {
		  increment_cache_stat(ECS_COPIES_BACK, access_type); 
		} else if (cache_writealloc == FALSE) {
		  increment_cache_stat(ECS_COPIES_BACK, access_type);
		}
	  }
  }
}
/************************************************************/

/************************************************************/
/* flush the cache */
void flush()
{
  printf("%u\n", cold_miss);
  int i;

  // directed mapped
  for (i = 0; i < c1.n_sets; ++i) {
	if (c1.LRU_head[i] == NULL) {
		continue;
	}
    if (c1.LRU_head[i]->dirty) {
	  increment_cache_stat(ECS_COPIES_BACK, ~TRACE_INST_LOAD); // All remained dirty bit is data bit.
	  c1.LRU_head[i]->dirty = FALSE;
	}
  }
}
/************************************************************/

/************************************************************/
void delete(head, tail, item)
  Pcache_line *head, *tail;
  Pcache_line item;
{
  if (item->LRU_prev) {
    item->LRU_prev->LRU_next = item->LRU_next;
  } else {
    /* item at head */
    *head = item->LRU_next;
  }

  if (item->LRU_next) {
    item->LRU_next->LRU_prev = item->LRU_prev;
  } else {
    /* item at tail */
    *tail = item->LRU_prev;
  }
}
/************************************************************/

/************************************************************/
/* inserts at the head of the list */
void insert(head, tail, item)
  Pcache_line *head, *tail;
  Pcache_line item;
{
  item->LRU_next = *head;
  item->LRU_prev = (Pcache_line)NULL;

  if (item->LRU_next)
    item->LRU_next->LRU_prev = item;
  else
    *tail = item;

  *head = item;
}
/************************************************************/

/************************************************************/
void dump_settings()
{
  printf("*** CACHE SETTINGS ***\n");
  if (cache_split) {
    printf("  Split I- D-cache\n");
    printf("  I-cache size: \t%d\n", cache_isize);
    printf("  D-cache size: \t%d\n", cache_dsize);
  } else {
    printf("  Unified I- D-cache\n");
    printf("  Size: \t%d\n", cache_usize);
  }
  printf("  Associativity: \t%d\n", cache_assoc);
  printf("  Block size: \t%d\n", cache_block_size);
  printf("  Write policy: \t%s\n", 
	 cache_writeback ? "WRITE BACK" : "WRITE THROUGH");
  printf("  Allocation policy: \t%s\n",
	 cache_writealloc ? "WRITE ALLOCATE" : "WRITE NO ALLOCATE");
}
/************************************************************/

/************************************************************/
void print_stats()
{
  printf("\n*** CACHE STATISTICS ***\n");

  printf(" INSTRUCTIONS\n");
  printf("  accesses:  %d\n", cache_stat_inst.accesses);
  printf("  misses:    %d\n", cache_stat_inst.misses);
  if (!cache_stat_inst.accesses)
    printf("  miss rate: 0 (0)\n"); 
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n", 
	 (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses,
	 1.0 - (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses);
  printf("  replace:   %d\n", cache_stat_inst.replacements);

  printf(" DATA\n");
  printf("  accesses:  %d\n", cache_stat_data.accesses);
  printf("  misses:    %d\n", cache_stat_data.misses);
  if (!cache_stat_data.accesses)
    printf("  miss rate: 0 (0)\n"); 
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n", 
	 (float)cache_stat_data.misses / (float)cache_stat_data.accesses,
	 1.0 - (float)cache_stat_data.misses / (float)cache_stat_data.accesses);
  printf("  replace:   %d\n", cache_stat_data.replacements);

  printf(" TRAFFIC (in words)\n");
  printf("  demand fetch:  %d\n", cache_stat_inst.demand_fetches + 
	 cache_stat_data.demand_fetches);
  printf("  copies back:   %d\n", cache_stat_inst.copies_back +
	 cache_stat_data.copies_back);
}
/************************************************************/
