/*
 *  eio_policy.c
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

#include "eio.h"

LIST_HEAD(eio_policy_list);

//registering a new polcy ina the policy list
int eio_policy_register(struct eio_policy_header *new_policy)
{
	struct list_head *ptr;
	struct eio_policy_header *curr;

	//walking the (already) registered policy list
	list_for_each(ptr, &eio_policy_list) 
	{
		//getting the current index policy in the list
		curr = list_entry(ptr, struct eio_policy_header, sph_list);
		//checking if it is the one we are trying to register
		if (curr->sph_name == new_policy->sph_name)
		{
			// policy already registered , returning error(1)
			return 1;
		}
	}
	//policy is not registered, adding it to the list
	list_add_tail(&new_policy->sph_list, &eio_policy_list);

	pr_info("register_policy: policy %d added", new_policy->sph_name);

	return 0;
}
EXPORT_SYMBOL(eio_policy_register);

//unregistering a polcy from the policy list
int eio_policy_unregister(struct eio_policy_header *p_ops)
{
	struct list_head *ptr;
	struct eio_policy_header *curr;

	//walking the (already) registered policy list
	list_for_each(ptr, &eio_policy_list) 
	{
		//getting the current index policy in the list
		curr = list_entry(ptr, struct eio_policy_header, sph_list);
		//checking if it is the one we are trying to unregister
		if (curr->sph_name == p_ops->sph_name) 
		{
			// unregistering the policy
			list_del(&curr->sph_list);
			pr_info("unregister_policy: policy %d removed",
				(int)p_ops->sph_name);
			return 0;
		}
	}
	//policy was not found, returning error(1)
	return 1;
}
EXPORT_SYMBOL(eio_policy_unregister);

//initializing a policy and getting a descriptor, from ident
struct eio_policy *eio_get_policy(int policy)
{
	struct list_head *ptr;
	struct eio_policy_header *curr;

	//walking the registered policy list
	list_for_each(ptr, &eio_policy_list) 
	{
		//getting the current index policy in the list
		curr = list_entry(ptr, struct eio_policy_header, sph_list);
		//checking if it is the one we are trying to get
		if (curr->sph_name == policy) 
		{
			//policy found, initilializing it and returning descriptor to caller
			pr_info("get_policy: policy %d found", policy);
			return curr->sph_instance_init();
		}
	}
	pr_info("get_policy: cannot find policy %d", policy);
	//policy not found, returning error(NULL)
	return NULL;
}

/*
 * Decrement the reference count of the policy specific module
 * and any other cleanup that is required when an instance of a
 * policy is no longer required.
 */
void eio_put_policy(struct eio_policy *p_ops)
{

	if (p_ops == NULL) 
	{
		pr_err("put_policy: Cannot decrement reference"	\
		       "count of NULL policy");
		return;
	}
	p_ops->sp_repl_exit();
}

/*
 * Wrappers for policy specific functions. These default to nothing if the
 * default policy is being used.
 */

int eio_policy_repl_sets_init(struct eio_policy *p_ops)
{
	return (p_ops && p_ops->sp_repl_sets_init) ? p_ops->sp_repl_sets_init(p_ops) : 0;
}

int eio_policy_repl_blk_init(struct eio_policy *p_ops)
{
	return (p_ops && p_ops->sp_repl_blk_init) ? p_ops->sp_repl_blk_init(p_ops) : 0;
}


//notify to the policy a cache block hit at index [index]
void EIOPolicy_NotifyHit(struct eio_policy *pOps, index_t Index)
{
	return (pOps && pOps->pfNotifyHit) ? pOps->pfNotifyHit(pOps,Index) : 0;
}
	
//notify to the policy a cache block miss at sector [dbn]
void EIOPolicy_NotifyMiss(struct eio_policy *pOps, sector_t Dbn )
{
	return (pOps && pOps->pfNotifyMiss) ? pOps->pfNotifyMiss(pOps,Dbn) : 0;
}

//notify the addition of a new valid cache block at [index]		
void EIOPolicy_NotifyNew(struct eio_policy *pOps, index_t Index)
{
	return (pOps && pOps->pfNotifyNew) ? pOps->pfNotifyNew(pOps,Index) : 0;
}
	
//notify the removal of a cache block at [index]		
void EIOPolicy_NotifyDelete(struct eio_policy *pOps, index_t Index)
{
	return (pOps && pOps->pfNotifyDelete) ? pOps->pfNotifyDelete(pOps,Index) : 0;
}

//ask the policy to find a victim block in set [set] to cache a new one, policy return the index of the cache block to evict [index] (but do not actually evict it !)		
void EIOPolicy_FindVictim(struct eio_policy *pOps, index_t Set, index_t *pIndex)
{
	return (pOps && pOps->pfFindVictim) ? pOps->pfFindVictim(pOps,Set,pIndex) : 0;
}

//ask the policy to flush a whole set	
void EIOPolicy_FlushSet(struct eio_policy *pOps, index_t Set, u_int32_t u32NbMaxBlockToFlush, u_int32_t* pu32NbFlushedBlock)
{
	return (pOps && pOps->pfFlushSet) ? pOps->pfFlushSet(pOps,Set,u32NbMaxBlockToFlush,pu32NbFlushedBlock) : 0;
}
