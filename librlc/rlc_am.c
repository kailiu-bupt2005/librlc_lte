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
 *   A RLC AM implementation that algins with 36322-930.
 *   Several points that has slight difference from 36322: 
 *   1) SDU discard procedure isn't implemented
 *   2) If a positive acknowledgement has been received for a SDU,
 *       there is no indication is sent to upper in current code.
 *   3) If a AM PDU that is completely or partly duplicated with any PDU in Rx buffer
 *       is recieved, the new received AM PDU will be dropped.
 *
 * @History
 * Phuuix Xiong, Create, 01-25-2011
 */
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "rlc.h"
#include "rlc_pdu.h"
#include "log.h"
#include "ptimer.h"
#include "list.h"
#include "fastalloc.h"

extern void bitcpy(unsigned long *dst, int dst_idx, const unsigned long *src, int src_idx, u32 n);

int rlc_am_rx_assemble_sdu(dllist_node_t *sdu_assembly_q, rlc_am_rx_pdu_ctrl_t *pdu_ctrl);
void rlc_am_rx_delivery_sdu(rlc_entity_am_rx_t *amrx, dllist_node_t *sdu_assembly_q);
int rlc_am_trigger_status_report(rlc_entity_am_rx_t *amrx, rlc_entity_am_tx_t *amtx, u16 sn, int forced);
int rlc_am_tx_update_poll(rlc_entity_am_tx_t *amtx, u16 is_retx, u16 data_size);
int rlc_am_tx_deliver_poll(rlc_entity_am_tx_t *amtx);
void rlc_am_tx_add_retx(rlc_entity_am_tx_t *amtx, rlc_am_tx_pdu_ctrl_t *pdu_ctrl);

extern fastalloc_t *g_mem_am_pdu_seg_base;
extern fastalloc_t *g_mem_am_pdu_rx_base;
extern fastalloc_t *g_mem_am_pdu_tx_base;

/***********************************************************************************/
/* Function : t_Reordering_am_func                                                 */
/***********************************************************************************/
/* Description : - RLC AM t-Reordering callback                                    */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
static void t_Reordering_am_func(void *timer, u32 arg1, u32 arg2)
{
	rlc_entity_am_rx_t *amrx = (rlc_entity_am_rx_t *)arg1;
	u32 sn;
	u32 sn_fs = RLC_SN_MAX_10BITS + 1;
/* 
When t-Reordering expires, the receiving side of an AM RLC entity shall:
-	update VR(MS) to the SN of the first AMD PDU with SN >= VR(X) for which not all byte segments have been received;
-	if VR(H) > VR(MS):
     -	start t-Reordering;
     -	set VR(X) to VR(H).
*/
	ZLOG_DEBUG("t_Reordering expires: lcid=%d\n", amrx->logical_chan);

	sn = amrx->VR_X;
	while(amrx->rxpdu[sn] && amrx->rxpdu[sn]->is_intact)
		sn = RLC_MOD(sn+1, sn_fs);

	amrx->VR_MS = sn;

	if(RLC_SN_LESS(amrx->VR_MS, amrx->VR_H, sn_fs))
	{
		ZLOG_DEBUG("start timer t_Reordering: lcid=%d\n", amrx->logical_chan);
		rlc_timer_start(&amrx->t_Reordering);
		amrx->VR_X = amrx->VR_H;
	}
/*
	The receiving side of an AM RLC entity shall trigger a STATUS report when t-Reordering expires
*/
	rlc_am_trigger_status_report(amrx, amrx->amtx, 0, 1/*forced*/);

	ZLOG_DEBUG("RLC AM Counters after t_Reordering: lcid=%d VR_R=%u VR_X=%u VR_H=%u VR_MR=%u VR_MS=%u.\n", 
		amrx->logical_chan, amrx->VR_R, amrx->VR_X, amrx->VR_H, amrx->VR_MR, amrx->VR_MS);
}

/***********************************************************************************/
/* Function : t_PollRetransmit_func                                                */
/***********************************************************************************/
/* Description : - RLC AM t_PollRetransmit timer callback                          */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
static void t_PollRetransmit_func(void *timer, u32 arg1, u32 arg2)
{
	rlc_entity_am_tx_t *amtx = (rlc_entity_am_tx_t *)arg1;
	u16 sn;
	
/*
Upon expiry of t-PollRetransmit, the transmitting side of an AM RLC entity shall:
-	if both the transmission buffer and the retransmission buffer are empty (excluding transmitted RLC data PDU awaiting for acknowledgements); or
-	if no new RLC data PDU can be transmitted (e.g. due to window stalling):
   -	consider the AMD PDU with SN = VT(S) - 1 for retransmission; or
   -	consider any AMD PDU which has not been positively acknowledged for retransmission;
-	include a poll in a RLC data PDU as described in section 5.2.2.1.
*/
	assert(amtx);
	ZLOG_DEBUG("t_PollRetransmit expires: lcid=%d\n", amtx->logical_chan);

	if(rlc_am_tx_update_poll(amtx, 1 /* is_retx */, 0))
	{
		/* consider any AMD PDU which has not been positively acknowledged for retransmission */
		sn = amtx->VT_A;
		while(sn != amtx->VT_S)
		{
			if(amtx->txpdu[sn])
			{
				/* add pdu_ctrl to ReTx queue: ascending on SN */
				if(amtx->txpdu[sn]->node.next == NULL)
					rlc_am_tx_add_retx(amtx, amtx->txpdu[sn]);
				
				break;
			}

			sn = RLC_MOD(sn+1, RLC_SN_MAX_10BITS+1);
		}
	}
}

/***********************************************************************************/
/* Function : t_StatusProhibit_func                                                */
/***********************************************************************************/
/* Description : - RLC AM t_StatusProhibit timer callback                          */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
static void t_StatusProhibit_func(void *timer, u32 arg1, u32 arg2)
{
	rlc_entity_am_tx_t *amtx = (rlc_entity_am_tx_t *)arg1;
	
	ZLOG_DEBUG("t_StatusProhibit expires: lcid=%d\n", amtx->logical_chan);
}

/***********************************************************************************/
/* Function : t_StatusPdu_func                                                     */
/***********************************************************************************/
/* Description : - Status Pdu timer: a new timer introduced by this library        */
/*               - Start/Restart on Rx a Poll bit set PDU, stop on Tx status PDU   */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
static void t_StatusPdu_func(void *timer, u32 arg1, u32 arg2)
{
	rlc_entity_am_rx_t *amrx = (rlc_entity_am_rx_t *)arg1;
	
	ZLOG_DEBUG("t_StatusPdu expires: lcid=%d\n", amrx->logical_chan);

	/* trigger a status PDU */
	rlc_am_trigger_status_report(amrx, amrx->amtx, 0/* sn */, 1 /* forced */);
}

/***********************************************************************************/
/* Function : rlc_am_init                                                          */
/***********************************************************************************/
/* Description : - Init RLC an AM entity                                           */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   rlc_am             | i  | pointer of RLC AM entity                            */
/*   t_Reordering       | i  | t_Reordering timer duration                         */
/*   t_StatusPdu        | i  | t_StatusPdu timer duration                          */
/*   t_StatusProhibit   | i  | t_StatusProhibit timer duration                     */
/*   t_PollRetransmit   | i  | t_PollRetransmit timer duration                     */
/*   maxRetxThreshold   | i  | see spec                                            */
/*   pollPDU            | i  | see spec                                            */
/*   pollByte           | i  | see spec                                            */
/*   free_pdu           | i  | function to free pdu                                */
/*   free_sdu           | i  | function to free sdu                                */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void rlc_am_init(rlc_entity_am_t *rlc_am, 
					u32 t_Reordering, 
					u32 t_StatusPdu,
					u32 t_StatusProhibit, 
					u32 t_PollRetransmit, 
					u16 maxRetxThreshold,
					u16 pollPDU,
					u16 pollByte,
					void (*free_pdu)(void *, void *),
					void (*free_sdu)(void *, void *))
{
	assert(rlc_am);
	assert(free_pdu);
	assert(free_sdu);
	
	memset(rlc_am, 0, sizeof(rlc_entity_am_t));

	rlc_am->amrx.type = RLC_ENTITY_TYPE_AM;
	rlc_am->amrx.amtx = &rlc_am->amtx;
	rlc_am->amrx.AM_Window_Size = 512;
	rlc_am->amrx.sn_max = RLC_SN_MAX_10BITS;
	rlc_am->amrx.VR_MR = rlc_am->amrx.VR_R + rlc_am->amrx.AM_Window_Size;
	rlc_am->amrx.t_Reordering.duration = t_Reordering;
	rlc_am->amrx.t_Reordering.onexpired_func = t_Reordering_am_func;
	rlc_am->amrx.t_Reordering.param[0] = (u32)&rlc_am->amrx;
	rlc_am->amrx.t_StatusPdu.duration = t_StatusPdu;
	rlc_am->amrx.t_StatusPdu.onexpired_func = t_StatusPdu_func;
	rlc_am->amrx.t_StatusPdu.param[0] = (u32)&rlc_am->amrx;
	rlc_am->amrx.free_pdu = free_pdu;
	rlc_am->amrx.free_sdu = free_sdu;
	dllist_init(&(rlc_am->amrx.sdu_assembly_q));

	rlc_am->amtx.type = RLC_ENTITY_TYPE_AM;
	rlc_am->amtx.amrx = &rlc_am->amrx;
	rlc_am->amtx.AM_Window_Size = 512;
	rlc_am->amtx.sn_max = RLC_SN_MAX_10BITS;
	rlc_am->amtx.VT_MS = rlc_am->amtx.VT_S + rlc_am->amtx.AM_Window_Size;
	rlc_am->amtx.t_PollRetransmit.duration = t_PollRetransmit;
	rlc_am->amtx.t_PollRetransmit.onexpired_func = t_PollRetransmit_func;
	rlc_am->amtx.t_PollRetransmit.param[0] = (u32)&rlc_am->amtx;
	rlc_am->amtx.t_StatusProhibit.duration = t_StatusProhibit;
	rlc_am->amtx.t_StatusProhibit.onexpired_func = t_StatusProhibit_func;
	rlc_am->amtx.t_StatusProhibit.param[0] = (u32)&rlc_am->amtx;
	rlc_am->amtx.maxRetxThreshold = maxRetxThreshold;
	rlc_am->amtx.pollPDU = pollPDU;
	rlc_am->amtx.pollByte = pollByte;
	rlc_am->amtx.free_pdu = free_pdu;
	rlc_am->amtx.free_sdu = free_sdu;
	dllist_init(&(rlc_am->amtx.sdu_tx_q));
	dllist_init(&(rlc_am->amtx.pdu_retx_q));
}

/***********************************************************************************/
/* Function : rlc_am_set_deliv_func                                                */
/***********************************************************************************/
/* Description : - deliver reassembled SDUs to Upper                               */
/*                 Provided by Upper (PDCP etc)                                    */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   rlc_am             | i  | AM entity                                           */
/*   deliv_sdu          | i  | delivery function provided by upper                 */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void rlc_am_set_deliv_func(rlc_entity_am_t *rlc_am, void (*deliv_sdu)(struct rlc_entity_am_rx *, rlc_sdu_t *))
{
	if(rlc_am)
		rlc_am->amrx.deliv_sdu = deliv_sdu;
}

/***********************************************************************************/
/* Function : rlc_am_set_maxretx_func                                              */
/***********************************************************************************/
/* Description : - Called when MAX ReTx reached of PDU                             */
/*                 Provided by Upper (PDCP etc)                                    */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   rlc_am             | i  | AM entity                                           */
/*   max_retx           | i  | function provided by upper                          */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void rlc_am_set_maxretx_func(rlc_entity_am_t *rlc_am, int (*max_retx)(struct rlc_entity_am_tx *, u32))
{
	if(rlc_am)
		rlc_am->amtx.max_retx_notify = max_retx;
}


/* new a RLC AM PDU segment for received PDU or PDU segment, only used in Rx entity */
rlc_am_pdu_segment_t *rlc_am_pdu_segment_new()
{
	rlc_am_pdu_segment_t *pdu_segment = NULL;

	pdu_segment = (rlc_am_pdu_segment_t *)FASTALLOC(g_mem_am_pdu_seg_base);
	if(pdu_segment)
	{
		pdu_segment->start_offset = 0;
		pdu_segment->end_offset = 0;
		pdu_segment->refcnt = 0;
		pdu_segment->buf_ptr = NULL;
		pdu_segment->buf_len = 0;
		pdu_segment->buf_cookie = NULL;
		pdu_segment->free = NULL;
		pdu_segment->fi = 0;
		pdu_segment->sn = 0;
		pdu_segment->lsf = 0;
		pdu_segment->n_li = 0;
		pdu_segment->data_ptr = NULL;

		dllist_init(&pdu_segment->node);
	}
	return pdu_segment;
}

/* free a RLC AM PDU segment, only used in Rx entity */
void rlc_am_pdu_segment_free(rlc_am_pdu_segment_t *pdu_segment)
{
	RLC_DEREF(pdu_segment);
	if(pdu_segment->refcnt <= 0)
	{
		if(pdu_segment->free)
			pdu_segment->free(pdu_segment->buf_ptr, pdu_segment->buf_cookie);
		
		FASTFREE(g_mem_am_pdu_seg_base, pdu_segment);
	}
}

/* dump a RLC AM PDU segment */
void rlc_am_pdu_segment_dump(rlc_am_pdu_segment_t *pdu_segment)
{	
	if(pdu_segment == NULL)
		return;

	ZLOG_INFO("sn=%u so=(%u,%u) fi=%u lsf=%u n_li=%u li_s=(%u %u %u..)\n", 
			pdu_segment->sn, pdu_segment->start_offset, pdu_segment->end_offset,
			pdu_segment->fi, pdu_segment->lsf, pdu_segment->n_li,
			pdu_segment->li_s[0], pdu_segment->li_s[1], pdu_segment->li_s[2]);
}

/* new a RLC AM Tx PDU control structure */
rlc_am_tx_pdu_ctrl_t *rlc_am_tx_pdu_ctrl_new()
{
	rlc_am_tx_pdu_ctrl_t *pdu_ctrl;

	pdu_ctrl = (rlc_am_tx_pdu_ctrl_t *)FASTALLOC(g_mem_am_pdu_tx_base);
	if(pdu_ctrl)
	{
		pdu_ctrl->buf_ptr = NULL;
		pdu_ctrl->pdu_size = 0;
		pdu_ctrl->fi = 0;
		pdu_ctrl->sn = 0;
		pdu_ctrl->n_li = 0;
		pdu_ctrl->data_ptr = NULL;
		pdu_ctrl->i_retransmit_seg = 0;
		pdu_ctrl->n_retransmit_seg = 0;
		pdu_ctrl->RETX_COUNT = 0;
		pdu_ctrl->node.prev = NULL;
		pdu_ctrl->node.next = NULL;
	}

	return pdu_ctrl;
}

/* free a RLC AM Tx PDU control structure */
void rlc_am_tx_pdu_ctrl_free(rlc_am_tx_pdu_ctrl_t *pdu_ctrl)
{
	/* free buffer of PDU */
	if(pdu_ctrl->buf_free)
		pdu_ctrl->buf_free(pdu_ctrl->buf_ptr, pdu_ctrl->buf_cookie);

	/* free pdu control */
	FASTFREE(g_mem_am_pdu_tx_base, pdu_ctrl);
}

/* dump a RLC AM Tx PDU control structure */
void rlc_am_tx_pdu_ctrl_dump(rlc_am_tx_pdu_ctrl_t *pdu_ctrl)
{
	int idx, i;
	
	if(pdu_ctrl == NULL)
		return;
	
	ZLOG_INFO("SN=%u n_li=%u li_s=(%u %u %u..) RETX_COUNT=%u ReTx_idx=%u\n", 
			pdu_ctrl->sn, pdu_ctrl->n_li, 
			pdu_ctrl->li_s[0], pdu_ctrl->li_s[1], pdu_ctrl->li_s[2],
			pdu_ctrl->RETX_COUNT, pdu_ctrl->i_retransmit_seg);
	for(idx=0; idx<pdu_ctrl->n_retransmit_seg; idx++)
	{
		i = RLC_MOD(idx, RLC_SEG_NUM_MAX);
		ZLOG_INFO("  Retx segment %d sostart=%u soend=%u lsf=%u\n", 
				i, pdu_ctrl->retransmit_seg[i].start_offset, 
				pdu_ctrl->retransmit_seg[i].end_offset, pdu_ctrl->retransmit_seg[i].lsf);
	}
}

/***********************************************************************************/
/* Function : rlc_am_tx_sdu_enqueue                                                */
/***********************************************************************************/
/* Description : - RLC SDU enqueue (only one segment)                              */
/*               - Called by PDCP, etc                                             */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   amtx               | i  | RLC AM entity                                       */
/*   buf_ptr            | i  | RLC SDU buffer pointer                              */
/*   sdu_size           | i  | Size of SDU                                         */
/*   cookie             | i  | parameter of free function                          */
/*   Return             |    | 0 is success                                        */
/***********************************************************************************/
int rlc_am_tx_sdu_enqueue(rlc_entity_am_tx_t *amtx, u8 *buf_ptr, u32 sdu_size, void *cookie)
{
	rlc_sdu_t *sdu;
	
	if(sdu_size <= 0 || buf_ptr == NULL || amtx == NULL)
		return -1;
	
	sdu = rlc_sdu_new();
	if(sdu == NULL)
	{
		ZLOG_ERR("out of memory: rlc_sdu_new() lcid=%d.\n", amtx->logical_chan);
		return -1;
	}
	dllist_append(&amtx->sdu_tx_q, (dllist_node_t *)sdu);

	sdu->segment[0].cookie = cookie;
	sdu->segment[0].free = amtx->free_sdu;
	sdu->segment[0].data = buf_ptr;
	sdu->segment[0].length = sdu_size;
	sdu->size = sdu_size;
	sdu->n_segment = 1;
	sdu->intact = 1;
	amtx->sdu_total_size += sdu_size;
	amtx->n_sdu ++;
	
	ZLOG_DEBUG("AM SDU enqueue: lcid=%d buf_ptr=%p sdu_size=%u total_size=%u data=0x%02x%02x%02x%02x\n",
			amtx->logical_chan, buf_ptr, sdu_size, amtx->sdu_total_size, buf_ptr[0], buf_ptr[1], buf_ptr[2], buf_ptr[3]);
	
	return 0;
}


/* return the number of not recieved PDU segment */
u32 rlc_am_rx_get_n_miss_segment(rlc_entity_am_rx_t *amrx, rlc_am_rx_pdu_ctrl_t *pdu_ctrl, rlc_spdu_so_t *so, u32 n_so)
{
	u32 n_miss = 0;
	rlc_am_pdu_segment_t *pdu_segment;
	u32 offset = 0;
	
	if(!pdu_ctrl->is_intact)
	{
		pdu_segment = (rlc_am_pdu_segment_t *)DLLIST_HEAD(&pdu_ctrl->rx_segq);

		while(!DLLIST_IS_HEAD(&pdu_ctrl->rx_segq, pdu_segment))
		{
			if(pdu_segment->start_offset != offset)
			{
				if(n_miss < n_so)
				{
					so[n_miss].sostart = offset;
					so[n_miss].soend = pdu_segment->start_offset;
				}
				n_miss ++;
			}

			offset = pdu_segment->end_offset;
			
			pdu_segment = (rlc_am_pdu_segment_t *)pdu_segment->node.next;
		}

		/* for last one */
		pdu_segment = (rlc_am_pdu_segment_t *)DLLIST_TAIL(&pdu_ctrl->rx_segq);
		if(!pdu_segment->lsf)
		{
			if(n_miss < n_so)
			{
				so[n_miss].sostart = pdu_segment->end_offset;
				so[n_miss].soend = RLC_AM_SO_END;
			}
			n_miss ++;
		}
	}

	return n_miss;
}

/***********************************************************************************/
/* Function : rlc_am_tx_get_status_pdu_size                                        */
/***********************************************************************************/
/* Description : - provide the size of status pdu to MAC                           */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   amtx               | i  | RLC AM entity                                       */
/*   Return             |    | size of STATUS PDU, 0 means no STATUS PDU           */
/***********************************************************************************/
u32 rlc_am_tx_get_status_pdu_size(rlc_entity_am_tx_t *amtx)
{
	u32 pdu_size_in_bits = 0;
	rlc_entity_am_rx_t *amrx;
	u32 sn, n_miss;
	rlc_am_rx_pdu_ctrl_t *pdu_ctrl;

	if(amtx == NULL)
		return 0;

	amrx = amtx->amrx;
	if(amtx->status_pdu_triggered && !rlc_timer_is_running(&amtx->t_StatusProhibit))
	{
		sn = amrx->VR_R;
		while(sn != amrx->VR_MS)
		{
			pdu_ctrl = (rlc_am_rx_pdu_ctrl_t *)amrx->rxpdu[sn];
			if(pdu_ctrl == NULL)
			{
				pdu_size_in_bits += 12;		//12 = size of (NACK_SN,E1,E2) set
			}
			else if(!pdu_ctrl->is_intact)
			{
				n_miss = rlc_am_rx_get_n_miss_segment(amrx, pdu_ctrl, NULL, 0);
				pdu_size_in_bits += n_miss*42;	//42 = size of (NACK_SN, E1, E2, SOstart, Soend)
			}

			sn = RLC_MOD(sn+1, RLC_SN_MAX_10BITS+1);
		}

		/* add size of header */
		pdu_size_in_bits += 15;

		/* return in bytes */
		return (pdu_size_in_bits+7)/8;
	}
	
	return 0;
}

/***********************************************************************************/
/* Function : rlc_am_tx_get_retx_pdu_size                                          */
/***********************************************************************************/
/* Description : - if there are PDUs in ReTx queue, return the size of first       */
/*                 segment of first  ReTx PDU                                      */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   amtx               | i  | RLC AM entity                                       */
/*   Return             |    | size of ReTx PDU                                    */
/***********************************************************************************/
u32 rlc_am_tx_get_retx_pdu_size(rlc_entity_am_tx_t *amtx)
{
	rlc_am_tx_pdu_ctrl_t *pdu_ctrl;
	rlc_am_pdu_segment_info_t *pdu_segment;
	u32 pdu_size;
	u32 i_li, n_li, now_offset;
	
	/* ReTx PDU first */
	if(!DLLIST_EMPTY(&amtx->pdu_retx_q))
	{
		pdu_ctrl = (rlc_am_tx_pdu_ctrl_t *)DLLIST_HEAD(&amtx->pdu_retx_q);
		assert(pdu_ctrl->n_retransmit_seg > 0);
		
		pdu_segment = &pdu_ctrl->retransmit_seg[pdu_ctrl->i_retransmit_seg];
		
		/* return size of the first segment if pdu_segment->pdu_size > 0
		 * pdu_segment->pdu_size is pre-calculated:
		 * 1) full pdu
		 * 2) rlc_am_tx_get_retx_pdu_size() is repeatedly called and rlc_am_tx_build_retx_pdu isn't
		 *     called in between.
		 */
		if(pdu_segment->pdu_size)
			return pdu_segment->pdu_size;
		
		/* get number of LI in PDU segment */
		/* 1) find start point */
		n_li = 0;
		now_offset = 0;
		for(i_li = 0; i_li < pdu_ctrl->n_li; i_li ++)
		{
			if(now_offset < pdu_segment->start_offset)
			{
				now_offset += pdu_ctrl->li_s[i_li];
			}
			else
				break;
		}
		
		if(now_offset > pdu_segment->start_offset)
		{
			n_li ++;
		}

		/* 2) find stop point */
		for(; i_li < pdu_ctrl->n_li; i_li ++)
		{
			if(now_offset < pdu_segment->end_offset)
			{
				now_offset += pdu_ctrl->li_s[i_li];
				n_li ++;
			}
			else
				break;
		}

		assert(n_li > 0);

		pdu_size = sizeof(rlc_am_pdu_segment_head_t) + rlc_li_len(n_li)	+ pdu_segment->end_offset - pdu_segment->start_offset;

		/* save pdu_size for next use */
		pdu_segment->pdu_size = pdu_size;
		
		return pdu_size;
	}
	
	return 0;
}

/***********************************************************************************/
/* Function : rlc_am_tx_get_fresh_pdu_size                                         */
/***********************************************************************************/
/* Description : - return the size of fresh PDU waiting for transmission           */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   amtx               | i  | RLC AM entity                                       */
/*   Return             |    | size of fresh PDU                                   */
/***********************************************************************************/
u32 rlc_am_tx_get_fresh_pdu_size(rlc_entity_am_tx_t *amtx)
{
	
	u32 head_len = sizeof(rlc_am_pdu_head_t);
	u32 li_len = 0;
	u32 pdu_size;
	
	/* no data in queue */
	if(amtx->sdu_total_size == 0)
		return 0;
		
	assert(amtx->n_sdu > 0);
	
	/* The transmitting side of an AM RLC entity shall not deliver to lower layer 
	   any RLC data PDU whose SN falls outside of the transmitting window.
	 */
	if(!RLC_SN_IN_TRANSMITTING_WIN(amtx->VT_S, amtx->VT_MS, amtx->VT_A, amtx->sn_max + 1))
		return 0;
	
	/* number of LI equals to n_sdu-1 */
	li_len = rlc_li_len(amtx->n_sdu);
		
	pdu_size = amtx->sdu_total_size + head_len + li_len;
	return RLC_MIN(pdu_size, 0xFFF0);
}

/* get avaible SDU size (including sizeof RLC header) in RLC AM entity:
 * 1) if there is STATUS PDU, return the size of status PDU;
 * 2) if there are PDUs in ReTx queue, return the size of waiting for ReTx part of 1st ReTx PDU;
 * 3) else return the total size of all SDUs in sdu queue.
 */
u32 rlc_am_tx_get_sdu_size(rlc_entity_am_tx_t *amtx)
{
	u32 pdu_size;

	/* status pdu first */
	pdu_size = rlc_am_tx_get_status_pdu_size(amtx);
	if(pdu_size)
		return pdu_size;
	
	/* ReTx PDU second */
	pdu_size = rlc_am_tx_get_retx_pdu_size(amtx);
	if(pdu_size)
		return pdu_size;

	return rlc_am_tx_get_fresh_pdu_size(amtx);
}

/* add pdu_ctrl to ReTx queue: ascending on SN */
void rlc_am_tx_add_retx(rlc_entity_am_tx_t *amtx, rlc_am_tx_pdu_ctrl_t *pdu_ctrl)
{
	rlc_am_tx_pdu_ctrl_t *tmp_ctrl;
	
	assert(pdu_ctrl->node.next == NULL);

	tmp_ctrl = (rlc_am_tx_pdu_ctrl_t *)DLLIST_HEAD(&amtx->pdu_retx_q);
	while(!DLLIST_IS_HEAD(&amtx->pdu_retx_q,tmp_ctrl))
	{
		if(RLC_SN_LESS(tmp_ctrl->sn, pdu_ctrl->sn, (RLC_SN_MAX_10BITS+1)))
			tmp_ctrl = (rlc_am_tx_pdu_ctrl_t *)(tmp_ctrl->node.next);
		else
			break;
	}

	/* insert pdu_ctrl before tmp_ctrl */
	pdu_ctrl->node.next = &tmp_ctrl->node;
	pdu_ctrl->node.prev = tmp_ctrl->node.prev;
	(tmp_ctrl->node.prev)->next = &pdu_ctrl->node;
	tmp_ctrl->node.prev = &pdu_ctrl->node;
	
}
			

#define MAXINFO_NUM 128
/***********************************************************************************/
/* Function : rlc_am_tx_build_status_pdu                                           */
/***********************************************************************************/
/* Description : - Called by MAC to build RLC Status PDU                           */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   amtx               | i  | RLC AM TX entity                                    */
/*   amrx               | i  | RLC AM RX entity                                    */
/*   buf_ptr            | o  | RLC PDU buffer pointer, provided by MAC             */
/*   pdu_size           | i  | requested PDU Size                                  */
/*   Return             |    | Size of real RLC PDU                                */
/***********************************************************************************/
int rlc_am_tx_build_status_pdu(rlc_entity_am_tx_t *amtx, rlc_entity_am_rx_t *amrx, u8 *buf_ptr, u16 pdu_size)
{
	u32 pdu_size_in_bits = 0;
	u16 sn, n_miss;
	rlc_am_rx_pdu_ctrl_t *pdu_ctrl;
	rlc_am_status_pdu_head_t *pdu_head = (rlc_am_status_pdu_head_t *)buf_ptr;
	u32 ack_sn;
	struct nacksn_info ninfo[MAXINFO_NUM];
	u32 i, n_nacksn = 0;
	rlc_spdu_so_t soinfo[MAXINFO_NUM];
	u8 *old_buf_ptr = buf_ptr;

	if(pdu_size < sizeof(rlc_am_status_pdu_head_t))
	{
		ZLOG_WARN("lcid=%d pdu_size=%d is too small\n", amtx->logical_chan, pdu_size);
		return 0;
	}

	memset(buf_ptr, 0, pdu_size);

	/* 15 = size of head
	 *  12 = size of (NACK_SN,E1,E2) set
	 *  30 = size of (SOstart, Soend)
	 *  42 = 12 + 30
	 */
	if(amtx->status_pdu_triggered && !rlc_timer_is_running(&amtx->t_StatusProhibit))
	{
		/* 1) first round: build nack_sn info */
		ack_sn = amrx->VR_MS;
		sn = amrx->VR_R;
		pdu_size_in_bits = 15;
		while(sn != amrx->VR_MS && n_nacksn<MAXINFO_NUM)
		{
			pdu_ctrl = (rlc_am_rx_pdu_ctrl_t *)amrx->rxpdu[sn];
			if(pdu_ctrl == NULL)
			{
				if(pdu_size >= (pdu_size_in_bits+12+7)/8)
				{
					pdu_size_in_bits += 12;
					ninfo[n_nacksn].nacksn.nack_sn = sn;
					ninfo[n_nacksn].nacksn.e1 = 1;
					ninfo[n_nacksn].nacksn.e2 = 0;
					n_nacksn ++;
				}
				else
				{
					/* no enough buffer to hold one NACK_SN */
					ack_sn = sn;
					break;
				}
			}
			else if(!pdu_ctrl->is_intact)
			{
				n_miss = rlc_am_rx_get_n_miss_segment(amrx, pdu_ctrl, soinfo, MAXINFO_NUM);
				if(pdu_size >= (pdu_size_in_bits+42*n_miss+7)/8)
				{
					pdu_size_in_bits += 42*n_miss;
					for(i=0; i<n_miss; i++)
					{
						ninfo[n_nacksn].nacksn.nack_sn = sn;
						ninfo[n_nacksn].nacksn.e1 = 1;
						ninfo[n_nacksn].nacksn.e2 = 1;

						ninfo[n_nacksn].so.sostart = soinfo[i].sostart;
						ninfo[n_nacksn].so.soend = soinfo[i].soend;
						n_nacksn ++;
					}
				}
				else
				{
					/* no enough buffer to hold all segments of one NACK_SN */
					ack_sn = sn;
					break;
				}
			}

			sn = RLC_MOD(sn+1, RLC_SN_MAX_10BITS+1);
		}

		/* set the last e1 to 0 */
		if(n_nacksn)
			ninfo[n_nacksn-1].nacksn.e1 = 0;

		/* 2) second round: encoding PDU */
		pdu_head->dc = RLC_AM_DC_CTRL_PDU;
		pdu_head->cpt = 0;
		pdu_head->e1 = (n_nacksn>0);
		pdu_head->ack_sn = ack_sn;
		
		pdu_size_in_bits = 15;	//15 = size of head

		for(i=0; i<n_nacksn; i++)
		{
			if(ninfo[i].nacksn.e2 == 0)
			{
				//12 = size of (NACK_SN,E1,E2) set
				bitcpy((unsigned long *)buf_ptr, pdu_size_in_bits, (const unsigned long *)&ninfo[i].nacksn, 0, 12);
				pdu_size_in_bits += 12;
				if(pdu_size_in_bits >= 32)
				{
					buf_ptr += 4;
					pdu_size_in_bits -= 32;
				}
			}
			else
			{
				//12 = size of (NACK_SN,E1,E2) set
				bitcpy((unsigned long *)buf_ptr, pdu_size_in_bits, (const unsigned long *)&ninfo[i].nacksn, 0, 12);
				pdu_size_in_bits += 12;
				if(pdu_size_in_bits >= 32)
				{
					buf_ptr += 4;
					pdu_size_in_bits -= 32;
				}
				//30 = size of (SOstart, Soend)
				bitcpy((unsigned long *)buf_ptr, pdu_size_in_bits, (const unsigned long *)&ninfo[i].so, 0, 30);
				pdu_size_in_bits += 30;
				if(pdu_size_in_bits >= 32)
				{
					buf_ptr += 4;
					pdu_size_in_bits -= 32;
				}
			}
		}

		ZLOG_DEBUG("lcid=%d ack_sn=%u n_nacksn=%u pdu_size_in_bits=%u nack_sn=(0x%x 0x%x 0x%x..)\n", 
				amtx->logical_chan, ack_sn, n_nacksn, pdu_size_in_bits,
				ninfo[0].nacksn.nack_sn | (ninfo[0].nacksn.e2 << 15),
				ninfo[1].nacksn.nack_sn | (ninfo[1].nacksn.e2 << 15),
				ninfo[2].nacksn.nack_sn | (ninfo[2].nacksn.e2 << 15));

		/* start timer t_StatusProhibit */
		amtx->status_pdu_triggered = 0;
		ZLOG_DEBUG("start timer t_StatusProhibit: lcid=%d\n", amtx->logical_chan);
		rlc_timer_start(&amtx->t_StatusProhibit);

		return buf_ptr - old_buf_ptr + (pdu_size_in_bits+7)/8;
	}

	return 0;
}

/* update poll bit */
int rlc_am_tx_update_poll(rlc_entity_am_tx_t *amtx, u16 is_retx, u16 data_size)
{
	u32 poll = 0;

	if(is_retx == 0)
	{
/*
	Upon assembly of a new AMD PDU, the transmitting side of an AM RLC entity shall:
	-	increment PDU_WITHOUT_POLL by one;
	-	increment BYTE_WITHOUT_POLL by every new byte of Data field element that it maps to the Data field of the RLC data PDU;
	-	if PDU_WITHOUT_POLL >= pollPDU; or
	-	if BYTE_WITHOUT_POLL >= pollByte;
	     -	include a poll in the RLC data PDU as described below.
*/
		amtx->PDU_WITHOUT_POLL ++;
		amtx->BYTE_WITHOUT_POLL += data_size;	
		
		if(amtx->PDU_WITHOUT_POLL >= amtx->pollPDU)
			poll = 1;
		
		if(amtx->BYTE_WITHOUT_POLL > amtx->pollByte)
			poll = 1;
	}
	
	/* - if both the transmission buffer and the retransmission buffer becomes empty 
	   (excluding transmitted RLC data PDU awaiting for acknowledgements) after the 
	   transmission of the RLC data PDU; or
       - if no new RLC data PDU can be transmitted after the transmission of the RLC 
       data PDU (e.g. due to window stalling);
         - include a poll in the RLC data PDU as described below
     */
	if((DLLIST_EMPTY(&amtx->sdu_tx_q) && DLLIST_EMPTY(&amtx->pdu_retx_q)) ||
			(amtx->VT_S == amtx->VT_MS))
		poll = 1;

	amtx->poll_bit = RLC_MAX(amtx->poll_bit, poll);

	return poll;
}

/* when a poll bit is set for a PDU, this function is called */
int rlc_am_tx_deliver_poll(rlc_entity_am_tx_t *amtx)
{
	if(amtx->poll_bit)
	{
		amtx->PDU_WITHOUT_POLL = 0;
		amtx->BYTE_WITHOUT_POLL = 0;
		
		/* start timer t_PollRetransmit */
		amtx->POLL_SN = (amtx->VT_S-1) & (amtx->sn_max+1);
		if(rlc_timer_is_running(&amtx->t_PollRetransmit))
		{
			ZLOG_DEBUG("stop timer t_PollRetransmit: lcid=%d\n", amtx->logical_chan);
			rlc_timer_stop(&amtx->t_PollRetransmit);
		}
		ZLOG_DEBUG("start timer t_PollRetransmit: lcid==%d\n", amtx->logical_chan);
		rlc_timer_start(&amtx->t_PollRetransmit);
	}

	return amtx->poll_bit;
}

/***********************************************************************************/
/* Function : rlc_am_tx_build_retx_pdu                                             */
/***********************************************************************************/
/* Description : - Called by MAC to build RLC PDU Segment                          */
/*               - transmit at most the first segment in first pdu                 */
/*               - always make a copy to destination buf_ptr                       */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   amtx               | i  | RLC AM TX entity                                    */
/*   buf_ptr            | o  | RLC PDU buffer pointer, provided by MAC             */
/*   pdu_size           | i  | requested PDU Size                                  */
/*   Return             |    | Size of real RLC PDU                                */
/***********************************************************************************/
int rlc_am_tx_build_retx_pdu(rlc_entity_am_tx_t *amtx, u8 *buf_ptr, u16 pdu_size, void *cookie)
{
	rlc_am_tx_pdu_ctrl_t *pdu_ctrl;
	u32 i_seg;								//pdu_ctrl->i_retransmit_seg
	u32 i_li;								//index in pdu_ctrl->li_s[]
	s32 li_offset;							//
	s32 remain_pdu_size;
	rlc_am_pdu_segment_head_t *segment_head;
	rlc_am_pdu_segment_t pdu_segment, *pdu_segment_ctrl;
	rlc_li_t *li_ptr;
	u8 *data_ptr, *data_ptr_src;
	u32 data_size = 0;						//size of SDU to copy
	u8 fi[2];
	u32 tmpv;
	rlc_am_pdu_segment_info_t *seginfo;
	
	if(DLLIST_EMPTY(&amtx->pdu_retx_q))
		return 0;
	
	pdu_ctrl = (rlc_am_tx_pdu_ctrl_t *)DLLIST_HEAD(&amtx->pdu_retx_q);
	assert(pdu_ctrl);
	assert(pdu_ctrl->n_retransmit_seg > 0);
	assert(pdu_ctrl->buf_ptr);
	assert(pdu_ctrl->pdu_size);

	/* transmit the first segment in first PDU */
	i_seg = pdu_ctrl->i_retransmit_seg;
	seginfo = &pdu_ctrl->retransmit_seg[i_seg];

	/* specially: The PDU hasn't been segmented and requested size is larger than the size of PDU,
	  *              then just copy the whole AM PDU to buf_ptrs.
	  */
	if(seginfo->start_offset == 0 && seginfo->lsf && pdu_size >= pdu_ctrl->pdu_size)
	{
		/* increase the RETX_COUNT */
		pdu_ctrl->RETX_COUNT ++;
		if(pdu_ctrl->RETX_COUNT >= amtx->maxRetxThreshold)
		{
			u32 ret;
			
			ZLOG_WARN("lcid=%d RETX_COUNT=%u exceed maxRetxThreshold=%u\n", 
					amtx->logical_chan, pdu_ctrl->RETX_COUNT, amtx->maxRetxThreshold);
			if(amtx->max_retx_notify)
			{
				ret = amtx->max_retx_notify(amtx, pdu_ctrl->RETX_COUNT);
				if(ret)
					return ret;
			}
		}
		
		memcpy(buf_ptr, pdu_ctrl->buf_ptr, pdu_ctrl->pdu_size);

		/* remove PDU ctrl from pdu_retx_q */
		dllist_remove(&amtx->pdu_retx_q, &pdu_ctrl->node);
		pdu_ctrl->n_retransmit_seg = 0;
		pdu_ctrl->i_retransmit_seg = 0;
		return pdu_ctrl->pdu_size;
	}

	/* now to build a new PDU segment */
	pdu_segment_ctrl = &pdu_segment;

	pdu_segment_ctrl->n_li = 0;

	segment_head = (rlc_am_pdu_segment_head_t *)buf_ptr;
	segment_head->dc = RLC_AM_DC_DATA_PDU;
	segment_head->sn = pdu_ctrl->sn;
	segment_head->rf = 1;
	segment_head->lsf = 0;
	segment_head->so = seginfo->start_offset;

	remain_pdu_size = pdu_size - sizeof(rlc_am_pdu_segment_head_t);
	if(remain_pdu_size <= 0)
		return 0;

	/* increase the RETX_COUNT */
	pdu_ctrl->RETX_COUNT ++;
	if(pdu_ctrl->RETX_COUNT >= amtx->maxRetxThreshold)
	{
		u32 ret;
		
		ZLOG_WARN("lcid=%d RETX_COUNT=%u exceed maxRetxThreshold=%u\n", 
				amtx->logical_chan, pdu_ctrl->RETX_COUNT, amtx->maxRetxThreshold);
		if(amtx->max_retx_notify)
		{
			ret = amtx->max_retx_notify(amtx, pdu_ctrl->RETX_COUNT);
			if(ret)
				return ret;
		}
	}
	
	/* find start point */
	i_li = 0;
	li_offset = 0;
	while(li_offset<segment_head->so && i_li<pdu_ctrl->n_li)
		li_offset += pdu_ctrl->li_s[i_li++];

	/* here li_offset >= segment_head->so */
	if(li_offset > segment_head->so)
	{
		/* set first LI  */
		pdu_segment_ctrl->n_li = 1;
		tmpv = RLC_MIN(li_offset, seginfo->end_offset);
		pdu_segment_ctrl->li_s[0] = RLC_MIN(remain_pdu_size, tmpv-segment_head->so);
		remain_pdu_size -= pdu_segment_ctrl->li_s[0];
		data_size += pdu_segment_ctrl->li_s[0];

		/* set FI */
		fi[0] = 1;		//NFIRST
		fi[1] = (li_offset != seginfo->start_offset + data_size);
	}
	else /* (li_offset == segment_head->so) */
	{
		/* set FI */
		if(i_li==0)
			fi[0] = pdu_ctrl->fi>>1;
		else
			fi[0] = 0;	//FIRST
		fi[1] = 0;		//set it to 0 temporarily
	}

	/* Build LI: at this point, li_offset equals to the first byte offset of a RLC SDU */
	while(remain_pdu_size > 0 && i_li<pdu_ctrl->n_li && (seginfo->start_offset+data_size<seginfo->end_offset))
	{
		u32 lisize;
		
		if(pdu_segment_ctrl->n_li > 0)
		{
			/* substrace the size of LI from remain_pdu_size */
			if(pdu_segment_ctrl->n_li & 0x01)
				lisize = 2;
			else
				lisize = 1;
				
			remain_pdu_size -= lisize;
			if(remain_pdu_size <= 0)
			{
				/* restore to previous value */
				remain_pdu_size += lisize;
				break;
			}
		}

		tmpv = RLC_MIN(remain_pdu_size, pdu_ctrl->li_s[i_li]);
		pdu_segment_ctrl->li_s[pdu_segment_ctrl->n_li] = RLC_MIN(tmpv, seginfo->end_offset-li_offset);
		remain_pdu_size -= pdu_segment_ctrl->li_s[pdu_segment_ctrl->n_li];
		data_size += pdu_segment_ctrl->li_s[pdu_segment_ctrl->n_li];
		li_offset += pdu_ctrl->li_s[i_li];

		/* update the second bit of FI */
		fi[1] = (li_offset != seginfo->start_offset + data_size);

		pdu_segment_ctrl->n_li ++;
		i_li ++;
	}

	assert(pdu_segment_ctrl->n_li > 0);

	/* Write LI */
	li_ptr = (rlc_li_t *)(segment_head+1);
	rlc_encode_li(li_ptr, pdu_segment_ctrl->n_li, pdu_segment_ctrl->li_s);

	/* Wrtie data */
	data_ptr = (u8 *)li_ptr + rlc_li_len(pdu_segment_ctrl->n_li);
	data_ptr_src = pdu_ctrl->data_ptr;
	memcpy(data_ptr, data_ptr_src+seginfo->start_offset, data_size);
	
	/* set e:1 in head of PDU segment */
	segment_head->e = pdu_segment_ctrl->n_li > 1;

	/* set poll bit */
	rlc_am_tx_update_poll(amtx, 1 /* is_retx */, data_size);
	segment_head->p = rlc_am_tx_deliver_poll(amtx);

	/* if a whole regment is built */
	if(seginfo->start_offset + data_size == seginfo->end_offset)
	{
		/* update LSF */
		segment_head->lsf = seginfo->lsf;
		/* update FI[1] if this is the last segment */
		if(seginfo->lsf)
			fi[1] = (pdu_ctrl->fi & 0x01);
		
		pdu_ctrl->n_retransmit_seg --;
		if(pdu_ctrl->n_retransmit_seg == 0)
		{
			/* remove PDU ctrl from pdu_retx_q */
			dllist_remove(&amtx->pdu_retx_q, &pdu_ctrl->node);
			pdu_ctrl->n_retransmit_seg = 0;
			pdu_ctrl->i_retransmit_seg = 0;
		}
		else
		{
			pdu_ctrl->i_retransmit_seg = RLC_MOD(pdu_ctrl->i_retransmit_seg+1, RLC_SEG_NUM_MAX);
		}
	}
	else	
	{
		/* update start offset */
		seginfo->start_offset += data_size;
		/* must reset pdu_size here */
		seginfo->pdu_size = 0;
	}

	/* set FI to PDU head */
	segment_head->fi = (fi[0] << 1) | fi[1];

	ZLOG_DEBUG("Retx build: lcid=%d sn=%u fi=%u lsf=%u n_li=%u li_s=(%u %u %u ..)\n", 
			amtx->logical_chan, segment_head->sn, segment_head->fi, segment_head->lsf, 
			pdu_segment_ctrl->n_li, 
			pdu_segment_ctrl->li_s[0], pdu_segment_ctrl->li_s[1], pdu_segment_ctrl->li_s[2]);
	
	ZLOG_DEBUG("after Retx build: lcid=%d pdu_size=%u poll=%d VT_A=%u VT_S=%u VT_MS=%u POLL_SN=%u\n", 
			amtx->logical_chan, pdu_size - remain_pdu_size, segment_head->p, 
			amtx->VT_A, amtx->VT_S, amtx->VT_MS, amtx->POLL_SN);
	
	return pdu_size - remain_pdu_size;
}

/***********************************************************************************/
/* Function : rlc_am_tx_build_fresh_pdu                                            */
/***********************************************************************************/
/* Description : - build fresh RLC AM PDU                                          */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   amtx               | i  | RLC AM entity                                       */
/*   buf_ptr            | o  | RLC PDU buffer pointer                              */
/*   pdu_size           | i  | requested PDU Size                                  */
/*   cookie             | i  | cookie used as input of free_pdu()                  */
/*   Return             |    | Size of real RLC PDU                                */
/***********************************************************************************/
int rlc_am_tx_build_fresh_pdu(rlc_entity_am_tx_t *amtx, u8 *buf_ptr, u16 pdu_size, void *cookie)
{
	u32 head_len;
	rlc_li_t *li_ptr;
	u8 *data_ptr;
	u32 data_size;
	rlc_sdu_t *sdu;
	rlc_am_tx_pdu_ctrl_t *pdu_ctrl;

	ZLOG_DEBUG("before build: lcid=%d pdu_size=%u VT_A=%u VT_S=%u VT_MS=%u POLL_SN=%u\n", 
			amtx->logical_chan, pdu_size, amtx->VT_A, amtx->VT_S, amtx->VT_MS, amtx->POLL_SN);
	
	if(pdu_size <= sizeof(rlc_am_pdu_head_t))
		return 0;
		
	if(amtx->sdu_total_size == 0)		//no data in queue
		return 0;

	assert(amtx->n_sdu > 0);
	
	/* if not in transmitting widnow */
	if(!RLC_SN_IN_TRANSMITTING_WIN(amtx->VT_S, amtx->VT_MS, amtx->VT_A, amtx->sn_max + 1))
		return 0;
		
	/* allocate PDU and PDU buffer */
	pdu_ctrl = rlc_am_tx_pdu_ctrl_new();
	if(pdu_ctrl == NULL)
	{
		ZLOG_WARN("rlc_am_pdu_new() out of memory to allocate AM PDU, lcid=%d\n", amtx->logical_chan);
		return 0;
	}

	/* save buffer pointer for later free */
	pdu_ctrl->buf_ptr = buf_ptr;
	pdu_ctrl->buf_cookie = cookie;
	pdu_ctrl->buf_free = amtx->free_pdu;

	/* (1) first round: build LIs */
	sdu = (rlc_sdu_t *)(DLLIST_HEAD(&amtx->sdu_tx_q));
	assert(!DLLIST_IS_HEAD(&amtx->sdu_tx_q, sdu));
	pdu_ctrl->n_li = 0;
	pdu_ctrl->fi = 0;
	//set the first bit of FI
	if(sdu->offset)
		pdu_ctrl->fi |= 0x02;			//NFIRST
	else
		pdu_ctrl->fi &= 0x01;			//FIRST

	head_len = 2;
	pdu_ctrl->n_li = rlc_build_li_from_sdu(pdu_size, head_len, &amtx->sdu_tx_q, pdu_ctrl->li_s);

	if(pdu_ctrl->n_li == 0)
	{
		ZLOG_WARN("RLC build LI: number of LI is 0, lcid=%d.\n", amtx->logical_chan);
		rlc_am_tx_pdu_ctrl_free(pdu_ctrl);
		return 0;
	}

	/* save PDU */
	amtx->txpdu[amtx->VT_S] = pdu_ctrl;
	
	assert(pdu_ctrl->n_li <= amtx->n_sdu);
	assert(pdu_ctrl->n_li <= RLC_LI_NUM_MAX);
	
	/* (2) second round: write LIs and data */
	li_ptr = (rlc_li_t *)(pdu_ctrl->buf_ptr + head_len);
	rlc_encode_li(li_ptr, pdu_ctrl->n_li, pdu_ctrl->li_s);
	
	data_ptr = (u8 *)li_ptr + rlc_li_len(pdu_ctrl->n_li);
	pdu_ctrl->data_ptr = data_ptr;
	data_size = rlc_encode_sdu(data_ptr, pdu_ctrl->n_li, pdu_ctrl->li_s, &amtx->sdu_tx_q);
	data_ptr += (data_size & 0xFFFF);
	amtx->sdu_total_size -= (data_size & 0xFFFF);
	amtx->n_sdu -= (data_size >> 16);

	assert(amtx->sdu_total_size >= 0);
	assert(amtx->n_sdu >= 0);

	//set the second bit of FI
	if(DLLIST_EMPTY(&amtx->sdu_tx_q))
		pdu_ctrl->fi &= 0x02;			//LAST
	else{
		sdu = (rlc_sdu_t *)amtx->sdu_tx_q.next;
		if(sdu->offset)
			pdu_ctrl->fi |= 0x01;			//NLAST
		else
			pdu_ctrl->fi &= 0x02;			//LAST
	}

	/* set RLC PDU header */
	rlc_am_pdu_head_t *pdu_head;
	
	pdu_head = (rlc_am_pdu_head_t *)pdu_ctrl->buf_ptr;
	pdu_head->dc = 1;		//data
	pdu_head->rf = 0;
	pdu_head->e = (pdu_ctrl->n_li > 1);
	pdu_head->fi = pdu_ctrl->fi;
	pdu_head->sn = amtx->VT_S;
	
	pdu_ctrl->sn = pdu_head->sn;			//save sn to pdu_ctrl
	pdu_ctrl->pdu_size = data_ptr-pdu_ctrl->buf_ptr;
	
	/* update AM Tx counter */
	amtx->VT_S++;
	amtx->VT_S &= amtx->sn_max;

	/* set poll bit */
	rlc_am_tx_update_poll(amtx, 0, data_size);
	pdu_head->p = rlc_am_tx_deliver_poll(amtx);

	ZLOG_DEBUG("fresh build: lcid=%d fi=%u n_li=%u li_s=(%u %u %u ..)\n", 
			amtx->logical_chan, pdu_head->fi, pdu_ctrl->n_li, 
			pdu_ctrl->li_s[0], pdu_ctrl->li_s[1], pdu_ctrl->li_s[2]);
	
	ZLOG_DEBUG("after build: lcid=%d pdu_size=%u poll=%d VT_A=%u VT_S=%u VT_MS=%u POLL_SN=%u\n", 
			amtx->logical_chan, data_ptr-pdu_ctrl->buf_ptr, pdu_head->p, 
			amtx->VT_A, amtx->VT_S, amtx->VT_MS, amtx->POLL_SN);
	
	return pdu_ctrl->pdu_size;
}

/***********************************************************************************/
/* Function : rlc_am_tx_build_pdu                                                  */
/***********************************************************************************/
/* Description : - build RLC AM PDU                                                */
/*               - Status PDU first, ReTx PDU second, fresh PDU last               */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   amtx               | i  | RLC AM entity                                       */
/*   buf_ptr            | o  | RLC PDU buffer pointer                              */
/*   pdu_size           | i  | requested PDU Size                                  */
/*   cookie             | i  | cookie used as input of free_pdu()                  */
/*   pdu_size           | o  | PDU type: status PDU, ReTx PDU or fresh PDU         */
/*   Return             |    | Size of real RLC PDU                                */
/***********************************************************************************/
int rlc_am_tx_build_pdu(rlc_entity_am_tx_t *amtx, u8 *buf_ptr, u16 pdu_size, void *cookie, u32 *pdu_type)
{
	u32 retx_pdu_size;
	u32 status_pdu_size;

	if(buf_ptr == NULL || pdu_size == 0)
		return 0;

	if(pdu_type == NULL)
		return 0;

	if(amtx == NULL)
		return 0;
	
	ZLOG_DEBUG("request RLC AM to build PDU: lcid=%d buf_ptr=%p size=%u.\n", amtx->logical_chan, buf_ptr, pdu_size);
	
	/* Step1: first, build Status PDU */
	status_pdu_size = rlc_am_tx_build_status_pdu(amtx, amtx->amrx, buf_ptr, pdu_size);
	if(status_pdu_size > 0)
	{
		*pdu_type = RLC_AM_CTRL_PDU;
		return status_pdu_size;
	}
	
	/* Step2: second, build PDU segment from ReTx PDU list */
	retx_pdu_size = rlc_am_tx_build_retx_pdu(amtx, buf_ptr, pdu_size, cookie);
	if(retx_pdu_size > 0)
	{
		*pdu_type = RLC_AM_RETX_PDU;
		return retx_pdu_size;
	}
	
	/* Step3: third, build fresh PDU */
	*pdu_type = RLC_AM_FRESH_PDU;
	return rlc_am_tx_build_fresh_pdu(amtx, buf_ptr, pdu_size, cookie);
}

/* trigger status pdu */
int rlc_am_trigger_status_report(rlc_entity_am_rx_t *amrx, rlc_entity_am_tx_t *amtx, u16 sn, int forced)
{
	u16 sn_fs = amrx->sn_max+1;
	
/*
 Triggers to initiate STATUS reporting include:
 -	 Polling from its peer AM RLC entity:
   - When a RLC data PDU with SN = x and the P field set to "1" is received from lower layer, the receiving side of an AM RLC entity shall:
	 -	 if the PDU is to be discarded as specified in subclause 5.1.3.2.2; or  (forced)
	 -	 if x < VR(MS) or x >= VR(MR):
	   - trigger a STATUS report;
	 -	 else:
	   - delay triggering the STATUS report until x < VR(MS) or x >= VR(MR).
 NOTE 1: This ensures that the RLC Status report is transmitted after HARQ reordering.
 -	 Detection of reception failure of an RLC data PDU:
   - The receiving side of an AM RLC entity shall trigger a STATUS report when t-Reordering expires.  (forced)
*/
	if(!forced)
	{
		if(!RLC_SN_LESS(sn, amrx->VR_MS, sn_fs) && !RLC_SN_LESSTHAN(amrx->VR_MR,sn,sn_fs))
			return 0;
	}

	/* set trigger flags */
	amtx->status_pdu_triggered = 1;

	return 0;
}

/* new RLC AM Rx PDU control structure */
rlc_am_rx_pdu_ctrl_t *rlc_am_rx_pdu_ctrl_new()
{
	rlc_am_rx_pdu_ctrl_t *pdu_ctrl;

	pdu_ctrl = (rlc_am_rx_pdu_ctrl_t *)FASTALLOC(g_mem_am_pdu_rx_base);
	if(pdu_ctrl)
	{
		pdu_ctrl->delivery_offset = 0;
		pdu_ctrl->is_intact = 0;
		dllist_init(&pdu_ctrl->rx_segq);
	}

	return pdu_ctrl;
}

/* free RLC AM Rx PDU control structure */
void rlc_am_rx_pdu_ctrl_free(rlc_am_rx_pdu_ctrl_t *pdu_ctrl)
{
	rlc_am_pdu_segment_t *pdu_segment;
	
	/* free all segments */
	while(!DLLIST_EMPTY(&pdu_ctrl->rx_segq))
	{
		pdu_segment = (rlc_am_pdu_segment_t *)DLLIST_HEAD(&pdu_ctrl->rx_segq);
		dllist_remove(&pdu_ctrl->rx_segq, &pdu_segment->node);
		
		rlc_am_pdu_segment_free(pdu_segment);
	}

	/* free pdu control */
	FASTFREE(g_mem_am_pdu_rx_base, pdu_ctrl);
}

/* free received PDU segment */
void rlc_am_rxseg_free(void *data, void *cookie)
{
	assert(cookie);
	rlc_am_pdu_segment_free((rlc_am_pdu_segment_t *)cookie);
}

/* dump RLC AM Rx PDU control structure */
void rlc_am_rx_pdu_ctrl_dump(rlc_am_rx_pdu_ctrl_t *pdu_ctrl)
{
	int idx;
	rlc_am_pdu_segment_t *pdu_segment;
	
	if(pdu_ctrl == NULL)
		return;

	ZLOG_INFO("delivery_offset=%u is_intac=%u\n", pdu_ctrl->delivery_offset, pdu_ctrl->is_intact);

	idx = 0;
	pdu_segment = (rlc_am_pdu_segment_t *)DLLIST_HEAD(&pdu_ctrl->rx_segq);
	while(!DLLIST_IS_HEAD(&pdu_ctrl->rx_segq, pdu_segment))
	{
		ZLOG_INFO("  %d: sn=%u so=(%u,%u) fi=%u lsf=%u n_li=%u li_s=(%u %u %u..)\n", 
				idx, pdu_segment->sn, pdu_segment->start_offset, pdu_segment->end_offset,
				pdu_segment->fi, pdu_segment->lsf, pdu_segment->n_li,
				pdu_segment->li_s[0], pdu_segment->li_s[1], pdu_segment->li_s[2]);

		pdu_segment = (rlc_am_pdu_segment_t *)pdu_segment->node.next;
	}
}

/* process STATUS PDU */
int rlc_am_rx_process_status_pdu(rlc_entity_am_rx_t *amrx, u8 *buf_ptr, u32 buf_len)
{
	rlc_am_status_pdu_head_t *pdu_head = (rlc_am_status_pdu_head_t *)buf_ptr;
	nacksn_info_t ninfo[MAXINFO_NUM];
	u16 ack_sn, sn, nack_sn;
	int i, n=0;
	u32 bit_offset = 15;
	u32 e1;
	rlc_am_tx_pdu_ctrl_t *pdu_ctrl;
	rlc_entity_am_tx_t *amtx = amrx->amtx;
	u32 maxso;

	ack_sn = pdu_head->ack_sn;
	e1 = pdu_head->e1;

	/* check pdu: VT_A <= ack_sn <= VT_S */
	if(RLC_SN_LESS(amtx->VT_S, ack_sn, (RLC_SN_MAX_10BITS+1)))
	{
		ZLOG_WARN("wrong ACK_SN:%u > VT_S:%u, lcid=%d\n", ack_sn, amtx->VT_S, amtx->logical_chan);
		return -1;
	}

	if(RLC_SN_LESS(ack_sn, amtx->VT_A,  (RLC_SN_MAX_10BITS+1)))
	{
		ZLOG_WARN("wrong ACK_SN:%u < VT_A:%u, lcid=%d\n", ack_sn, amtx->VT_A, amtx->logical_chan);
		return -1;
	}

	/* check pdu: pdu length */
	if((bit_offset+7)/8 > buf_len)
	{
		ZLOG_WARN("invalid buf_len=%u, bit_offset=%u, lcid=%d\n", buf_len, bit_offset, amrx->logical_chan);
		return -1;
	}
	
	/* 1) extract nack info */
	while(e1 && n<MAXINFO_NUM)
	{
		bitcpy((unsigned long *)&ninfo[n].nacksn, 0, (const unsigned long *)buf_ptr, bit_offset, 12);
		bit_offset += 12;
		if(bit_offset >= 32)
		{
			buf_ptr += 4;
			bit_offset -= 32;
		}
		e1 = ninfo[n].nacksn.e1;
		nack_sn = ninfo[n].nacksn.nack_sn;

		if(ninfo[n].nacksn.e2)
		{
			bitcpy((unsigned long *)&ninfo[n].so, 0, (const unsigned long *)buf_ptr, bit_offset, 30);
			bit_offset += 30;
			if(bit_offset >= 32)
			{
				buf_ptr += 4;
				bit_offset -= 32;
			}
		}

		/* check length of PDU */
		if((bit_offset+7)/8 > buf_len)
		{
			ZLOG_WARN("invalid buf_len=%u, bit_offset=%u, lcid=%d\n", buf_len, bit_offset, amrx->logical_chan);
			return -1;
		}

/* check nack_sn */
/*
	When receiving a negative acknowledgement for an AMD PDU or a portion of an AMD PDU by a STATUS PDU 
	from its peer AM RLC entity, the transmitting side of the AM RLC entity shall:
	-	if the SN of the corresponding AMD PDU falls within the range VT(A) <= SN < VT(S):
	     -	consider the AMD PDU or the portion of the AMD PDU for which a negative acknowledgement was 
	             received for retransmission.
*/
		
		if(RLC_SN_LESSTHAN(amtx->VT_S, nack_sn, (RLC_SN_MAX_10BITS+1)))
		{
			ZLOG_WARN("invalid NACK_SN: must NACK_SN=%u < VT(S)=%u, lcid=%d\n", nack_sn, amtx->VT_S, amrx->logical_chan);
			return -1;
		}
		
		if(RLC_SN_LESS(nack_sn, amtx->VT_A, (RLC_SN_MAX_10BITS+1)))
		{
			ZLOG_WARN("invalid NACK_SN: must VT(A)=%u <= NACK_SN=%u, lcid=%d\n", amtx->VT_A, nack_sn, amrx->logical_chan);
			return -1;
		}
		
		if(amtx->txpdu[nack_sn] == NULL)
		{
			ZLOG_WARN("invalid NACK_SN=%u, PDU=NULL, lcid=%d.\n", nack_sn, amrx->logical_chan);
			return -1;
		}

		/* check sostart and soend */
		if(ninfo[n].nacksn.e2)
		{
			pdu_ctrl = amtx->txpdu[nack_sn];
			assert(pdu_ctrl->data_ptr > pdu_ctrl->buf_ptr);
			maxso = pdu_ctrl->pdu_size - (pdu_ctrl->data_ptr - pdu_ctrl->buf_ptr);
			
			if(ninfo[n].so.sostart >= maxso)
			{
				ZLOG_WARN("invalid sostart=%u >= maxso=%u, lcid=%d\n", ninfo[n].so.sostart, maxso, amrx->logical_chan);
				return -1;
			}

			if(ninfo[n].so.soend > maxso && ninfo[n].so.soend != RLC_AM_SO_END)
			{
				ZLOG_WARN("invalid soend=%u > maxso=%u, lcid=%d\n", ninfo[n].so.soend, maxso, amrx->logical_chan);
				return -1;
			}

			if(ninfo[n].so.soend <= ninfo[n].so.sostart)
			{
				ZLOG_WARN("invalid sostart=%u and soend=%u, lcid=%d\n", ninfo[n].so.sostart, ninfo[n].so.soend, amrx->logical_chan);
				return -1;
			}
		}
		
		n++;
	}

	
	assert(n<MAXINFO_NUM);
	
	/* check if nack_sn is in ascending order of sn */
	ZLOG_DEBUG("status PDU: lcid=%d ack_sn=%u n_nack_sn=%u\n", amrx->logical_chan, ack_sn, n);
	for(i=0; i<n; i++)
	{
		ZLOG_DEBUG("  nack_sn=%u so_present=%u so_start=%u so_end=%u\n",
				ninfo[i].nacksn.nack_sn, ninfo[i].nacksn.e2, ninfo[i].so.sostart, ninfo[i].so.soend);

		if(i>0)
		{
			if(!RLC_SN_LESSTHAN(ninfo[i-1].nacksn.nack_sn, ninfo[i].nacksn.nack_sn, (RLC_SN_MAX_10BITS+1)))
			{
				ZLOG_WARN("NACK_SN must be in ascending order of SN: nack_sn[%u]=%u nack_sn[%u]=%u\n",
						i-1, ninfo[i-1].nacksn.nack_sn, i, ninfo[i].nacksn.nack_sn);
				return -1;
			}
		}
	}
	
	/* 2) process nack info */
	sn = amtx->VT_A;
	i = 0;
	while(sn != ack_sn)
	{
		pdu_ctrl = amtx->txpdu[sn];
		if((i < n) && (sn == ninfo[i].nacksn.nack_sn))
		{
			/* reset retransmit_seg[] */
			pdu_ctrl->n_retransmit_seg = 0;
			pdu_ctrl->i_retransmit_seg = 0;

			/* add pdu_ctrl to ReTx queue: ascending on SN */
			if(pdu_ctrl->node.next == NULL)
				rlc_am_tx_add_retx(amtx, pdu_ctrl);

			maxso = pdu_ctrl->pdu_size - (pdu_ctrl->data_ptr - pdu_ctrl->buf_ptr);

			do{
				if(ninfo[i].nacksn.e2)
				{	//partly nack
					pdu_ctrl->retransmit_seg[pdu_ctrl->n_retransmit_seg].lsf = (ninfo[i].so.soend >= maxso);
					pdu_ctrl->retransmit_seg[pdu_ctrl->n_retransmit_seg].start_offset = ninfo[i].so.sostart;
					if(pdu_ctrl->retransmit_seg[pdu_ctrl->n_retransmit_seg].lsf)
						pdu_ctrl->retransmit_seg[pdu_ctrl->n_retransmit_seg].end_offset = maxso;
					else
						pdu_ctrl->retransmit_seg[pdu_ctrl->n_retransmit_seg].end_offset = ninfo[i].so.soend;
					pdu_ctrl->retransmit_seg[pdu_ctrl->n_retransmit_seg].pdu_size = 0;
					pdu_ctrl->n_retransmit_seg ++;
				}
				else
				{	//fullly nack					
					pdu_ctrl->n_retransmit_seg = 1;
					pdu_ctrl->retransmit_seg[0].lsf = 1;
					pdu_ctrl->retransmit_seg[0].start_offset = 0;
					pdu_ctrl->retransmit_seg[0].end_offset = maxso;
					pdu_ctrl->retransmit_seg[0].pdu_size = pdu_ctrl->pdu_size;
				}

				i++;
			}while((i < n) && (sn == ninfo[i].nacksn.nack_sn));
		}
		else
		{
/*
TODO:
	-	if positive acknowledgements have been received for all AMD PDUs associated with a transmitted RLC SDU:
	     -	send an indication to the upper layers of successful delivery of the RLC SDU.
*/
			if(pdu_ctrl)
			{
				rlc_am_tx_pdu_ctrl_free(amtx->txpdu[sn]);
				amtx->txpdu[sn] = NULL;
			}
		}

		sn = RLC_MOD(sn+1, RLC_SN_MAX_10BITS+1);
	}

	/*
	When receiving a positive acknowledgement for an AMD PDU with SN = VT(A), the transmitting side of an AM RLC entity shall:
	-	set VT(A) equal to the SN of the AMD PDU with the smallest SN, whose SN falls within the range VT(A) <= SN <= VT(S) 
	      and for which a positive acknowledgment has not been received yet.
	     */
	if(n > 0)
		amtx->VT_A = ninfo[0].nacksn.nack_sn;	//here assume the first nack_sn is the minimum one, don't check on it
	else
		amtx->VT_A = ack_sn;

	/* also need to update VT(MS) */
	amtx->VT_MS = RLC_MOD(amtx->VT_A + amtx->AM_Window_Size, RLC_SN_MAX_10BITS+1);
	
/*
	Upon reception of a STATUS report from the receiving RLC AM entity the transmitting side of an AM RLC entity shall:
	-	if the STATUS report comprises a positive or negative acknowledgement for the RLC data PDU with sequence number equal to POLL_SN:
		 -	if t-PollRetransmit is running:
				-	stop and reset t-PollRetransmit.
*/
	if(RLC_SN_LESS(amtx->POLL_SN, ack_sn, (RLC_SN_MAX_10BITS+1)))
	{
		if(rlc_timer_is_running(&amtx->t_PollRetransmit))
		{
			ZLOG_DEBUG("stop timer t_PollRetransmit: lcid=%d\n", amtx->logical_chan);
			rlc_timer_stop(&amtx->t_PollRetransmit);
		}
	}
	
	return 0;
}


/* Place received PDU in Rx buffer 
    FIXME: For briefness, the partly duplicated PDU or PDU segments are simplely discarded. 
    This treatment is not fully aligned to 36.322 and need to be improved. 
  */
rlc_am_rx_pdu_ctrl_t *rlc_am_place_pdu_in_rxbuf(rlc_entity_am_rx_t *amrx, u32 sn, rlc_am_pdu_head_t *pdu_hdr, u32 pdu_size, void *cookie)
{
	rlc_am_rx_pdu_ctrl_t *pdu_ctrl;
	rlc_am_pdu_segment_t *pdu_segment, *pdu_segtmp;
	rlc_am_pdu_segment_head_t *seg_hdr;
	rlc_li_t *li_ptr;
	s32 length;
	int duplicated = 0;
	u16 so, eo;
	
	seg_hdr = (rlc_am_pdu_segment_head_t *)pdu_hdr;

	/* parse LI */
	if(pdu_hdr->rf)
	{
		length = pdu_size - sizeof(rlc_am_pdu_segment_head_t);
		li_ptr = (rlc_li_t *)(seg_hdr+1);
	}
	else
	{
		length = pdu_size - sizeof(rlc_am_pdu_head_t);
		li_ptr = (rlc_li_t *)(pdu_hdr+1);
	}

	/* create a new segment control: just a little bit waste if this pdu is duplicated */
	pdu_segment = rlc_am_pdu_segment_new();
	if(pdu_segment == NULL)
	{
		ZLOG_ERR("out of memory for new PDU segment: lcid=%d.\n", amrx->logical_chan);
		return NULL;
	}
	
 	pdu_segment->n_li = rlc_parse_li(pdu_hdr->e, li_ptr, length, &pdu_segment->data_ptr, pdu_segment->li_s);
 	if(pdu_segment->n_li <= 0 || pdu_segment->n_li > RLC_LI_NUM_MAX)
 	{
 		ZLOG_WARN("wrong AM PDU: lcid=%d n_li=%d, sn=%u, size=%d.\n", 
 				amrx->logical_chan, (int)pdu_segment->n_li, sn, pdu_size);
		rlc_am_pdu_segment_free(pdu_segment);
 		return NULL;
 	}

	pdu_segment->buf_len = pdu_size;
	pdu_segment->buf_ptr = (u8 *)pdu_hdr;
	pdu_segment->buf_cookie = cookie;
	pdu_segment->fi = pdu_hdr->fi;
	pdu_segment->sn = sn;
	if(pdu_hdr->rf)
	{
		pdu_segment->start_offset = seg_hdr->so;
		pdu_segment->end_offset = pdu_segment->start_offset + pdu_size-(pdu_segment->data_ptr - (u8 *)pdu_hdr);
		pdu_segment->lsf = seg_hdr->lsf;
	}
	else
	{
		pdu_segment->start_offset = 0;
		pdu_segment->end_offset = pdu_segment->start_offset + pdu_size-(pdu_segment->data_ptr - (u8 *)pdu_hdr);
		pdu_segment->lsf = 1;
	}
	
	/* get old pdu control pointer */
	pdu_ctrl = amrx->rxpdu[sn];
	
	if(pdu_hdr->rf == 0)
	{
		/* Rx a PDU */
		if(pdu_ctrl)
		{
			ZLOG_WARN("RLC AM PDU duplicated: lcid=%d SN=%d.\n", amrx->logical_chan, sn);
			rlc_am_pdu_segment_free(pdu_segment);
			return NULL;
		}

		/* now allocate new PDU control */
		pdu_ctrl = (rlc_am_rx_pdu_ctrl_t *)rlc_am_rx_pdu_ctrl_new();
		if(pdu_ctrl == NULL)
		{
			ZLOG_ERR("out of memory for new AM PDU: lcid=%d.\n", amrx->logical_chan);
			rlc_am_pdu_segment_free(pdu_segment);
			return NULL;
		}

		pdu_segment->free = amrx->free_pdu;
		RLC_REF(pdu_segment);
		dllist_append(&pdu_ctrl->rx_segq, &pdu_segment->node);
		pdu_ctrl->is_intact = 1;
		
		/* place in Rx buf */
		amrx->rxpdu[sn] = pdu_ctrl;
		return pdu_ctrl;
	}
	else
	{
		/* Rx a PDU segment */
		if(pdu_ctrl == NULL)
		{
			/* allocate new PDU control */
			pdu_ctrl = (rlc_am_rx_pdu_ctrl_t *)rlc_am_rx_pdu_ctrl_new();
			if(pdu_ctrl == NULL)
			{
				ZLOG_ERR("out of memory for new AM PDU: lcid=%d.\n", amrx->logical_chan);
				rlc_am_pdu_segment_free(pdu_segment);
				return NULL;
			}

			pdu_segment->free = amrx->free_pdu;
			RLC_REF(pdu_segment);
			dllist_append(&pdu_ctrl->rx_segq, &pdu_segment->node);
			pdu_ctrl->is_intact = 0;
			
			/* place in Rx buf */
			amrx->rxpdu[sn] = pdu_ctrl;
			return pdu_ctrl;
		}
		else
		{
			/* new pdu segment recieved but not the first arrival */
			if(pdu_ctrl->is_intact)
			{
				duplicated = 1;
			}
			else
			{
				/* must be a segment: insert into rx_segq if not duplicated */
				so = pdu_segment->start_offset;
				eo = pdu_segment->end_offset;
				pdu_segtmp = (rlc_am_pdu_segment_t *)DLLIST_HEAD(&pdu_ctrl->rx_segq);
				while(!DLLIST_IS_HEAD(&pdu_ctrl->rx_segq, pdu_segtmp))
				{
					if(pdu_segtmp->start_offset <= so && pdu_segtmp->end_offset > so)
					{
						duplicated = 1;
						break;
					}
					else if(pdu_segtmp->start_offset > so)
					{
						if(pdu_segtmp->start_offset < eo)
							duplicated = 1;
						break;
					}

					pdu_segtmp = (rlc_am_pdu_segment_t *)pdu_segtmp->node.next;
				}

				if(duplicated)
				{
					ZLOG_WARN("RLC AM PDU Segment duplicated: lcid=%d SN=%d rf=%d so=%d eo=%d.\n", 
							amrx->logical_chan, sn, pdu_hdr->rf, so, eo);
					rlc_am_pdu_segment_free(pdu_segment);
					return NULL;
				}
				
				/* insert pdu_segment before pdu_segtmp */
				pdu_segment->free = amrx->free_pdu;
				RLC_REF(pdu_segment);
				pdu_segment->node.next = &pdu_segtmp->node;
				pdu_segment->node.prev = pdu_segtmp->node.prev;
				(pdu_segtmp->node.prev)->next = &pdu_segment->node;
				pdu_segtmp->node.prev = &pdu_segment->node;

				/* loop list to check intact */
				pdu_segtmp = (rlc_am_pdu_segment_t *)DLLIST_HEAD(&pdu_ctrl->rx_segq);
				so = 0;
				while(!DLLIST_IS_HEAD(&pdu_ctrl->rx_segq, pdu_segtmp))
				{
					if(pdu_segtmp->start_offset != so)
						break;

					so = pdu_segtmp->end_offset;
					if(pdu_segtmp->lsf)
					{
						pdu_ctrl->is_intact = 1;
						break;
					}

					pdu_segtmp = (rlc_am_pdu_segment_t *)pdu_segtmp->node.next;
				}
			}
		}
	}
	
	return pdu_ctrl;
}

/* process data pdu */
int rlc_am_rx_process_data_pdu(rlc_entity_am_rx_t *amrx, u8 *buf_ptr, u32 buf_len, void *cookie)
{
	u16 sn, sn_fs;
	rlc_am_rx_pdu_ctrl_t *pdu_ctrl = NULL;
	rlc_am_pdu_head_t *pdu_hdr;
	int discard = 0;
	
	assert(buf_ptr);
	assert(buf_len > 0);
	
	pdu_hdr = (rlc_am_pdu_head_t *)buf_ptr;
	sn = pdu_hdr->sn;
	sn_fs = RLC_SN_MAX_10BITS+1;

	ZLOG_DEBUG("RLC AM Counters before processing PDU: lcid=%d sn=%u fi=%u VR_R=%u VR_X=%u VR_H=%u VR_MR=%u VR_MS=%u.\n", 
		amrx->logical_chan, sn, pdu_hdr->fi, amrx->VR_R, amrx->VR_X, amrx->VR_H, amrx->VR_MR, amrx->VR_MS);
		
/*
 -	if x falls outside of the receiving window (VR(R) <= SN < VR(MR)); or
 -	if byte segment numbers y to z of the AMD PDU with SN = x have been received before:
      -	discard the received RLC data PDU;
 -	else:
      -	 place the received RLC data PDU in the reception buffer;
      -	 if some byte segments of the AMD PDU contained in the RLC data PDU have been received before:
              - discard the duplicate byte segments.
 */
 	if(!RLC_SN_IN_RECEIVING_WIN(sn, amrx->VR_MR, amrx->VR_R, sn_fs))
 	{
 		ZLOG_WARN("sn is out of receiving window: lcid=%d sn=%d VR_R=%d VR_MR=%d\n", 
 				amrx->logical_chan, sn, amrx->VR_R, amrx->VR_MR);
		discard = 1;
 	}
	else
	{
		/* To make things simple, just discard whole PDU/segment, not only the duplicate byte segments */
		pdu_ctrl = rlc_am_place_pdu_in_rxbuf(amrx, sn, pdu_hdr, buf_len, cookie);
		if(pdu_ctrl == NULL)
		{
			ZLOG_NOTICE("sn has been partly recieved: lcid=%d sn=%u\n", amrx->logical_chan, sn);
			discard = 1;
		}
	}

/* status report handling */
	if(pdu_hdr->p)
	{
		rlc_am_trigger_status_report(amrx, amrx->amtx, sn, discard);
		ZLOG_DEBUG("Rx poll_bit=1 and status_pdu_triggered=%u, lcid=%d\n", 
				amrx->amtx->status_pdu_triggered, amrx->logical_chan);

		/* process t_StatusPdu */
		if(rlc_timer_is_running(&amrx->t_StatusPdu))
			rlc_timer_stop(&amrx->t_StatusPdu);

		if(!amrx->amtx->status_pdu_triggered)
			rlc_timer_start(&amrx->t_StatusPdu);
	}

	if(discard)
	{
		return -1;
	}

	/*
	 *	Actions when a RLC data PDU is placed in the reception buffer:
	 */

/*
-	if x >= VR(H)
    -	update VR(H) to x+ 1;
*/
	if(RLC_SN_LESSTHAN(amrx->VR_H, sn, sn_fs))
		amrx->VR_H = (sn + 1) & amrx->sn_max;
		
/*
-	if all byte segments of the AMD PDU with SN = VR(MS) are received:
    -	update VR(MS) to the SN of the first AMD PDU with SN > current VR(MS) for which not all byte segments 
        have been received;
*/
	if(sn == amrx->VR_MS && pdu_ctrl->is_intact)
	{
		do{
			sn = RLC_MOD(sn+1, sn_fs);
			pdu_ctrl = amrx->rxpdu[sn];
		}while(pdu_ctrl && pdu_ctrl->is_intact);
		
		amrx->VR_MS = sn;
	}

/*
-	if x = VR(R):
    -	if all byte segments of the AMD PDU with SN = VR(R) are received:
        -	update VR(R) to the SN of the first AMD PDU with SN > current VR(R) for which not all byte segments 
            have been received;
        -	update VR(MR) to the updated VR(R) + AM_Window_Size;
    -	reassemble RLC SDUs from any byte segments of AMD PDUs with SN that falls outside of the receiving window
        and in-sequence byte segments of the AMD PDU with SN = VR(R), remove RLC headers when doing so and deliver 
        the reassembled RLC SDUs to upper layer in sequence if not delivered before;
*/
	sn = pdu_hdr->sn;
	if(sn == amrx->VR_R)
	{
		do{
			ZLOG_DEBUG("Try to assembly RLC AM SDU from PDU: lcid=%d sn=%u.\n", amrx->logical_chan, sn);
			rlc_am_rx_assemble_sdu(&amrx->sdu_assembly_q, amrx->rxpdu[sn]);
			if(amrx->rxpdu[sn]->is_intact)
			{
				/* free rxpdu[sn] */
				rlc_am_rx_pdu_ctrl_free(amrx->rxpdu[sn]);
				amrx->rxpdu[sn] = NULL;

				/* to next pdu */
				sn = RLC_MOD((sn + 1), sn_fs);
			}
			else
			{
				break;
			}
		}while(amrx->rxpdu[sn]);
		
		amrx->VR_R = RLC_MOD(sn, sn_fs);
		amrx->VR_MR = RLC_MOD(sn+amrx->AM_Window_Size, sn_fs);
	}

/* 
-	if t-Reordering is running:
    -	if VR(X) = VR(R); or
    -	if VR(X) falls outside of the receiving window and VR(X) is not equal to VR(MR):
        -	stop and reset t-Reordering;
*/

	if(ptimer_is_running(&amrx->t_Reordering))
	{
		if((amrx->VR_X == amrx->VR_R) ||
			(!RLC_SN_IN_RECEIVING_WIN(amrx->VR_X, amrx->VR_MR, amrx->VR_R, sn_fs) && amrx->VR_X != amrx->VR_MR))
		{
			ZLOG_DEBUG("RLC AM stop timer t_Reordering: lcid=%d.\n", amrx->logical_chan);
			rlc_timer_stop(&amrx->t_Reordering);
		}
	}

/*
 -	if t-Reordering is not running (includes the case when t-Reordering is stopped due to actions above):
    -	if VR(H) > VR(R):
        -	start t-Reordering;
        -	set VR(X) to VR(H).
*/
	if(!ptimer_is_running(&amrx->t_Reordering))
	{
		if(RLC_SN_LESS(amrx->VR_R, amrx->VR_H, sn_fs))
		{
			ZLOG_DEBUG("RLC AM start timer t_Reordering: lcid=%d.\n", amrx->logical_chan);
			rlc_timer_start(&amrx->t_Reordering);
			
			amrx->VR_X = amrx->VR_H;
		}
	}

	ZLOG_DEBUG("RLC AM Counters after processing PDU: lcid=%d VR_R=%u VR_X=%u VR_H=%u VR_MR=%u VR_MS=%u.\n", 
		amrx->logical_chan, amrx->VR_R, amrx->VR_X, amrx->VR_H, amrx->VR_MR, amrx->VR_MS);
	
	/* deliver intact SDU to upper */
	rlc_am_rx_delivery_sdu(amrx, &amrx->sdu_assembly_q);
	
	return 0;
}

/* Process received RLC AM PDU
 * return: on success return 0; on error return non-zero
 */
int rlc_am_rx_process_pdu(rlc_entity_am_rx_t *amrx, u8 *buf_ptr, u32 buf_len, void *cookie)
{
	rlc_am_pdu_head_t *pdu_hdr;
	int ret;
	
	/* parse header */
	pdu_hdr = (rlc_am_pdu_head_t *)buf_ptr;
	if(pdu_hdr->dc == RLC_AM_DC_CTRL_PDU)
	{
		//control PDU: only status PDU now
		ret = rlc_am_rx_process_status_pdu(amrx, buf_ptr, buf_len);
		amrx->free_pdu(buf_ptr, cookie);
		return ret;
	}
	else
	{
		//data PDU
		ret = rlc_am_rx_process_data_pdu(amrx, buf_ptr, buf_len, cookie);
		if(ret)
			amrx->free_pdu(buf_ptr, cookie);
		return ret;
	}
}

/***********************************************************************************/
/* Function : rlc_am_rx_assemble_sdu                                               */
/***********************************************************************************/
/* Description : - Assemble one PDU to SDUs                                        */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   sdu_assembly_q     | o  | assemblied RLC SDU is put here                      */
/*   pdu_ctrl           | i  | PDU control info pointer                            */
/*   Return             |    | 0 is success                                        */
/***********************************************************************************/
int rlc_am_rx_assemble_sdu(dllist_node_t *sdu_assembly_q, rlc_am_rx_pdu_ctrl_t *pdu_ctrl)
{
	rlc_sdu_t *sdu;
	u32 li_idx, li_len;
	u32 is_first, is_last;
	rlc_am_pdu_segment_t *pdu_segment;

	/* get the PDU segment corresponding to delivery offset */
	pdu_segment = (rlc_am_pdu_segment_t *)DLLIST_HEAD(&pdu_ctrl->rx_segq);
	while(!DLLIST_IS_HEAD(&pdu_ctrl->rx_segq, pdu_segment))
	{
		if(pdu_segment->start_offset < pdu_ctrl->delivery_offset)
			pdu_segment = (rlc_am_pdu_segment_t *)pdu_segment->node.next;
		else
			break;
	}

	/* assemble pdu_segment in pdu_ctrl */
	while(!DLLIST_IS_HEAD(&pdu_ctrl->rx_segq, pdu_segment))
	{
		if(pdu_segment->start_offset != pdu_ctrl->delivery_offset)
		{
			ZLOG_DEBUG("no valid segment: start_offset=%d but delivery_offset=%d.\n", 
				pdu_segment->start_offset, pdu_ctrl->delivery_offset);
			return 0;
		}

		li_idx = 0;
		li_len = 0;
		
		/* RLC_FI_FIRST_LAST	00
		RLC_FI_FIRST_NLAST	01
		RLC_FI_NFIRST_LAST	10
		RLC_FI_NFIRST_NLAST	11 */
		is_first = !(pdu_segment->fi & 0x02);
		is_last = !(pdu_segment->fi & 0x01);

		/* first LI */
		if(is_first)
		{
			//check last SDU's state
			sdu = (rlc_sdu_t *)DLLIST_TAIL(sdu_assembly_q);
			if(!DLLIST_IS_HEAD(sdu_assembly_q, sdu) && (!sdu->intact))
			{
				ZLOG_WARN("SDU not intact, but FI in PDU is RLC_FI_FIRST_XLAST, sn=%u, discard SDU.\n", pdu_segment->sn);
				
				/* discard old sdu */
				dllist_remove(sdu_assembly_q, (dllist_node_t *)sdu);
				rlc_sdu_free(sdu);
			}
		}
		else
		{
			/* The last SDU hasn't been completely received */
			sdu = (rlc_sdu_t *)DLLIST_TAIL(sdu_assembly_q);
			if(DLLIST_IS_HEAD(sdu_assembly_q, sdu) || (sdu->intact))
			{
				/* just print a warning, we still deliver this PDU */
				ZLOG_WARN("SDU is intact, but FI in PDU is RLC_FI_NFIRST_XLAST, sn=%u.\n", pdu_segment->sn);
			}
			else{
				if(sdu->n_segment < RLC_SDU_SEGMENT_MAX)
				{
					ZLOG_DEBUG("assemble not first segment: offset=%u length=%u.\n", 
							sdu->size, pdu_segment->li_s[0]);
					
					sdu->size += pdu_segment->li_s[0];
					sdu->segment[sdu->n_segment].data = pdu_segment->data_ptr;
					sdu->segment[sdu->n_segment].length = pdu_segment->li_s[0];
					RLC_REF(pdu_segment);		//increase reference counter
					sdu->segment[sdu->n_segment].cookie = pdu_segment;
					sdu->segment[sdu->n_segment].free = rlc_am_rxseg_free;
					li_len += pdu_segment->li_s[0];
					sdu->n_segment ++;
					sdu->intact = 1;	//temporaryly set intact to 1
				}
				else{
					ZLOG_WARN("SDU has too much segments.\n");
					/* discard sdu */
					dllist_remove(sdu_assembly_q, (dllist_node_t *)sdu);
					rlc_sdu_free(sdu);
				}
				
				//anyway, move to next LI
				li_idx ++;
			}
		}
		
		
		/* now to a new SDU */
		for(; li_idx < pdu_segment->n_li; li_idx ++)
		{
			/* new sdu and append it to queue */
			sdu = rlc_sdu_new();
			dllist_append(sdu_assembly_q, (dllist_node_t *)sdu);

			ZLOG_DEBUG("assemble first segment: length=%u.\n", pdu_segment->li_s[li_idx]);
			
			sdu->size += pdu_segment->li_s[li_idx];
			sdu->segment[sdu->n_segment].data = pdu_segment->data_ptr + li_len;
			sdu->segment[sdu->n_segment].length = pdu_segment->li_s[li_idx];
			RLC_REF(pdu_segment);
			sdu->segment[sdu->n_segment].cookie = pdu_segment;
			sdu->segment[sdu->n_segment].free = rlc_am_rxseg_free;
			li_len += pdu_segment->li_s[li_idx];
			sdu->n_segment ++;
			sdu->intact = 1;	//temporaryly set intact to 1
		}
		
		/* last LI */
		if(!is_last)
		{
			sdu->intact = 0;
		}

		/* update delivery offset: we do this even end_offset=0x7FFF */
		pdu_ctrl->delivery_offset += pdu_segment->end_offset-pdu_segment->start_offset;
		
		pdu_segment = (rlc_am_pdu_segment_t *)pdu_segment->node.next;
	}
	
	return 0;
}

/* deliver reassemblied SDU to upper */
void rlc_am_rx_delivery_sdu(rlc_entity_am_rx_t *amrx, dllist_node_t *sdu_assembly_q)
{
	rlc_sdu_t *sdu;
	
	sdu = (rlc_sdu_t *)sdu_assembly_q->next;
	while(((dllist_node_t *)sdu != sdu_assembly_q) && sdu->intact)
	{
		ZLOG_DEBUG("deliver a SDU to upper: lcid=%d size=%u n_segment=%u.\n", 
				amrx->logical_chan, sdu->size, sdu->n_segment);
		
		if(amrx->deliv_sdu)
			amrx->deliv_sdu(amrx, sdu);
		else
			rlc_dump_sdu(sdu);
		
		dllist_remove(sdu_assembly_q, (dllist_node_t *)sdu);
		
		/* free sdu control info */
		rlc_sdu_free(sdu);
		
		sdu = (rlc_sdu_t *)sdu_assembly_q->next;
	}
}

/* Re-establishment procedure */
int rlc_am_reestablish(rlc_entity_am_t *rlcam)
{
	u16 sn;
	rlc_entity_am_rx_t *amrx;
	rlc_entity_am_tx_t *amtx;
	rlc_am_tx_pdu_ctrl_t *pdu_ctrl;
	rlc_sdu_t *sdu;
	
/* 
-	if it is an AM RLC entity:
      -	when possible, reassemble RLC SDUs from any byte segments of AMD PDUs with 
      SN < VR(MR) in the receiving side, remove RLC headers when doing so and deliver all 
      reassembled RLC SDUs to upper layer in ascending order of the RLC SN, if not delivered before;
      -	discard the remaining AMD PDUs and byte segments of AMD PDUs in the receiving side;
      -	discard all RLC SDUs and AMD PDUs in the transmitting side;
      -	discard all RLC control PDUs.
-	stop and reset all timers;
-	reset all state variables to their initial values.
*/
	if(rlcam == NULL)
		return -1;

	amrx = &rlcam->amrx;
	amtx = &rlcam->amtx;

	/* force reassemble SDU */
	sn = amrx->VR_R;
	while(RLC_SN_LESS(sn, amrx->VR_MR, (RLC_SN_MAX_10BITS+1)))
	{
		if(amrx->rxpdu[sn])
		{
			ZLOG_DEBUG("Re-Establishment: Try to assembly RLC AM SDU from PDU: lcid=%d sn=%u.\n", amrx->logical_chan, sn);
			rlc_am_rx_assemble_sdu(&amrx->sdu_assembly_q, amrx->rxpdu[sn]);

			rlc_am_rx_pdu_ctrl_free(amrx->rxpdu[sn]);
			amrx->rxpdu[sn] = NULL;
		}
		sn = RLC_MOD((sn + 1), (RLC_SN_MAX_10BITS+1));
	}

	rlc_am_rx_delivery_sdu(amrx, &amrx->sdu_assembly_q);

	/* if any partly SDU in assembly queue */
	if(!DLLIST_EMPTY(&amrx->sdu_assembly_q))
	{
		sdu = (rlc_sdu_t *)(amrx->sdu_assembly_q.next);

		dllist_remove(&amrx->sdu_assembly_q, (dllist_node_t *)sdu);
		
		/* free sdu control info */
		rlc_sdu_free(sdu);
	}

	/* discard all RLC SDUs and AMD PDUs in the transmitting side */
	while(!DLLIST_EMPTY(&amtx->sdu_tx_q))
	{
		sdu = (rlc_sdu_t *)(amtx->sdu_tx_q.next);

		dllist_remove(&amtx->sdu_tx_q, (dllist_node_t *)sdu);
		rlc_sdu_free(sdu);
	}
	
	while(!DLLIST_EMPTY(&amtx->pdu_retx_q))
	{
		pdu_ctrl = (rlc_am_tx_pdu_ctrl_t *)(&amtx->pdu_retx_q.next);
		dllist_remove(&amtx->pdu_retx_q, (dllist_node_t *)pdu_ctrl);
	}

	sn = amtx->VT_A;
	while(RLC_SN_LESS(sn, amtx->VT_S, (RLC_SN_MAX_10BITS+1)))
	{
		if(amtx->txpdu[sn])
		{
			rlc_am_tx_pdu_ctrl_free(amtx->txpdu[sn]);
			amtx->txpdu[sn] = NULL;
		}
		sn = RLC_MOD((sn + 1), (RLC_SN_MAX_10BITS+1));
	}

	/* reset timers and state variables */
	if(rlc_timer_is_running(&amrx->t_Reordering))
		rlc_timer_stop(&amrx->t_Reordering);

	if(rlc_timer_is_running(&amrx->t_StatusPdu))
		rlc_timer_stop(&amrx->t_StatusPdu);

	if(rlc_timer_is_running(&amtx->t_PollRetransmit))
		rlc_timer_stop(&amtx->t_PollRetransmit);

	if(rlc_timer_is_running(&amtx->t_StatusProhibit))
		rlc_timer_stop(&amtx->t_StatusProhibit);

	amrx->VR_R = 0;
	amrx->VR_MR = amrx->VR_R + amrx->AM_Window_Size;
	amrx->VR_H = 0;
	amrx->VR_MS = 0;
	amrx->VR_X = 0;
	amrx->n_discard_pdu = 0;
	amrx->n_good_pdu = 0;

	amtx->BYTE_WITHOUT_POLL = 0;
	amtx->PDU_WITHOUT_POLL = 0;
	amtx->n_sdu = 0;
	amtx->poll_bit = 0;
	amtx->sdu_total_size = 0;
	amtx->status_pdu_triggered = 0;
	amtx->VT_A = 0;
	amtx->VT_S = 0;
	amtx->VT_MS = amtx->VT_S + amtx->AM_Window_Size;
	
	return 0;
}

