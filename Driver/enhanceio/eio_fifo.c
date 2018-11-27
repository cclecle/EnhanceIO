/*
 *  eio_fifo.c
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

#define EIO_FIFO_NULL    0xFFFF

/* Generic policy functions prototypes */
int eio_fifo_init(struct cache_c *);
void eio_fifo_exit(void);
int eio_fifo_cache_sets_init(struct eio_policy *);
int eio_fifo_cache_blk_init(struct eio_policy *);

void EIOPolicyFIFO_NotifyNew(struct eio_policy *pOps, index_t Index);	
//notify the removal of a cache block at [index]		
void EIOPolicyFIFO_NotifyDelete(struct eio_policy *pOps, index_t Index);
//ask the policy to find a victim block in set [set] to cache a new one, policy return the index of the cache block to evict [index] (but do not actually evict it !)		
void EIOPolicyFIFO_FindVictim(struct eio_policy *pOps, index_t Set, index_t *pIndex);
//ask the policy to flush a whole set	
void EIOPolicyFIFO_FlushSet(struct eio_policy *pOps, index_t Set, u_int32_t u32NbMaxBlockToFlush, u_int32_t* pu32NbFlushedBlock);


/* Per policy instance initialization */
struct eio_policy *eio_fifo_instance_init(void);

/* Per cache set data structure */
struct eio_fifo_cache_set {
	index_t fifo_head;
	index_t fifo_tail;
};

/* Per cache block data structure */
struct eio_fifo_cache_block {
	u_int16_t fifo_next;
};
/*
 * Context that captures the FIFO replacement policy
 */
static struct eio_policy_header eio_fifo_ops = {
	.sph_name		= CACHE_REPL_FIFO,
	.sph_instance_init	= eio_fifo_instance_init,
};

/*
 * Initialize FIFO policy.
 */
int eio_fifo_init(struct cache_c *dmc)
{
	return 0;
}


/*
 * Allocate a new instance of eio_policy per dmc
 */
struct eio_policy *eio_fifo_instance_init(void)
{
	struct eio_policy *new_instance;

	new_instance = vmalloc(sizeof(struct eio_policy));
	if (new_instance == NULL) {
		pr_err("ssdscache_fifo_instance_init: vmalloc failed");
		return NULL;
	}

	/* Initialize the FIFO specific functions and variables */
	new_instance->sp_name 		= CACHE_REPL_FIFO;
	new_instance->sp_repl_init 	= eio_fifo_init;
	new_instance->sp_repl_exit 	= eio_fifo_exit;
	new_instance->sp_repl_sets_init	= eio_fifo_cache_sets_init;
	new_instance->sp_repl_blk_init 	= eio_fifo_cache_blk_init;

	new_instance->pfNotifyHit	= NULL;
	new_instance->pfNotifyMiss	= NULL;	
	new_instance->pfNotifyNew	= EIOPolicyFIFO_NotifyNew;
	new_instance->pfNotifyDelete	= EIOPolicyFIFO_NotifyDelete;
	new_instance->pfFindVictim	= EIOPolicyFIFO_FindVictim;
	new_instance->pfFlushSet	= EIOPolicyFIFO_FlushSet;

	try_module_get(THIS_MODULE);

	pr_info("eio_fifo_instance_init: created new instance of FIFO");

	return new_instance;
}

/*
 * Initialize FIFO data structure called from ctr.
 */
int eio_fifo_cache_sets_init(struct eio_policy *p_ops)
{
	int i;
	sector_t order;
	struct cache_c *dmc = p_ops->sp_dmc;
	struct eio_fifo_cache_set *cache_sets;

	pr_info("Initializing fifo cache sets\n");
	order = (dmc->size >> dmc->consecutive_shift) *
		sizeof(struct eio_fifo_cache_set);

	dmc->sp_cache_set = vmalloc((size_t)order);
	if (dmc->sp_cache_set == NULL)
		return -ENOMEM;

	cache_sets = (struct eio_fifo_cache_set *)dmc->sp_cache_set;

	for (i = 0; i < (int)(dmc->size >> dmc->consecutive_shift); i++) {
		cache_sets[i].fifo_head = EIO_FIFO_NULL;
		cache_sets[i].fifo_tail = EIO_FIFO_NULL;
	}

	return 0;
}

/*
 * Initialize per block FIFO data structures
 */
int eio_fifo_cache_blk_init(struct eio_policy *p_ops)
{
	sector_t order;
	index_t BlkIndex; 
	struct cache_c *dmc = p_ops->sp_dmc;
	struct eio_fifo_cache_block* 	ar_CacheBlks = NULL;

	order = dmc->size * sizeof(struct eio_fifo_cache_block);

	dmc->sp_cache_blk = vmalloc((size_t)order);
	if (dmc->sp_cache_blk == NULL)
		return -ENOMEM;

	ar_CacheBlks = dmc->sp_cache_blk;
	
	for(BlkIndex=0;BlkIndex<dmc->size;BlkIndex++)
	{
		ar_CacheBlks[BlkIndex].fifo_next = EIO_FIFO_NULL;
	}

	return 0;
}

void EIOPolicyFIFO_NotifyNew(struct eio_policy *pOps, index_t Index)
{
	struct cache_c* pDmc = pOps->sp_dmc;

	index_t SetIndex 		= Index / pDmc->assoc; 		//the set index in the set array
	index_t SetAbsoluteStartIndex 	= SetIndex * pDmc->assoc;			//the set start index in the blk array
	index_t BlkRelativeIndex 	= Index - SetAbsoluteStartIndex;	//the blk relative index (from the set index) in the blk array

	struct eio_fifo_cache_set* 	ar_CacheSets		= (struct eio_fifo_cache_set *)pDmc->sp_cache_set;	
	struct eio_fifo_cache_block* 	ar_CacheBlks		= (struct eio_fifo_cache_block *)pDmc->sp_cache_blk;
	struct eio_fifo_cache_block* 	pCurrentCacheBlk 	= &ar_CacheBlks[Index];

	pCurrentCacheBlk->fifo_next = EIO_FIFO_NULL;



	if (likely((ar_CacheSets[SetIndex].fifo_head != EIO_FIFO_NULL)&&(ar_CacheSets[SetIndex].fifo_tail != EIO_FIFO_NULL))) //FIFO set is usually not empty
	{
		//Updating previous tail block index to follow current block index
		ar_CacheBlks[ar_CacheSets[SetIndex].fifo_head + SetAbsoluteStartIndex].fifo_next =(u_int16_t)BlkRelativeIndex;
	}
	else //FIFO was empty
	{
		//Updating head block index to follow current block index
		ar_CacheSets[SetIndex].fifo_head =(u_int16_t)BlkRelativeIndex;
	}
	ar_CacheSets[SetIndex].fifo_tail = (u_int16_t)BlkRelativeIndex;

	return;
}
	
//notify the removal of a cache block at [index]		
void EIOPolicyFIFO_NotifyDelete(struct eio_policy *pOps, index_t Index)
{
	struct cache_c* pDmc = pOps->sp_dmc;

	struct eio_fifo_cache_set* 	ar_CacheSets		= (struct eio_fifo_cache_set *)pDmc->sp_cache_set;	
	struct eio_fifo_cache_block* 	ar_CacheBlks		= (struct eio_fifo_cache_block *)pDmc->sp_cache_blk;
	struct eio_fifo_cache_block* 	pCurrentCacheBlk 	= &ar_CacheBlks[Index];

	index_t SetIndex 		= Index / pDmc->assoc; 		//the set index in the set array
	index_t SetAbsoluteStartIndex 	= SetIndex * pDmc->assoc;	//the set start index in the blk array

	index_t BlkRelativeIndex 	= Index - SetAbsoluteStartIndex;	//the blk relative index (from the set index) in the blk array	
	index_t BlkRelativeIndex_Iter;

	/* Removing cache block from FIFO linked list */
	if	(likely(ar_CacheSets[SetIndex].fifo_head  == BlkRelativeIndex)) //we usually Delete the head cache block
			
		
	{
		ar_CacheSets[SetIndex].fifo_head = pCurrentCacheBlk->fifo_next;
	}
	else // looking for the previous cache block
	{		
		//Note:	This algorithm is slow but necessary because we use a single linked list
		//	But this case is usually not supposed to append, top level function is supposed to delete the HEAD cache block.
		//	Exept if there is an OPERATION on the HEAD cache block.

		// starting iterating by fifo HEAD
		BlkRelativeIndex_Iter = ar_CacheSets[SetIndex].fifo_head ;
		while( likely(	
				(ar_CacheBlks[SetAbsoluteStartIndex + BlkRelativeIndex_Iter].fifo_next != BlkRelativeIndex) && 
				(ar_CacheBlks[SetAbsoluteStartIndex + BlkRelativeIndex_Iter].fifo_next != EIO_FIFO_NULL)
			     ) 
		     )
		{
			BlkRelativeIndex_Iter = ar_CacheBlks[SetAbsoluteStartIndex + BlkRelativeIndex_Iter].fifo_next;
		}

		if(likely(ar_CacheBlks[SetAbsoluteStartIndex + BlkRelativeIndex_Iter].fifo_next != EIO_FIFO_NULL)) //We found the previous block
		{
			//updating its next index
			ar_CacheBlks[SetAbsoluteStartIndex + BlkRelativeIndex_Iter].fifo_next = pCurrentCacheBlk->fifo_next;
		}
		else
		{
			//Very unusual case, should not append
			//Assumming the current cache block not really tracked but point to another tracked blk
			//So we do nothing, the ->fifo_next index will be null-ed at the end of the functions
			//and then the blk will be untracked
		}
		 
	}

	pCurrentCacheBlk->fifo_next = EIO_FIFO_NULL;
	
	if(unlikely(ar_CacheSets[SetIndex].fifo_head==EIO_FIFO_NULL)) //if it was the only cache block in the fifo, fifo is now empty
	{
		//updating the TAIL index
		ar_CacheSets[SetIndex].fifo_tail = EIO_FIFO_NULL;
	}

	return;
}

//ask the policy to find a victim block in set [set] to cache a new one, policy return the index of the cache block to evict [index] (but do not actually evict it !)		
void EIOPolicyFIFO_FindVictim(struct eio_policy *pOps, index_t Set, index_t *pIndex)
{
	struct cache_c* pDmc = pOps->sp_dmc;

	struct eio_fifo_cache_set* 	ar_CacheSets		= (struct eio_fifo_cache_set *)pDmc->sp_cache_set;	
	struct eio_fifo_cache_block* 	ar_CacheBlks		= (struct eio_fifo_cache_block *)pDmc->sp_cache_blk;
	struct eio_fifo_cache_block* 	pCurrentCacheBlk 	= NULL;

	index_t BlkRelativeIndex;					//the blk relative index (from the set index) in the blk array
	index_t SetAbsoluteStartIndex 	= Set * pDmc->assoc;	//the set start index in the blk array

	//iterating FIFO BLK 
	BlkRelativeIndex = ar_CacheSets[Set].fifo_head; //starting by head

	while (BlkRelativeIndex != EIO_FIFO_NULL) 
	{		
		pCurrentCacheBlk = &ar_CacheBlks[BlkRelativeIndex+SetAbsoluteStartIndex];
		//Checking if (still) valid
		if (likely(EIO_CACHE_STATE_GET(pOps->sp_dmc, (BlkRelativeIndex+SetAbsoluteStartIndex)) == VALID)) //check that BLK is valid and that there is no pending operation
		{			
			*pIndex = BlkRelativeIndex+SetAbsoluteStartIndex; //giving blk to the caller index
			break;
		}
		//If not valid, going to next one
		BlkRelativeIndex = pCurrentCacheBlk->fifo_next;
	}

	return;
}

//ask the policy to flush a whole set	
void EIOPolicyFIFO_FlushSet(struct eio_policy *pOps, index_t Set, u_int32_t u32NbMaxBlockToFlush, u_int32_t* pu32NbFlushedBlock)
{
	struct cache_c* pDmc = pOps->sp_dmc;

	struct eio_fifo_cache_set* 	ar_CacheSets		= (struct eio_fifo_cache_set *)pDmc->sp_cache_set;	
	struct eio_fifo_cache_block* 	ar_CacheBlks		= (struct eio_fifo_cache_block *)pDmc->sp_cache_blk;
	struct eio_fifo_cache_block* 	pCurrentCacheBlk 	= NULL;

	index_t BlkRelativeIndex;					//the blk relative index (from the set index) in the blk array
	index_t SetAbsoluteStartIndex 	= Set * pDmc->assoc;	//the set start index in the blk array

	(*pu32NbFlushedBlock) = 0;

	//iterating FIFO BLK 
	BlkRelativeIndex = ar_CacheSets[Set].fifo_head; 

	while ((BlkRelativeIndex != EIO_FIFO_NULL) && ((*pu32NbFlushedBlock) < u32NbMaxBlockToFlush)) 
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
		BlkRelativeIndex = pCurrentCacheBlk->fifo_next;
	}

	return;
}





/*
 * Cleanup an instance of eio_policy (called from dtr).
 */
void eio_fifo_exit(void)
{
	module_put(THIS_MODULE);
}

static
int __init fifo_register(void)
{
	int ret;

	ret = eio_policy_register(&eio_fifo_ops);
	if (ret != 0)
		pr_info("eio_fifo already registered");

	return ret;
}

static
void __exit fifo_unregister(void)
{
	int ret;

	ret = eio_policy_unregister(&eio_fifo_ops);
	if (ret != 0)
		pr_err("eio_fifo unregister failed");
}

module_init(fifo_register);
module_exit(fifo_unregister);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FIFO policy for EnhanceIO");
MODULE_AUTHOR("STEC, Inc. based on code by Facebook");
