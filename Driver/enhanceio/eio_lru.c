/*
 *  eio_lru.c
 *
 *  Copyright (C) 2012 STEC, Inc. All rights not specifically granted
 *   under a license included herein are reserved
 *  Made EnhanceIO specific changes.
 *   Saied Kazemi <skazemi@stec-inc.com>
 *   Siddharth Choudhuri <schoudhuri@stec-inc.com>
 *
 *  Copyright 2010 Facebook, Inc.
 *   Author: Mohan Srinivasan (mohan@facebook.com)
 *
 *  Based on DM-Cache:
 *   Copyright (C) International Business Machines Corp., 2006
 *   Author: Ming Zhao (mingzhao@ufl.edu)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "eio.h"

#define EIO_LRU_NULL    0xFFFF

/* Generic policy functions prototyes */
int eio_lru_init(struct cache_c *);
void eio_lru_exit(void);
int eio_lru_cache_sets_init(struct eio_policy *);
int eio_lru_cache_blk_init(struct eio_policy *);

//notify to the policy a cache block hit at index [index]
void EIOPolicyLRU_NotifyHit(struct eio_policy *pOps, index_t Index);
//notify the addition of a new valid cache block at [index]		
void EIOPolicyLRU_NotifyNew(struct eio_policy *pOps, index_t Index);	
//notify the removal of a cache block at [index]		
void EIOPolicyLRU_NotifyDelete(struct eio_policy *pOps, index_t Index);
//ask the policy to find a victim block in set [set] to cache a new one, policy return the index of the cache block to evict [index] (but do not actually evict it !)		
void EIOPolicyLRU_FindVictim(struct eio_policy *pOps, index_t Set, index_t *pIndex);
//ask the policy to flush a whole set	
void EIOPolicyLRU_FlushSet(struct eio_policy *pOps, index_t Set, u_int32_t u32NbMaxBlockToFlush, u_int32_t* pu32NbFlushedBlock);

/* Per policy instance initialization */
struct eio_policy *eio_lru_instance_init(void);

/* LRU specific policy functions prototype */
void eio_lru_movetail(struct eio_policy *p_ops, index_t BlkAbsoluteIndex);

/* Per cache set data structure */
struct eio_lru_cache_set {
	u_int16_t lru_head;
	u_int16_t lru_tail;
};

/* Per cache block data structure */
struct eio_lru_cache_block {
	u_int16_t lru_prev;
	u_int16_t lru_next;
};


/*
 * Context that captures the LRU replacement policy
 */
static struct eio_policy_header eio_lru_ops = {
	.sph_name		= CACHE_REPL_LRU,
	.sph_instance_init	= eio_lru_instance_init,
};

/*
 * Intialize LRU. Called from ctr.
 */
int eio_lru_init(struct cache_c *dmc)
{
	return 0;
}
/*
 * Allocate a new instance of eio_policy per dmc
 */
struct eio_policy *eio_lru_instance_init(void)
{
	struct eio_policy *new_instance;

	new_instance = vmalloc(sizeof(struct eio_policy));
	if (new_instance == NULL) {
		pr_err("eio_lru_instance_init: vmalloc failed");
		return NULL;
	}

	/* Initialize the LRU specific functions and variables */
	new_instance->sp_name = CACHE_REPL_LRU;
	new_instance->sp_repl_init = eio_lru_init;
	new_instance->sp_repl_exit = eio_lru_exit;
	new_instance->sp_repl_sets_init = eio_lru_cache_sets_init;
	new_instance->sp_repl_blk_init = eio_lru_cache_blk_init;

	new_instance->pfNotifyHit		= EIOPolicyLRU_NotifyHit;
	new_instance->pfNotifyMiss		= NULL;	
	new_instance->pfNotifyNew		= EIOPolicyLRU_NotifyNew;
	new_instance->pfNotifyDelete		= EIOPolicyLRU_NotifyDelete;
	new_instance->pfFindVictim		= EIOPolicyLRU_FindVictim;
	new_instance->pfFlushSet		= EIOPolicyLRU_FlushSet;

	new_instance->sp_dmc = NULL;

	try_module_get(THIS_MODULE);

	pr_info("eio_lru_instance_init: created new instance of LRU");

	return new_instance;
}

/*
 * Initialize per set LRU data structures.
 */
int eio_lru_cache_sets_init(struct eio_policy *p_ops)
{
	sector_t order;
	int i;
	struct cache_c *dmc = p_ops->sp_dmc;
	struct eio_lru_cache_set *cache_sets;

	order =
		(dmc->size >> dmc->consecutive_shift) *
		sizeof(struct eio_lru_cache_set);

	dmc->sp_cache_set = vmalloc((size_t)order);
	if (dmc->sp_cache_set == NULL)
		return -ENOMEM;

	cache_sets = (struct eio_lru_cache_set *)dmc->sp_cache_set;

	for (i = 0; i < (int)(dmc->size >> dmc->consecutive_shift); i++) {
		cache_sets[i].lru_tail = EIO_LRU_NULL;
		cache_sets[i].lru_head = EIO_LRU_NULL;
	}
	pr_info("Initialized %d sets in LRU", i);

	return 0;
}

/*
 * Initialize per block LRU data structures
 */
int eio_lru_cache_blk_init(struct eio_policy *p_ops)
{
	sector_t order;
	index_t BlkIndex; 
	struct cache_c *dmc = p_ops->sp_dmc;
	struct eio_lru_cache_block* 	ar_CacheBlks = NULL;

	order = dmc->size * sizeof(struct eio_lru_cache_block);

	dmc->sp_cache_blk = vmalloc((size_t)order);
	if (dmc->sp_cache_blk == NULL)
		return -ENOMEM;

	ar_CacheBlks = dmc->sp_cache_blk;
	
	for(BlkIndex=0;BlkIndex<dmc->size;BlkIndex++)
	{
		ar_CacheBlks[BlkIndex].lru_prev = EIO_LRU_NULL;
		ar_CacheBlks[BlkIndex].lru_next = EIO_LRU_NULL;	
	}

	return 0;
}


/*
 * Cleanup an instance of eio_policy (called from dtr).
 */
void eio_lru_exit(void)
{
	module_put(THIS_MODULE);
}

//notify to the policy a cache block hit at index [index]
void EIOPolicyLRU_NotifyHit(struct eio_policy *pOps, index_t Index)
{
	struct cache_c* pDmc = pOps->sp_dmc;

	index_t SetIndex 		= Index / pDmc->assoc; 		//the set index in the set array
	index_t SetAbsoluteStartIndex 	= SetIndex * pDmc->assoc;			//the set start index in the blk array
	index_t BlkRelativeIndex 	= Index - SetAbsoluteStartIndex;	//the blk relative index (from the set index) in the blk array

	struct eio_lru_cache_set* 	ar_CacheSets		= (struct eio_lru_cache_set *)pDmc->sp_cache_set;	

	if(likely(BlkRelativeIndex != ar_CacheSets[SetIndex].lru_tail))
	{
		EIOPolicyLRU_NotifyDelete(pOps, Index); //removing blk from lru
		EIOPolicyLRU_NotifyNew(pOps,Index);	//ading the blk to lru (at tail)
	}

	return;
}

//notify the addition of a new valid cache block at [index]
// LRU case: we push the new bock at tail		
void EIOPolicyLRU_NotifyNew(struct eio_policy *pOps, index_t Index)
{
	struct cache_c* pDmc = pOps->sp_dmc;

	index_t SetIndex 		= Index / pDmc->assoc; 		//the set index in the set array
	index_t SetAbsoluteStartIndex 	= SetIndex * pDmc->assoc;			//the set start index in the blk array
	index_t BlkRelativeIndex 	= Index - SetAbsoluteStartIndex;	//the blk relative index (from the set index) in the blk array

	struct eio_lru_cache_set* 	ar_CacheSets		= (struct eio_lru_cache_set *)pDmc->sp_cache_set;	
	struct eio_lru_cache_block* 	ar_CacheBlks		= (struct eio_lru_cache_block *)pDmc->sp_cache_blk;
	struct eio_lru_cache_block* 	pCurrentCacheBlk 	= &ar_CacheBlks[Index];

	pCurrentCacheBlk->lru_next = EIO_LRU_NULL;
	if (likely((ar_CacheSets[SetIndex].lru_tail != EIO_LRU_NULL)&&(ar_CacheSets[SetIndex].lru_head != EIO_LRU_NULL))) //LRU was not empty
	{
		//Updating previous tail block index to follow current block index
		ar_CacheBlks[ar_CacheSets[SetIndex].lru_tail + SetAbsoluteStartIndex].lru_next =(u_int16_t)BlkRelativeIndex;
		pCurrentCacheBlk->lru_prev = ar_CacheSets[SetIndex].lru_tail ;
	}
	else //LRU was empty
	{
		//Updating head block index to follow current block index
		ar_CacheSets[SetIndex].lru_head =(u_int16_t)BlkRelativeIndex;
		pCurrentCacheBlk->lru_prev = EIO_LRU_NULL;
	}
	ar_CacheSets[SetIndex].lru_tail = (u_int16_t)BlkRelativeIndex;

	return;
}
	
//notify the removal of a cache block at [index]		
void EIOPolicyLRU_NotifyDelete(struct eio_policy *pOps, index_t Index)
{
	struct cache_c* pDmc = pOps->sp_dmc;

	struct eio_lru_cache_set* 	ar_CacheSets		= (struct eio_lru_cache_set *)pDmc->sp_cache_set;	
	struct eio_lru_cache_block* 	ar_CacheBlks		= (struct eio_lru_cache_block *)pDmc->sp_cache_blk;
	struct eio_lru_cache_block* 	pCurrentCacheBlk 	= &ar_CacheBlks[Index];

	index_t SetIndex 		= Index / pDmc->assoc; 		//the set index in the set array
	index_t SetAbsoluteStartIndex 	= SetIndex * pDmc->assoc;			//the set start index in the blk array


	/* Removing cache block from LRU linked list */
	if 	(likely	(	(pCurrentCacheBlk->lru_prev != EIO_LRU_NULL) || //cache blk is not the only one
				(pCurrentCacheBlk->lru_next != EIO_LRU_NULL)
			)
		) 
	{
		if (likely(pCurrentCacheBlk->lru_prev == EIO_LRU_NULL)) //there is usualy no other cache block before  because we are supposed to delete HEAD cache block
		{
			ar_CacheSets[SetIndex].lru_head = pCurrentCacheBlk->lru_next;
		}
		else
		{
			ar_CacheBlks[pCurrentCacheBlk->lru_prev + SetAbsoluteStartIndex].lru_next = pCurrentCacheBlk->lru_next;
		}

		if (likely(pCurrentCacheBlk->lru_next != EIO_LRU_NULL)) //we usually Delete the HEAD cache block, and LRU usualy contain others cache blocks
		{
			ar_CacheBlks[pCurrentCacheBlk->lru_next + SetAbsoluteStartIndex].lru_prev = pCurrentCacheBlk->lru_prev;
		}
		else
		{
			ar_CacheSets[SetIndex].lru_tail = pCurrentCacheBlk->lru_prev;
		}
	}
	else
	{
		ar_CacheSets[SetIndex].lru_tail = EIO_LRU_NULL;
		ar_CacheSets[SetIndex].lru_head = EIO_LRU_NULL;
	}

	pCurrentCacheBlk->lru_prev = EIO_LRU_NULL;
	pCurrentCacheBlk->lru_next = EIO_LRU_NULL;

	return;
}

//ask the policy to find a victim block in set [set] to cache a new one, policy return the index of the cache block to evict [index] (but do not actually evict it !)		
void EIOPolicyLRU_FindVictim(struct eio_policy *pOps, index_t Set, index_t *pIndex)
{
	struct cache_c* pDmc = pOps->sp_dmc;

	struct eio_lru_cache_set* 	ar_CacheSets		= (struct eio_lru_cache_set *)pDmc->sp_cache_set;	
	struct eio_lru_cache_block* 	ar_CacheBlks		= (struct eio_lru_cache_block *)pDmc->sp_cache_blk;
	struct eio_lru_cache_block* 	pCurrentCacheBlk 	= NULL;

	index_t BlkRelativeIndex;					//the blk relative index (from the set index) in the blk array
	index_t SetAbsoluteStartIndex 	= Set * pDmc->assoc;	//the set start index in the blk array

	//iterating LRU BLK 
	BlkRelativeIndex = ar_CacheSets[Set].lru_head; //starting by head

	while (BlkRelativeIndex != EIO_LRU_NULL) 
	{		
		pCurrentCacheBlk = &ar_CacheBlks[BlkRelativeIndex+SetAbsoluteStartIndex];
		//Checking if (still) valid
		if (likely(EIO_CACHE_STATE_GET(pOps->sp_dmc, (BlkRelativeIndex+SetAbsoluteStartIndex)) == VALID)) //check that BLK is valid and that there is no pending operation
		{			
			*pIndex = BlkRelativeIndex+SetAbsoluteStartIndex; //giving blk to the caller index
			break;
		}
		//If not valid, going to next one
		BlkRelativeIndex = pCurrentCacheBlk->lru_next;
	}

	return;
}

//ask the policy to flush a whole set	
void EIOPolicyLRU_FlushSet(struct eio_policy *pOps, index_t Set, u_int32_t u32NbMaxBlockToFlush, u_int32_t* pu32NbFlushedBlock)
{
	struct cache_c* pDmc = pOps->sp_dmc;

	struct eio_lru_cache_set* 	ar_CacheSets		= (struct eio_lru_cache_set *)pDmc->sp_cache_set;	
	struct eio_lru_cache_block* 	ar_CacheBlks		= (struct eio_lru_cache_block *)pDmc->sp_cache_blk;
	struct eio_lru_cache_block* 	pCurrentCacheBlk 	= NULL;

	index_t BlkRelativeIndex;					//the blk relative index (from the set index) in the blk array
	index_t SetAbsoluteStartIndex 	= Set * pDmc->assoc;	//the set start index in the blk array

	(*pu32NbFlushedBlock) = 0;

	//iterating LRU BLK 
	BlkRelativeIndex = ar_CacheSets[Set].lru_head;

	while ((BlkRelativeIndex != EIO_LRU_NULL) && ((*pu32NbFlushedBlock) < u32NbMaxBlockToFlush)) 
	{		
		pCurrentCacheBlk = &ar_CacheBlks[BlkRelativeIndex+SetAbsoluteStartIndex];
		//Checking if cache block contain dirty data not in prog.
		if (likely(EIO_CACHE_STATE_GET(pOps->sp_dmc, (BlkRelativeIndex+SetAbsoluteStartIndex)) & (DIRTY | BLOCK_IO_INPROG)) == DIRTY) 
		{		
			// is so, seting a flag so that the cache system will flush it to the HDD
			EIO_CACHE_STATE_ON(pOps->sp_dmc, (BlkRelativeIndex+SetAbsoluteStartIndex), DISKWRITEINPROG);
			(*pu32NbFlushedBlock)++;
		}
		//If not valid, going to next one
		BlkRelativeIndex = pCurrentCacheBlk->lru_next;
	}

	return;
}

static
int __init lru_register(void)
{
	int ret;

	ret = eio_policy_register(&eio_lru_ops);
	if (ret != 0)
		pr_info("eio_lru already registered");

	return ret;
}

static
void __exit lru_unregister(void)
{
	int ret;

	ret = eio_policy_unregister(&eio_lru_ops);
	if (ret != 0)
		pr_err("eio_lru unregister failed");
}

module_init(lru_register);
module_exit(lru_unregister);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LRU policy for EnhanceIO");
MODULE_AUTHOR("STEC, Inc. based on code by Facebook");
