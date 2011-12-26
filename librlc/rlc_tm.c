/**
 * Copyright (c) 2011-2012 Phuuix Xiong <phuuix@163.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 *
 * @file
 *   RLC TM functions.
 *
 * @History
 * Phuuix Xiong, Create, 01-25-2011
 */
/*
 * rlc_tm.c: RLC TM entity code
 */
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "list.h"
#include "rlc.h"
#include "rlc_pdu.h"
#include "log.h"
#include "fastalloc.h"

/***********************************************************************************/
/* Function : rlc_tm_init                                                          */
/***********************************************************************************/
/* Description : - Init RLC TM entity                                              */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   rlc_tm             | i  | pointer of RLC TM entity                            */
/*   free_sdu           | i  | function to free sdu                                */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void rlc_tm_init(rlc_entity_tm_t *rlc_tm, void (*free_sdu)(void *, void *))
{
	memset(rlc_tm, 0, sizeof(rlc_entity_tm_t));
	rlc_tm->type = RLC_ENTITY_TYPE_TM;
//	rlc_tm->free_pdu = free_pdu;
	rlc_tm->free_sdu = free_sdu;
	
	dllist_init(&(rlc_tm->sdu_tx_q));
}

/***********************************************************************************/
/* Function : rlc_tm_reestablish                                                   */
/***********************************************************************************/
/* Description : - Re-establishment procedure                                      */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   rlctm              | o  | TM entity                                           */
/*   Return             |    | 0 is success                                        */
/***********************************************************************************/
int rlc_tm_reestablish(rlc_entity_tm_t *rlctm)
{
	rlc_sdu_t *sdu;

	if(rlctm == NULL)
		return -1;
		
/* 
-	if it is a transmitting TM RLC entity:
	-	discard all RLC SDUs;
*/
	while(!DLLIST_EMPTY(&rlctm->sdu_tx_q))
	{
		sdu = (rlc_sdu_t *)(rlctm->sdu_tx_q.next);

		dllist_remove(&rlctm->sdu_tx_q, (dllist_node_t *)sdu);
		rlc_sdu_free(sdu);
	}
	
	return 0;
}


/* a dummy function, just for compatibility */
int rlc_tm_rx_process_pdu(rlc_entity_tm_t *tmrx, u8 *buf_ptr, u32 buf_len, void *cookie)
{
	return 0;
}

/***********************************************************************************/
/* Function : rlc_tm_tx_build_pdu                                                  */
/***********************************************************************************/
/* Description : - Called by MAC to build RLC TM PDU                               */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   tmtx               | i  | RLC TM entity                                       */
/*   out_sdu            | o  | RLC PDU buffer pointer                              */
/*   pdu_size           | i  | requested PDU Size                                  */
/*   Return             |    | Size of real RLC PDU                                */
/***********************************************************************************/
int rlc_tm_tx_build_pdu(rlc_entity_tm_t *tmtx, rlc_sdu_t **out_sdu, u16 pdu_size)
{
	rlc_sdu_t *sdu;
	
	/* no data in queue */
	if(tmtx->sdu_total_size == 0)
		return 0;
		
	/* return the first SDU in tx queue */
	assert(!DLLIST_EMPTY(&tmtx->sdu_tx_q));
	sdu = (rlc_sdu_t *)tmtx->sdu_tx_q.next;
	assert(sdu->size);
	
	if(pdu_size >= sdu->size)
	{
		dllist_remove(&tmtx->sdu_tx_q, (dllist_node_t *)sdu);
		*out_sdu = sdu;
		tmtx->sdu_total_size -= sdu->size;
		tmtx->n_sdu --;
		return sdu->size;
	}
	
	/* can't re-segment sdu */
	return 0;
}

/***********************************************************************************/
/* Function : rlc_tm_tx_get_sdu_size                                               */
/***********************************************************************************/
/* Description : - Get available SDU size in tx queue (the size of first SDU)      */
/*               - Called by MAC                                                   */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   tmtx               | i  | RLC TM entity                                       */
/*   Return             |    | Size of RLC SDU including RLC header                */
/***********************************************************************************/
u32 rlc_tm_tx_get_sdu_size(rlc_entity_tm_t *tmtx)
{
	rlc_sdu_t *sdu;
	
	/* no data in queue */
	if(tmtx->sdu_total_size == 0)
		return 0;
		
	/* return the size of first SDU in tx queue */
	sdu = (rlc_sdu_t *)tmtx->sdu_tx_q.next;
	assert(sdu->size > 0);
	return sdu->size;
}

/***********************************************************************************/
/* Function : rlc_tm_tx_sdu_enqueue                                                */
/***********************************************************************************/
/* Description : - RLC SDU enqueue (only one segment)                              */
/*               - Called by PDCP, etc                                             */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   tmtx               | i  | RLC TM entity                                       */
/*   buf_ptr            | i  | RLC SDU buffer pointer                              */
/*   sdu_size           | i  | Size of SDU                                         */
/*   cookie             | i  | parameter of free function                          */
/*   Return             |    | 0 is success                                        */
/***********************************************************************************/
int rlc_tm_tx_sdu_enqueue(rlc_entity_tm_t *tmtx, u8 *buf_ptr, u32 sdu_size, void *cookie)
{
	rlc_sdu_t *sdu;
	
	if(sdu_size <= 0 || buf_ptr == NULL || tmtx == NULL)
		return -1;

	sdu = rlc_sdu_new();
	if(sdu == NULL)
	{
		ZLOG_ERR("out of memory: rlc_sdu_new().\n");
		return -1;
	}

	dllist_append(&tmtx->sdu_tx_q, (dllist_node_t *)sdu);
	
	sdu->segment[0].cookie = cookie;
	sdu->segment[0].free = tmtx->free_sdu;
	sdu->segment[0].data = buf_ptr;
	sdu->segment[0].length = sdu_size;
	sdu->size = sdu_size;
	sdu->n_segment = 1;
	sdu->intact = 1;
	tmtx->sdu_total_size += sdu_size;
	tmtx->n_sdu ++;
	
	ZLOG_DEBUG("TM SDU enqueue: logical_chan=%d buf_ptr=%p sdu_size=%u total_size=%u data=0x%02x%02x%02x%02x\n",
			tmtx->logical_chan, buf_ptr, sdu_size, tmtx->sdu_total_size, buf_ptr[0], buf_ptr[1], buf_ptr[2], buf_ptr[3]);
	
	return 0;
}
