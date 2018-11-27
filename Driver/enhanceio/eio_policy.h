/*
 *  eio_policy.h
 *
 *  Copyright (C) 2012 STEC, Inc. All rights not specifically granted
 *   under a license included herein are reserved
 *  Made EnhanceIO specific changes.
 *   Saied Kazemi <skazemi@stec-inc.com>
 *   Siddharth Choudhuri <schoudhuri@stec-inc.com>
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

#ifndef EIO_POLICY_H
#define EIO_POLICY_H

#include <linux/module.h>
#include <linux/list.h>

/*
 * Defines for policy types (EIO_REPL_XXX are in eio.h
 * so that user space utilties can use those definitions.
 */

/*
 * The LRU pointers are maintained as set-relative offsets, instead of
 * pointers. This enables us to store the LRU pointers per cacheblock
 * using 4 bytes instead of 16 bytes. The upshot of this is that we
 * are required to clamp the associativity at an 8K max.
 *
 * XXX - The above comment is from the original code. Looks like an error,
 * maximum associativity should be 32K (2^15) and not 8K.
 */
#define EIO_MAX_ASSOC   32768 	//TODO: check, was 8192

/* Declerations to keep the compiler happy */
struct cache_c;
struct eio_policy;

/*
 * Context that captures the cache block replacement policy.
 * There is one instance of this struct per dmc (cache)
 */
struct eio_policy {
	int 	sp_name;
	int 	(*sp_repl_init)		(struct cache_c *);
	void 	(*sp_repl_exit)		(void);
	int 	(*sp_repl_sets_init)	(struct eio_policy *);
	int 	(*sp_repl_blk_init)	(struct eio_policy *);
	//TODO: add policy context backup management
/*
The following set of command is the interface between the cache manager and the policies.
Policies must not operate direclty on cache data nor cache sets but should receive notification to update their internal
context data and then make the decisions of wich block to evict.
*/
	//notify to the policy a cache block hit at index [index]
	void 	(*pfNotifyHit)		(struct eio_policy *pOps, index_t Index);	
	//notify to the policy a cache block miss at sector [dbn]
	void 	(*pfNotifyMiss)		(struct eio_policy *pOps, sector_t Dbn );
	//notify the addition of a new valid cache block at [index]		
	void 	(*pfNotifyNew)		(struct eio_policy *pOps, index_t Index);	
	//notify the removal of a cache block at [index]		
	void 	(*pfNotifyDelete)	(struct eio_policy *pOps, index_t Index);
	//ask the policy to find a victim block in set [set] to cache a new one, policy return the index of the cache block to evict [index] (but do not actually evict it !)		
	void 	(*pfFindVictim)		(struct eio_policy *pOps, index_t Set, index_t *pIndex);
	//ask the policy to flush a whole set	
	void 	(*pfFlushSet)		(struct eio_policy *pOps, index_t Set, u_int32_t u32NbMaxBlockToFlush,u_int32_t* pu32NbFlushedBlock);

	struct cache_c *sp_dmc;
};

/*
 * List of registered policies. There is one instance
 * of this structure per policy type.
 */
struct eio_policy_header {
	int sph_name;
	struct eio_policy *(*sph_instance_init)(void);
	struct list_head sph_list;
};

/* Prototypes of generic functions in eio_policy */
int *eio_policy_repl_init(struct cache_c *);
int eio_policy_repl_sets_init(struct eio_policy *);
int eio_policy_repl_blk_init(struct eio_policy *);

//notify to the policy a cache block hit at index [index]
void EIOPolicy_NotifyHit(struct eio_policy *pOps, index_t Index);	
//notify to the policy a cache block miss at sector [dbn]
void EIOPolicy_NotifyMiss(struct eio_policy *pOps, sector_t Dbn );
//notify the addition of a new valid cache block at [index]		
void EIOPolicy_NotifyNew(struct eio_policy *pOps, index_t Index);	
//notify the removal of a cache block at [index]		
void EIOPolicy_NotifyDelete(struct eio_policy *pOps, index_t Index);
//ask the policy to find a victim block in set [set] to cache a new one, policy return the index of the cache block to evict [index] (but do not actually evict it !)		
void EIOPolicy_FindVictim(struct eio_policy *pOps, index_t Set, index_t *pIndex);
//ask the policy to flush a whole set	
void EIOPolicy_FlushSet(struct eio_policy *pOps, index_t Set, u_int32_t u32NbMaxBlockToFlush, u_int32_t* pu32NbFlushedBlock);

int eio_policy_register(struct eio_policy_header *);
int eio_policy_unregister(struct eio_policy_header *);
struct eio_policy *eio_get_policy(int);
void eio_put_policy(struct eio_policy *);

#endif                          /* EIO_POLICY_H */
