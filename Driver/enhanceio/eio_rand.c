/*
 *  eio_rand.c
 *
 *  Copyright (C) 2013 ProfitBricks, GmbH.
 *   Jack Wang <jinpu.wang@profitbricks.com>
 *   Dongsu Park <dongsu.park@profitbricks.com>
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
/* Generic policy functions prototypes */
int eio_rand_init(struct cache_c *);
void eio_rand_exit(void);
int eio_rand_cache_sets_init(struct eio_policy *);
int eio_rand_cache_blk_init(struct eio_policy *);
void EIOPolicyRAND_FindVictim(struct eio_policy *pOps, index_t Set, index_t *pIndex);
void EIOPolicyRAND_FlushSet(struct eio_policy *pOps, index_t Set, u_int32_t u32NbMaxBlockToFlush, u_int32_t* pu32NbFlushedBlock);

/* Per policy instance initialization */
struct eio_policy *eio_rand_instance_init(void);


/*
 * Context that captures the rand replacement policy
 */
static struct eio_policy_header eio_rand_ops = {
	.sph_name		= CACHE_REPL_RANDOM,
	.sph_instance_init	= eio_rand_instance_init,
};

/*
 * Initialize RAND policy.
 */
int eio_rand_init(struct cache_c *dmc)
{
	return 0;
}

/*
 * Initialize rand data structure called from ctr.
 */
int eio_rand_cache_sets_init(struct eio_policy *p_ops)
{
	return 0;
}

//ask the policy to find a victim block in set [set] to cache a new one, policy return the index of the cache block to evict [index] (but do not actually evict it !)		
void EIOPolicyRAND_FindVictim(struct eio_policy *pOps, index_t Set, index_t *pIndex)
{
	int i = 0;
	index_t idx;
	struct cache_c* pDmc = pOps->sp_dmc;
	index_t start_index ;

	start_index= Set * pDmc->assoc;

	/*
	 * "start_index" should already be the beginning index of the set.
	 * We're just being cautious here.
	 */
	start_index = (start_index / pDmc->assoc) * pDmc->assoc;
	for (i = 0; i < (int)pDmc->assoc; i++) {
		idx = pDmc->random++ % pDmc->assoc;
		if (EIO_CACHE_STATE_GET(pDmc, start_index + idx) == VALID) {
			(*pIndex) = start_index + idx;
			return;
		}
	}

	return;
}

//ask the policy to flush a whole set	
void EIOPolicyRAND_FlushSet(struct eio_policy *pOps, index_t Set, u_int32_t u32NbMaxBlockToFlush, u_int32_t* pu32NbFlushedBlock)
{
	int i = 0;
	struct cache_c* pDmc = pOps->sp_dmc;
	index_t start_index ;

	start_index= Set * pDmc->assoc;
	(*pu32NbFlushedBlock) = 0;

	/* Scan sequentially in the set and pick blocks to clean */
	while ((i < (int)pDmc->assoc) && ((*pu32NbFlushedBlock)  < u32NbMaxBlockToFlush)) {
		if ((EIO_CACHE_STATE_GET(pDmc, start_index + i) &
		     (DIRTY | BLOCK_IO_INPROG)) == DIRTY) {
			EIO_CACHE_STATE_ON(pDmc, start_index + i,
					   DISKWRITEINPROG);
			(*pu32NbFlushedBlock)++;
		}
		i++;
	}
	return;
}


/*
 * rand is per set, so do nothing on a per block init.
 */
int eio_rand_cache_blk_init(struct eio_policy *p_ops)
{
	return 0;
}

/*
 * Allocate a new instance of eio_policy per dmc
 */
struct eio_policy *eio_rand_instance_init(void)
{
	struct eio_policy *new_instance;

	new_instance = vmalloc(sizeof(struct eio_policy));
	if (new_instance == NULL) {
		pr_err("ssdscache_rand_instance_init: vmalloc failed");
		return NULL;
	}

	/* Initialize the rand specific functions and variables */
	new_instance->sp_name = CACHE_REPL_RANDOM;
	new_instance->sp_repl_init = eio_rand_init;
	new_instance->sp_repl_exit = eio_rand_exit;
	new_instance->sp_repl_sets_init = eio_rand_cache_sets_init;
	new_instance->sp_repl_blk_init = eio_rand_cache_blk_init;


	new_instance->pfNotifyHit		= NULL;
	new_instance->pfNotifyMiss		= NULL;	
	new_instance->pfNotifyNew		= NULL;
	new_instance->pfNotifyDelete		= NULL;
	new_instance->pfFindVictim		= EIOPolicyRAND_FindVictim;
	new_instance->pfFlushSet		= EIOPolicyRAND_FlushSet;

	new_instance->sp_dmc = NULL;

	try_module_get(THIS_MODULE);

	pr_info("eio_rand_instance_init: created new instance of RAND");

	return new_instance;
}

/*
 * Cleanup an instance of eio_policy (called from dtr).
 */
void eio_rand_exit(void)
{
	module_put(THIS_MODULE);
}

static
int __init rand_register(void)
{
	int ret;

	ret = eio_policy_register(&eio_rand_ops);
	if (ret != 0)
		pr_info("eio_rand already registered");

	return ret;
}

static
void __exit rand_unregister(void)
{
	int ret;

	ret = eio_policy_unregister(&eio_rand_ops);
	if (ret != 0)
		pr_err("eio_rand unregister failed");
}

module_init(rand_register);
module_exit(rand_unregister);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAND policy for EnhanceIO");
MODULE_AUTHOR("STEC, Inc. based on code by Facebook");
