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
 *   RLC common functions.
 *
 * @History
 * Phuuix Xiong, Create, 01-25-2011
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "log.h"
#include "rlc.h"
#include "rlc_pdu.h"
#include "ptimer.h"
#include "fastalloc.h"

/* some macros for memory allocation, change them to fulfill your requirement */
#define RLC_AM_ENTITY_MAX 10
#define RLC_UM_ENTITY_MAX 10

#define RLC_MEM_SDU_MAX (RLC_UM_ENTITY_MAX+RLC_AM_ENTITY_MAX)*1024
#define RLC_MEM_UM_PDU_MAX RLC_UM_ENTITY_MAX*1024
#define RLC_MEM_AM_PDU_SEG_MAX RLC_AM_ENTITY_MAX*128
#define RLC_MEM_AM_PDU_RX_MAX RLC_AM_ENTITY_MAX*1024
#define RLC_MEM_AM_PDU_TX_MAX RLC_AM_ENTITY_MAX*1024


fastalloc_t *g_mem_sdu_base;
fastalloc_t *g_mem_um_pdu_base;
fastalloc_t *g_mem_am_pdu_seg_base;
fastalloc_t *g_mem_am_pdu_rx_base;
fastalloc_t *g_mem_am_pdu_tx_base;


/*************** Timer APIS: a wrapper of ptimer ********************/
static ptimer_table_t rlc_timerbase;
#define RLC_TIMER_NSLOT 2048

void rlc_timer_start(ptimer_t *timer)
{
	ptimer_start(&rlc_timerbase, timer, timer->duration);
}

void rlc_timer_stop(ptimer_t *timer)
{
	ptimer_cancel(&rlc_timerbase, timer);
}

int rlc_timer_is_running(ptimer_t *timer)
{
	return ptimer_is_running(timer);
}

void rlc_timer_push(u32 time)
{
	ptimer_consume_time(&rlc_timerbase, time);
}

/***********************************************************************************/
/* Function : rlc_init                                                             */
/***********************************************************************************/
/* Description : - RLC global initialization                                       */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void rlc_init()
{
	/* init timer */
	ptimer_init(&rlc_timerbase, RLC_TIMER_NSLOT);

	/* init memory pool */
	g_mem_sdu_base = fastalloc_create(sizeof(rlc_sdu_t), RLC_MEM_SDU_MAX, 0, 1000);
	assert(g_mem_sdu_base);
	g_mem_um_pdu_base = fastalloc_create(sizeof(rlc_um_pdu_t), RLC_MEM_UM_PDU_MAX, 0, 1000);
	assert(g_mem_sdu_base);
	g_mem_am_pdu_seg_base = fastalloc_create(sizeof(rlc_am_pdu_segment_t), RLC_MEM_AM_PDU_SEG_MAX, 0, 1000);
	assert(g_mem_sdu_base);
	g_mem_am_pdu_rx_base = fastalloc_create(sizeof(rlc_am_rx_pdu_ctrl_t), RLC_MEM_AM_PDU_RX_MAX, 0, 1000);
	assert(g_mem_sdu_base);
	g_mem_am_pdu_tx_base = fastalloc_create(sizeof(rlc_am_tx_pdu_ctrl_t), RLC_MEM_AM_PDU_TX_MAX, 0, 1000);
	assert(g_mem_sdu_base);
}


/*****************Basic Memory Allocator**************************************/

/***********************************************************************************/
/* Function : rlc_sdu_new                                                          */
/***********************************************************************************/
/* Description : - allocate new RLC SDU control info                               */
/*               - Notice: will be optimized                                       */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   Return             |    | pointer to RLC SDU Control                          */
/***********************************************************************************/
rlc_sdu_t *rlc_sdu_new()
{
	rlc_sdu_t *sdu;

	sdu = (rlc_sdu_t *)FASTALLOC(g_mem_sdu_base);
	if(sdu)
	{
		sdu->size = 0;
		sdu->offset = 0;
		sdu->n_segment = 0;
	}
	else
		ZLOG_ERR("out of memory to new SDU control.\n");
	
	return sdu;
}

/***********************************************************************************/
/* Function : rlc_sdu_free                                                         */
/***********************************************************************************/
/* Description : - free RLC SDU control info and its segmentations                 */
/*               - Notice: will be optimized                                       */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   sdu                | i  | pointer to RLC SDU Control                          */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void rlc_sdu_free(rlc_sdu_t *sdu)
{
	int i;
	
	/* free the buffers in segmentation */
	for(i=0; i<sdu->n_segment; i++)
	{
		if(sdu->segment[i].free)
			sdu->segment[i].free(sdu->segment[i].data, sdu->segment[i].cookie);
	}
	
	/* free sdu control info */
	FASTFREE(g_mem_sdu_base, sdu);
}

/***********************************************************************************/
/* Function : rlc_dump_sdu                                                         */
/***********************************************************************************/
/* Description : - dump RLC SDU control info                                       */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   sdu                | i  | pointer to RLC SDU Control                          */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void rlc_dump_sdu(rlc_sdu_t *sdu)
{
	
	
	if(sdu->size)
	{
		u8 *data;
		
		data = (u8 *)sdu->segment[0].data + sdu->offset;
		ZLOG_DEBUG("SDU: intact=%u n_segment=%u size=%u offset=%u first 4 bytes (0x%p): 0x%02x%02x%02x%02x\n",
				sdu->intact, sdu->n_segment, sdu->size, sdu->offset,
				data, data[0], data[1], data[2], data[3]);
	}
	else
	{
		ZLOG_DEBUG("SDU: intact=%u n_segment=%u size=%u offset=%u.\n",
			sdu->intact, sdu->n_segment, sdu->size, sdu->offset);
	}
}

void rlc_dump_mem_counter()
{
	ZLOG_INFO("n_alloc_amrx_pdu=%u\n", g_mem_am_pdu_rx_base->alloc_cnt);
	ZLOG_INFO("n_free_amrx_pdu=%u\n", g_mem_am_pdu_rx_base->free_cnt);
	
	ZLOG_INFO("n_alloc_amtx_pdu=%u\n", g_mem_am_pdu_tx_base->alloc_cnt);
	ZLOG_INFO("n_free_amtx_pdu=%u\n", g_mem_am_pdu_tx_base->free_cnt);
	
	ZLOG_INFO("n_alloc_am_pdu_seg=%u\n", g_mem_am_pdu_seg_base->alloc_cnt);
	ZLOG_INFO("n_free_am_pdu_seg=%u\n", g_mem_am_pdu_seg_base->free_cnt);
	
	ZLOG_INFO("n_alloc_sdu=%u\n", g_mem_sdu_base->alloc_cnt);
	ZLOG_INFO("n_free_sdu=%u\n", g_mem_sdu_base->free_cnt);
	
	ZLOG_INFO("n_alloc_um_pdu=%u\n", g_mem_um_pdu_base->alloc_cnt);
	ZLOG_INFO("n_free_um_pdu=%u\n", g_mem_um_pdu_base->free_cnt);
}
/***********************************************************************************/
/* Function : rlc_serialize_sdu 										           */
/***********************************************************************************/
/* Description : - Copy SDU to RLC PDU buffer                                      */
/* 		         - Assume only one segment in SDU                                  */
/* 	                                                                               */
/* Interface :                                                                     */
/* 	 Name               | io | 	  Description                                      */
/* ---------------------|----|-----------------------------------------------------*/
/*   data_ptr           | o  | RLC PDU buffer pointer                              */
/*   sdu                | i  | SDU control info pointer                            */
/*   size               | i  | Copy size                                           */
/*   Return             |    | Size of real RLC PDU	                               */
/***********************************************************************************/
void rlc_serialize_sdu(u8 *data_ptr, rlc_sdu_t *sdu, u32 length)
{
	assert(sdu->n_segment == 1);
	assert(sdu->offset + length <= sdu->segment[0].length);

	memcpy(data_ptr, sdu->segment[0].data + sdu->offset, length);
	sdu->offset += length;
}

/***********************************************************************************/
/* Function : rlc_li_len                                                           */
/***********************************************************************************/
/* Description : - calculate the length of LI in RLC PDU                           */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   n_li               | i  | The number of LI                                    */
/*   Return             |    | Length of LI (align to bytes)                       */
/***********************************************************************************/
inline u32 rlc_li_len(u32 n_li)
{
	u32 li_len;

	assert(n_li);
	
	li_len = ((n_li-1)>>1)*3 + (((n_li&0x01) == 0)<<1);

	return li_len;
}

/***********************************************************************************/
/* Function : rlc_parse_li                                                         */
/***********************************************************************************/
/* Description : - Paser LIs in RLC PDU header                                     */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   e                  | i  | E in RLC header                                     */
/*   li_ptr             | i  | point to the first LI or first SDU                  */
/*   size               | i  | including size of all LIs and size of all SDUs      */
/*   data_ptr           | o  | poniter to first SDU                                */
/*   li_s               | o  | to store parsed LIs                                 */
/*   Return             |    | number of LIs                                       */
/***********************************************************************************/
u32 rlc_parse_li(u32 e, rlc_li_t *li_ptr, u32 size, u8 **data_ptr, u32 *li_s)
{
	u32 n_li = 0;
	u32 clen = 0;

	assert(size > 0);
	
	if(e == 0)
	{
		li_s[0] = size;
		*data_ptr = (u8 *)li_ptr;
 		return 1;
	}
	
	/* parse LI in case of number of LI > 1 */
	while((n_li < RLC_LI_NUM_MAX-1) && (clen < size))
	{
		if(li_ptr->li1 == 0)
			return -1;
			
		li_s[n_li++] = li_ptr->li1;
		clen += li_ptr->li1 + 2;
		
		if(li_ptr->e1)
		{
			if(li_ptr->li2 == 0)
				return -1;
				
			li_s[n_li++] = li_ptr->li2;
			clen += li_ptr->li2 + 1;
			if(li_ptr->e2)
			{
				li_ptr ++;
			}
			else{
				*data_ptr = (u8 *)li_ptr + 3;
				break;
			}
		}
		else{
			*data_ptr = (u8 *)li_ptr + 2;
			break;
		}
	}
	
	if(clen < size)
	{
		/* the last LI may not exist */
		li_s[n_li++] = size - clen;
		clen = size;
	}
	
	/* check PDU size with clen */
	if(clen != size)
		return -2;

	return n_li;
}

/***********************************************************************************/
/* Function : rlc_build_li_from_sdu                                                */
/***********************************************************************************/
/* Description : - Given the PDU size and SDU queue, build LI array                */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   pdu_size           | i  | The allowed size of PDU                             */
/*   head_len           | i  | the length the PDU head                             */
/*   sdu_q              | i  | the SDU queue                                       */
/*   li_s               | o  | LI array                                            */
/*   Return             |    | The number of LI                                    */
/***********************************************************************************/
u32 rlc_build_li_from_sdu(u32 pdu_size, u32 head_len, dllist_node_t *sdu_q, u32 *li_s)
{
	s32 remain_pdu_size;
	u32 n_li = 0;
	u32 sdu_size, lisize = 0;
	rlc_sdu_t *sdu;

	sdu = (rlc_sdu_t *)(DLLIST_HEAD(sdu_q));
	assert(!DLLIST_IS_HEAD(sdu_q,sdu));
	
	remain_pdu_size = pdu_size - head_len;
	if(remain_pdu_size < 1)		//at leaset 1 byte data
	{
		ZLOG_WARN("pdu_size is two small.\n");
		return 0;
	}
	
	do{
		/* substract LI length for current RLC SDU */
		if(n_li > 0)
		{
			if(n_li & 0x01)
				lisize = 2;
			else
				lisize = 1;
				
			if(remain_pdu_size <= lisize)
			{
				break;	//can't contain LI
			}
			else
				remain_pdu_size -= lisize;
		}
		
		sdu_size = sdu->size - sdu->offset;
		assert(sdu_size > 0);
		if(sdu_size <= remain_pdu_size)
		{
			if(sdu_size <= RLC_LI_VALUE_MAX)
			{
				li_s[n_li++] = sdu_size;
				remain_pdu_size -= sdu_size;
			}
			else	//usually won't go into else branch 
			{
				if(n_li == 0)
				{
					//if this is the first SDU, it's size can be large than 2047
					//but on other SDUs can be in current PDU
					li_s[n_li++] = sdu_size;
					remain_pdu_size -= sdu_size;
				}
				else
					remain_pdu_size += lisize;	//add back lisize
				break;
			}
		}
		else
		{
			if(remain_pdu_size <= RLC_LI_VALUE_MAX)
			{
				li_s[n_li++] = remain_pdu_size;
				remain_pdu_size = 0;
			}
			else	//usually won't go into else branch 
			{
				if(n_li == 0)
				{
					li_s[n_li++] = remain_pdu_size;
					remain_pdu_size = 0;
				}
				else
					remain_pdu_size += lisize;	//add back lisize
				break;
			}
		}

		/* n_li should not large than RLC_LI_NUM_MAX */
		if(n_li == RLC_LI_NUM_MAX)
			break;
		
		sdu = (rlc_sdu_t *)(sdu->node.next);
	}while(remain_pdu_size > 0 && !DLLIST_IS_HEAD(sdu_q, sdu));

	return n_li;
}

/***********************************************************************************/
/* Function : rlc_encode_li                                                        */
/***********************************************************************************/
/* Description : - Given the LI array, build the LI of PDU                         */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   li_ptr             | o  | The start address of LI in PDU                      */
/*   n_li               | i  | the number of LI                                    */
/*   li_s               | i  | LI array                                            */
/*   Return             |    | 0                                                   */
/***********************************************************************************/
int rlc_encode_li(rlc_li_t * li_ptr, u32 n_li, u32 li_s[])
{
	u32 i_li;
	
	for(i_li=0; i_li<n_li; i_li++)
	{		
		if(i_li > 0)
		{
			if(i_li & 0x01)
			{
				li_ptr->e1 = (i_li == n_li - 1) ? 0 : 1;
				li_ptr->li1 = li_s[i_li-1];
			}
			else
			{
				li_ptr->e2 = (i_li == n_li - 1) ? 0 : 1;
				li_ptr->li2 = li_s[i_li-1];
				li_ptr ++;
			}
		}
	}

	return 0;
}

/***********************************************************************************/
/* Function : rlc_encode_sdu                                                       */
/***********************************************************************************/
/* Description : - Given the LI array and SDU queue, build the data part of PDU    */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   datatr             | o  | The start address of data part in PDU               */
/*   n_li               | i  | the number of LI                                    */
/*   li_s               | i  | LI array                                            */
/*   sdu_tx_q           | i  | SDU queue                                           */
/*   Return             |    | the number of SDU and the total size of SDU         */
/***********************************************************************************/
int rlc_encode_sdu(u8 *data_ptr, u32 n_li, u32 li_s[], dllist_node_t *sdu_tx_q)
{
	u32 li_idx;
	rlc_sdu_t *sdu;
	u16 total_size = 0, n_sdu = 0;
	
	for(li_idx=0; li_idx<n_li; li_idx++)
	{
		sdu = (rlc_sdu_t *)DLLIST_HEAD(sdu_tx_q);
		
		/* write data */
		rlc_serialize_sdu(data_ptr, sdu, li_s[li_idx]);
		total_size += li_s[li_idx];
		
		if(li_idx != n_li - 1)
		{
			//remove and free sdu
			n_sdu ++;
			dllist_remove(sdu_tx_q, (dllist_node_t *)sdu);
			rlc_sdu_free(sdu);
			data_ptr += li_s[li_idx];
		}
		else
		{
			//last SDU
			if(sdu->offset == sdu->size)
			{
				//remove and free sdu
				n_sdu ++;
				dllist_remove(sdu_tx_q, (dllist_node_t *)sdu);
				rlc_sdu_free(sdu);
			}
			data_ptr += li_s[li_idx];
		}
	}

	return ((n_sdu << 16) | total_size);
}

