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
  error_handling("init_cache");
  int i;
  Pcache c[2] = { &c1, &c2 };
  int cache_size[2] = { cache_isize, cache_dsize };
  for (i = 0; i < (cache_split ? 2 : 1); ++i) {
	c[i]->size = cache_split ? cache_size[i] : cache_usize;
    c[i]->associativity = cache_assoc;
    c[i]->n_sets = c[i]->size / (cache_block_size * c[i]->associativity);
    c[i]->index_mask = c[i]->n_sets - 1;
    c[i]->index_mask_offset = LOG2(c[i]->index_mask);

    c[i]->set_contents = (int*)malloc(sizeof(int)*c[i]->n_sets);
    {
      int j;
	  for (j = 0; j < c[i]->n_sets; ++j) {
	    c[i]->set_contents[j] = 0;
	  }
    }

    c[i]->contents = 0;

    c[i]->LRU_head = (Pcache_line*)malloc(sizeof(Pcache_line)*c[i]->n_sets);
    {
      int j;
	  for (j = 0; j < c[i]->n_sets; ++j) {
	    c[i]->LRU_head[j] = NULL;
	  }
    }
    c[i]->LRU_tail = (Pcache_line*)malloc(sizeof(Pcache_line)*c[i]->n_sets);
    {
	  int j;
	  for (j = 0; j < c[i]->n_sets; ++j) {
	    c[i]->LRU_tail[j] = NULL;
	  }
    }
  }

  // statistics setting 
  memset(&cache_stat_inst, 0, sizeof(cache_stat));
  memset(&cache_stat_data, 0, sizeof(cache_stat));

  error_handling("init end");
}
/************************************************************/

/************************************************************/
void destroy_cache()
{
	int i;
	assert(c1.LRU_head);
	assert(c1.LRU_tail);

	for (i = 0; i < c1.n_sets; ++i) {
		Pcache_line head = c1.LRU_head[i], tail = c1.LRU_tail[i], idx = NULL;
		for (idx = head; head && idx && idx != tail;) {
		  Pcache_line del = idx;
		  idx = idx->LRU_next;
		  free(del);
		}
		free(tail);

		c1.LRU_head[i] = c1.LRU_tail[i] = NULL;
	}

	free(c1.LRU_head);
	c1.LRU_head = NULL;

	free(c1.LRU_tail);
	c1.LRU_tail = NULL;

	free(c1.set_contents);
	c1.set_contents = NULL;
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

/************************************************************/
/* handle an access to the cache */
void perform_access(addr, access_type)
  unsigned addr, access_type;
{
  int idx, tag;
  Pcache c[2] = { &c1, &c2 };
  Pcache ch = (cache_split && (access_type != TRACE_INST_LOAD)) ? c[1] : c[0];

  increment_cache_stat(ECS_ACCESSES, access_type);

  // calculate index and tag
  idx = ((addr >> (LOG2(words_per_block) + WORD_SIZE_OFFSET)) & ch->index_mask);
  tag = ((addr >> (LOG2(words_per_block) + WORD_SIZE_OFFSET)) & ~ch->index_mask) >> ch->index_mask_offset;

  Pcache_line *head = &ch->LRU_head[idx], *tail = &ch->LRU_tail[idx];
  Pcache_line finder = *tail;
  
  if (*head) {
	error_handling("find");
    finder = isIn(head, tail, tag);
	error_handling("find end");
  }

  if (ch->set_contents[idx] == 0
	  || finder == NULL && ch->associativity > ch->set_contents[idx]) {
	error_handling("cold miss");
	Pcache_line cl = NULL;
	increment_cache_stat(ECS_MISSES, access_type);
	increment_cache_stat(ECS_DEMAND_FETCHES, access_type);

	cl = (Pcache_line)malloc(sizeof(cache_line));
	cl->tag = tag;
	cl->dirty = ((access_type == TRACE_DATA_STORE) && cache_writeback) ? TRUE : FALSE;
	cl->LRU_next = cl->LRU_prev = NULL;
	
	insert(head, tail, cl);
	ch->set_contents[idx]++;
	ch->contents++;
	error_handling("cold miss end");
  } else if (access_type == TRACE_INST_LOAD || access_type == TRACE_DATA_LOAD) { // read
    error_handling("read");
	if (finder == NULL) { // found!
	  finder = *tail;

	  increment_cache_stat(ECS_MISSES, access_type);
	  increment_cache_stat(ECS_DEMAND_FETCHES, access_type);
	  increment_cache_stat(ECS_REPLACEMENTS, access_type);

	  finder->tag = tag;

	  if (cache_writeback && finder->dirty) {
		increment_cache_stat(ECS_COPIES_BACK, access_type);

	    finder->dirty = FALSE;
	  }

	  error_handling("delete");
	  delete(head, tail, finder);
	  error_handling("insert");
	  insert(head, tail, finder);
	} else {
	  delete(head, tail, finder);
	  insert(head, tail, finder);
	}
	error_handling("read end");
  } else if (access_type == TRACE_DATA_STORE) { // write
    error_handling("write");
	if (finder == NULL) { // found!
	  finder = *tail;

	  increment_cache_stat(ECS_MISSES, access_type);
	  
	  finder->dirty = TRUE;

	  if (cache_writealloc) {
		finder->tag = tag;
	  }

	  if (cache_writealloc == FALSE) {
		increment_cache_stat(ECS_COPIES_BACK, access_type);
	  } else if (cache_writeback && finder->dirty) {
		increment_cache_stat(ECS_COPIES_BACK, access_type);
		increment_cache_stat(ECS_REPLACEMENTS, access_type);
	  } else if (cache_writeback == FALSE) {
		increment_cache_stat(ECS_COPIES_BACK, access_type);
		increment_cache_stat(ECS_REPLACEMENTS, access_type);
	  }

	  delete(head, tail, finder);
	  insert(head, tail, finder);
	} else {
	  delete(head, tail, finder);
	  insert(head, tail, finder);
	}
	error_handling("write end");
  }
}
/************************************************************/

/************************************************************/
/* flush the cache */
void flush()
{
  int i;
  Pcache ch = cache_split ? &c1 : &c2;

  for (i = 0; i < ch->n_sets; ++i) {
	Pcache_line accessor = ch->LRU_head[i], tail = ch->LRU_tail[i];
	if (accessor == NULL) {
	  continue;
	}

	while (accessor != tail) {
	  if (accessor->dirty) {
		increment_cache_stat(ECS_COPIES_BACK, ~TRACE_INST_LOAD); // All remained dirty bit is data bit.

		accessor->dirty = FALSE;
	  }
	  accessor = accessor->LRU_next;
	}
	if (tail->dirty) {
	  increment_cache_stat(ECS_COPIES_BACK, ~TRACE_INST_LOAD);
	  tail->dirty = FALSE;
	}
  }
}
/************************************************************/

/************************************************************/
Pcache_line isIn(head, tail, tag)
  Pcache_line *head, *tail;
  unsigned tag;
{
  Pcache_line accessor = *head;

  while (accessor != *tail) {
	assert(accessor);
    if (accessor->tag == tag) {
	  return accessor;
	}

	accessor = accessor->LRU_next;
  }
  if (*tail && (*tail)->tag == tag) {
	return *tail;
  }

  return NULL;
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

/************************************************************/
void error_handling(const char* msg)
{
#ifdef DEBUG
  puts(msg);
#endif // DEBUG
}
/************************************************************/

