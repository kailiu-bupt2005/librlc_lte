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
 * RLC UM implementation.
 *
 * @History
 * Phuuix Xiong, Create, 01-25-2011
 */
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "list.h"
#include "rlc.h"
#include "rlc_pdu.h"
#include "log.h"
#include "fastalloc.h"

int rlc_um_rx_assemble_sdu(dllist_node_t *sdu_assembly_q, rlc_um_pdu_t *pdu);
rlc_um_pdu_t *rlc_um_pdu_new();
void rlc_um_pdu_free(rlc_um_pdu_t *pdu);

extern fastalloc_t *g_mem_um_pdu_base;

/***********************************************************************************/
/* Function : rlc_um_pdu_new                                                       */
/***********************************************************************************/
/* Description : - new UM PDU control info                                         */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   Return             |    | pointer to RLC PDU control info                     */
/***********************************************************************************/
rlc_um_pdu_t *rlc_um_pdu_new()
{
	rlc_um_pdu_t *pdu;

	pdu = (rlc_um_pdu_t *)FASTALLOC(g_mem_um_pdu_base);
	if(pdu)
	{
		memset(pdu, 0, sizeof(rlc_um_pdu_t));
	}
	
	return pdu;
}


/***********************************************************************************/
/* Function : rlc_um_pdu_free                                                      */
/***********************************************************************************/
/* Description : - free RLC PDU control structure and its PDU buffer               */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   pdu                | i  | pointer to RLC PDU control info                     */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void rlc_um_pdu_free(rlc_um_pdu_t *pdu)
{
	if(pdu)
	{
		pdu->refcnt --;
		if(pdu->refcnt <= 0)
		{
			if(pdu->buf_free)
				pdu->buf_free(pdu->buf_ptr, pdu->cookie);

			FASTFREE(g_mem_um_pdu_base, pdu);
		}
	}
}

/***********************************************************************************/
/* Function : t_Reordering_um_func                                                 */
/***********************************************************************************/
/* Description : - RLC UM t-Reordering callback                                    */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
static void t_Reordering_um_func(void *timer, u32 arg1, u32 arg2)
{
	rlc_entity_um_rx_t *umrx = (rlc_entity_um_rx_t *)arg1;
	u16 sn, sn_fs;
	
	assert(umrx);

	ZLOG_DEBUG("RLC UM Counter after t_Reordering expires: lcid=%d VR_UR=%u VR_UX=%u VR_UH=%u.\n", 
				umrx->logical_chan, umrx->VR_UR, umrx->VR_UX, umrx->VR_UH);
/*
  When t-Reordering expires, the receiving UM RLC entity shall:
  -	update VR(UR) to the SN of the first UMD PDU with SN >= VR(UX) that has not been received;
  -	reassemble RLC SDUs from any UMD PDUs with SN < updated VR(UR), remove RLC headers when doing
    so and deliver the reassembled RLC SDUs to upper layer in ascending order of the RLC SN if 
    not delivered before;
  -	if VR(UH) > VR(UR):
     -	start t-Reordering;
     -	set VR(UX) to VR(UH).
*/
	sn_fs = umrx->sn_max + 1;
	sn = umrx->VR_UR;
	while(RLC_SN_LESS(sn, umrx->VR_UX, sn_fs) || umrx->pdu[sn])
	{
		if(umrx->pdu[sn])
		{
			ZLOG_DEBUG("t_Reordering: assembly RLC UM SDU from PDU: lcid=%d sn=%u.\n", umrx->logical_chan, sn);
			rlc_um_rx_assemble_sdu(&umrx->sdu_assembly_q, umrx->pdu[sn]);
			umrx->pdu[sn] = NULL;
		}
		sn = RLC_MOD((sn + 1), sn_fs);
	}
	umrx->VR_UR = RLC_MOD(sn, sn_fs);

	if(RLC_SN_LESS(umrx->VR_UR, umrx->VR_UH, sn_fs))
	{
		ZLOG_DEBUG("RLC UM start timer: t_Reordering lcid=%d duration=%u.\n", 
				umrx->logical_chan, umrx->t_Reordering.duration);
		rlc_timer_start(&umrx->t_Reordering);
		
		umrx->VR_UX = umrx->VR_UH;
	}
	
	ZLOG_DEBUG("RLC UM Counter after t_Reordering processing: lcid=%d VR_UR=%u VR_UX=%u VR_UH=%u.\n", 
			umrx->logical_chan, umrx->VR_UR, umrx->VR_UX, umrx->VR_UH);

	/* deliver intact SDU to upper */
	rlc_um_rx_delivery_sdu(umrx, &umrx->sdu_assembly_q);
}

/***********************************************************************************/
/* Function : rlc_um_tx_sdu_enqueue                                                */
/***********************************************************************************/
/* Description : - RLC SDU enqueue (only one segment)                              */
/*               - Called by PDCP, etc                                             */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   umtx               | i  | RLC UM entity                                       */
/*   buf_ptr            | i  | RLC SDU buffer pointer                              */
/*   sdu_size           | i  | Size of SDU                                         */
/*   cookie             | i  | parameter of free function                          */
/*   Return             |    | 0 is success                                        */
/***********************************************************************************/
int rlc_um_tx_sdu_enqueue(rlc_entity_um_tx_t *umtx, u8 *buf_ptr, u32 sdu_size, void *cookie)
{
	rlc_sdu_t *sdu;
	
	if(sdu_size <= 0 || buf_ptr == NULL || umtx == NULL)
		return -1;

	sdu = rlc_sdu_new();
	if(sdu == NULL)
	{
		ZLOG_ERR("out of memory: rlc_sdu_new() lcid=%d.\n", umtx->logical_chan);
		return -1;
	}

	dllist_append(&umtx->sdu_tx_q, (dllist_node_t *)sdu);
	
	sdu->segment[0].cookie = cookie;
	sdu->segment[0].free = umtx->free_sdu;
	sdu->segment[0].data = buf_ptr;
	sdu->segment[0].length = sdu_size;
	sdu->size = sdu_size;
	sdu->n_segment = 1;
	sdu->intact = 1;
	umtx->sdu_total_size += sdu_size;
	umtx->n_sdu ++;
	
	ZLOG_DEBUG("UM SDU enqueue: lcid=%d buf_ptr=%p sdu_size=%u total_size=%u data=0x%02x%02x%02x%02x\n",
			umtx->logical_chan, buf_ptr, sdu_size, umtx->sdu_total_size, buf_ptr[0], buf_ptr[1], buf_ptr[2], buf_ptr[3]);
	
	return 0;
}

/***********************************************************************************/
/* Function : rlc_um_tx_get_sdu_size                                               */
/***********************************************************************************/
/* Description : - Get available SDU size in tx queue                              */
/*               - Called by MAC                                                   */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   umtx               | i  | RLC UM entity                                       */
/*   Return             |    | Size of RLC SDU including RLC header                */
/***********************************************************************************/
u32 rlc_um_tx_get_sdu_size(rlc_entity_um_tx_t *umtx)
{
	u32 head_len;
	u32 li_len;
	u32 pdu_size;
	
	/* no data in queue */
	if(umtx->sdu_total_size == 0)
		return 0;
	
	/* get length of RLD PDU header */
	head_len = (umtx->sn_max==RLC_SN_MAX_5BITS)?1:2;
	
	/* number of LI equals to n_sdu-1 */
	li_len = ((umtx->n_sdu-1)>>1)*3;
	if((umtx->n_sdu-1) & 0x01)
		li_len += 2;
		
	pdu_size = umtx->sdu_total_size + head_len + li_len;
	return pdu_size;
}



/***********************************************************************************/
/* Function : rlc_um_tx_build_pdu                                                  */
/***********************************************************************************/
/* Description : - Called by MAC to build RLC PDU                                  */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   umtx               | i  | RLC UM entity                                       */
/*   buf_ptr            | o  | RLC PDU buffer pointer                              */
/*   pdu_size           | i  | requested PDU Size                                  */
/*   Return             |    | Size of real RLC PDU                                */
/***********************************************************************************/
int rlc_um_tx_build_pdu(rlc_entity_um_tx_t *umtx, u8 *buf_ptr, u16 pdu_size)
{
	u32 head_len;
	rlc_li_t *li_ptr;
	u8 *data_ptr;
	u32 data_size;
	rlc_sdu_t *sdu;
	rlc_um_pdu_t pdu;
	
	ZLOG_DEBUG("request RLC UM to build PDU: lcid=%d size=%u.\n", umtx->logical_chan, pdu_size);
	
	if(umtx->sdu_total_size == 0)		//no data in queue
		return 0;

	assert(umtx->n_sdu > 0);
	
	/* (1) first round: build LIs */
	sdu = (rlc_sdu_t *)umtx->sdu_tx_q.next;
	pdu.n_li = 0;
	pdu.fi = 0;
	//set the first bit of FI
	if(sdu->offset)
		pdu.fi |= 0x02;			//NFIRST
	else
		pdu.fi &= 0x01;			//FIRST
		
	head_len = (umtx->sn_max==RLC_SN_MAX_5BITS)?1:2;
	if(pdu_size < head_len)		//at leaset 1 byte data
		return 0;

	pdu.n_li = rlc_build_li_from_sdu(pdu_size, head_len, &umtx->sdu_tx_q, pdu.li_s);
	if(pdu.n_li == 0)
	{
		ZLOG_WARN("RLC build LI: number of LI is 0, lcid=%d.\n", umtx->logical_chan);
		return 0;
	}

	assert(pdu.n_li <= umtx->n_sdu);
	assert(pdu.n_li <= RLC_LI_NUM_MAX);
	
	/* (2) second round: write LIs and data */
	li_ptr = (rlc_li_t *)(buf_ptr + head_len);
	rlc_encode_li(li_ptr, pdu.n_li, pdu.li_s);
	
	data_ptr = (u8 *)li_ptr + ((pdu.n_li-1)>>1)*3;
	if((pdu.n_li & 0x01) == 0)
		data_ptr += 2;
	data_size = rlc_encode_sdu(data_ptr, pdu.n_li, pdu.li_s, &umtx->sdu_tx_q);
	data_ptr += (data_size & 0xFFFF);
	umtx->sdu_total_size -= (data_size & 0xFFFF);
	umtx->n_sdu -= (data_size >> 16);

	assert(umtx->sdu_total_size >= 0);
	assert(umtx->n_sdu >= 0);

	//set the second bit of FI
	if(DLLIST_EMPTY(&umtx->sdu_tx_q))
		pdu.fi &= 0x02;			//LAST
	else{
		sdu = (rlc_sdu_t *)umtx->sdu_tx_q.next;
		if(sdu->offset)
			pdu.fi |= 0x01;			//NLAST
		else
			pdu.fi &= 0x02;			//LAST
	}
	
	/* set RLC PDU header */
	if(umtx->sn_max== RLC_SN_MAX_5BITS)
	{
		rlc_um_pdu_head_5bits_t *pdu_head;
		pdu_head = (rlc_um_pdu_head_5bits_t *)buf_ptr;
		pdu_head->e = (pdu.n_li > 1);
		pdu_head->fi = pdu.fi;
		pdu_head->sn = umtx->VT_US;

		ZLOG_DEBUG("After build: lcid=%d fi=%u sn=%u n_li=%u li_s=(%u %u %u ..)\n", 
			umtx->logical_chan, pdu_head->fi, pdu_head->sn, pdu.n_li, 
			pdu.li_s[0], pdu.li_s[1], pdu.li_s[2]);
	}
	else
	{
		rlc_um_pdu_head_10bits_t *pdu_head;
		pdu_head = (rlc_um_pdu_head_10bits_t *)buf_ptr;
		*buf_ptr = 0;			//set r1
		pdu_head->e = (pdu.n_li > 1);
		pdu_head->fi = pdu.fi;
		pdu_head->sn = umtx->VT_US;

		ZLOG_DEBUG("After build: lcid=%d fi=%u sn=%u n_li=%u li_s=(%u %u %u ..)\n", 
			umtx->logical_chan, pdu_head->fi, pdu_head->sn, pdu.n_li, 
			pdu.li_s[0], pdu.li_s[1], pdu.li_s[2]);
	}
	
	/* update UM counter */
	umtx->VT_US++;
	umtx->VT_US &= umtx->sn_max;

	return data_ptr-buf_ptr;
}

/***********************************************************************************/
/* Function : rlc_um_rx_process_pdu                                                */
/***********************************************************************************/
/* Description : - receive one UM PDU from MAC and enqueue RLC SDU                 */
/*               - the assembled SDU is append to entity's SDU queue               */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   Return             |    | Size of RLC PDU                                     */
/***********************************************************************************/
int rlc_um_rx_process_pdu(rlc_entity_um_rx_t *umrx, u8 *buf_ptr, u32 buf_len, void *cookie)
{
	u16 sn, sn_fs, sn_reodering_low;
	rlc_li_t *li_ptr = NULL;
	u32 e;
	rlc_um_pdu_t *pdu;
			
	/* allocate new PDU */
	pdu = (rlc_um_pdu_t *)rlc_um_pdu_new();
	if(pdu == NULL)
	{
		ZLOG_ERR("out of memory for new UM PDU: lcid=%d.\n", umrx->logical_chan);
		umrx->free_pdu(buf_ptr, cookie);
		return -1;
	}
	pdu->buf_ptr = buf_ptr;
	pdu->buf_len = buf_len;
	pdu->buf_free = umrx->free_pdu;
	pdu->cookie  = cookie;
	
	/* parse header */
	if(umrx->sn_max == RLC_SN_MAX_5BITS)
	{
		rlc_um_pdu_head_5bits_t *pdu_hdr;
		
		pdu_hdr = (rlc_um_pdu_head_5bits_t *)buf_ptr;
		pdu->fi = pdu_hdr->fi;
		sn = pdu->sn = pdu_hdr->sn;
		sn_fs = RLC_SN_MAX_5BITS+1;
		buf_len -= 1;
		li_ptr = (rlc_li_t *)(pdu_hdr+1);
		e = pdu_hdr->e;
	}
	else
	{
		rlc_um_pdu_head_10bits_t *pdu_hdr;
		
		pdu_hdr = (rlc_um_pdu_head_10bits_t *)buf_ptr;
		pdu->fi = pdu_hdr->fi;
		sn = pdu->sn = pdu_hdr->sn;
		sn_fs = RLC_SN_MAX_10BITS+1;
		buf_len -= 2;
		li_ptr = (rlc_li_t *)(pdu_hdr+1);
		e = pdu_hdr->e;
	}
	
	ZLOG_DEBUG("RLC UM process PDU: lcid=%d sn=%u fi=%u pdu_size=%u VR_UR=%u VR_UX=%u VR_UH=%u.\n", 
			umrx->logical_chan, sn, pdu->fi, pdu->buf_len, umrx->VR_UR, umrx->VR_UX, umrx->VR_UH);
			
	/* Receive operations */
	
/*
 -	if VR(UR) < x < VR(UH) and the UMD PDU with SN = x has been received before; or
 -	if (VR(UH) ¨C UM_Window_Size) <= x < VR(UR):
     -	discard the received UMD PDU;
 */
 	if((0 < RLC_MOD(sn - umrx->VR_UR, sn_fs)) && 
 			(RLC_MOD(sn - umrx->VR_UR, sn_fs) < RLC_MOD(umrx->VR_UH - umrx->VR_UR, sn_fs)))
 	{
 		if(umrx->pdu[sn])
 		{
 			umrx->n_discard_pdu ++;
 			rlc_um_pdu_free(pdu);
			ZLOG_WARN("discard UM PDU -- sn duplicated: lcid=%d sn=%u, VR(UH)=%u, VR(UR)=%u.\n", 
					umrx->logical_chan, sn, umrx->VR_UH, umrx->VR_UR);
			return -1;
 		}
 	}
 	
 	sn_reodering_low = RLC_MOD(umrx->VR_UH - umrx->UM_Window_Size, sn_fs);
	if(RLC_SN_IN_WINDOW(sn, umrx->VR_UR, sn_reodering_low, sn_fs))
	{
		umrx->n_discard_pdu ++;
		rlc_um_pdu_free(pdu);
		ZLOG_WARN("discard UM PDU -- (VR(UH) ¨C UM_Window_Size) <= sn < VR(UR): lcid=%d sn=%u, VR(UH)=%u, VR(UR)=%u.\n", 
				umrx->logical_chan, sn, umrx->VR_UH, umrx->VR_UR);
		return -1;
	}
	
/*
 -	place the received UMD PDU in the reception buffer.
 */
 	pdu->n_li = rlc_parse_li(e, li_ptr, buf_len, &pdu->data_ptr, pdu->li_s);
 	if(pdu->n_li <= 0 || pdu->n_li > RLC_LI_NUM_MAX)
 	{
 		ZLOG_WARN("wrong UM PDU: lcid=%d n_li=%d, sn=%u, size=%d.\n", umrx->logical_chan, (int)pdu->n_li, sn, buf_len);
 		umrx->n_discard_pdu ++;
 		rlc_um_pdu_free(pdu);
 		return -1;
 	}
 	/* store pdu */
	assert(umrx->pdu[sn] == NULL);
	umrx->pdu[sn] = pdu;

/*
 -	if x falls outside of the reordering window:
    -	update VR(UH) to x + 1;
    -	reassemble RLC SDUs from any UMD PDUs with SN that falls outside of the reordering window, 
        remove RLC headers when doing so and deliver the reassembled RLC SDUs to upper layer in 
        ascending order of the RLC SN if not delivered before;
    -	if VR(UR) falls outside of the reordering window:
        -	set VR(UR) to (VR(UH) ¨C UM_Window_Size);
*/
	if(!RLC_SN_IN_RECODERING_WIN(sn, umrx->VR_UH, sn_reodering_low, sn_fs))
	{
		u16 tmp_sn = umrx->VR_UR;
		
		ZLOG_DEBUG("sn falls outside of the reordering window: lcid=%d sn=%u.\n", umrx->logical_chan, sn);
		
		umrx->VR_UH = RLC_MOD(sn+1, sn_fs);
		sn_reodering_low = RLC_MOD(umrx->VR_UH - umrx->UM_Window_Size, sn_fs);
		
		/* reassemble RLC SDUs that fall outside of the window */
		while(!RLC_SN_IN_RECODERING_WIN(tmp_sn, umrx->VR_UH, sn_reodering_low, sn_fs))
		{
			if(umrx->pdu[tmp_sn])
			{
				ZLOG_DEBUG("RLC UM assembly SDU from PDU: lcid=%d sn=%u.\n", umrx->logical_chan, tmp_sn);
				rlc_um_rx_assemble_sdu(&umrx->sdu_assembly_q, umrx->pdu[tmp_sn]);
				umrx->pdu[tmp_sn] = NULL;
			}
			tmp_sn = RLC_MOD((tmp_sn + 1), sn_fs);
		}
		
		if(!RLC_SN_IN_RECODERING_WIN(umrx->VR_UR, umrx->VR_UH, sn_reodering_low, sn_fs))
		{
			umrx->VR_UR = RLC_MOD(sn_reodering_low, sn_fs);
		}
	}

/*
 -	if the reception buffer contains an UMD PDU with SN = VR(UR):
    -	update VR(UR) to the SN of the first UMD PDU with SN > current VR(UR) that has not been received;
    -	reassemble RLC SDUs from any UMD PDUs with SN < updated VR(UR), remove RLC headers when doing so 
        and deliver the reassembled RLC SDUs to upper layer in ascending order of the RLC SN if not 
        delivered before;
*/
	if(sn == umrx->VR_UR)
	{
		do{
			ZLOG_DEBUG("Try to assembly RLC UM SDU from PDU: lcid=%d sn=%u.\n", umrx->logical_chan, sn);
			rlc_um_rx_assemble_sdu(&umrx->sdu_assembly_q, umrx->pdu[sn]);
			umrx->pdu[sn] = NULL;
			sn = RLC_MOD((sn + 1), sn_fs);
		}while(umrx->pdu[sn]);
		
		umrx->VR_UR = RLC_MOD(sn, sn_fs);
	}


/*
 -	if t-Reordering is running:
    -	if VR(UX) <= VR(UR); or
    -	if VR(UX) falls outside of the reordering window and VR(UX) is not equal to VR(UH)::
        -	stop and reset t-Reordering;
*/
	if(rlc_timer_is_running(&umrx->t_Reordering))
	{
		if(RLC_SN_LESSTHAN(umrx->VR_UX, umrx->VR_UR, sn_fs) ||
				((umrx->VR_UX != umrx->VR_UH) && !RLC_SN_IN_RECODERING_WIN(umrx->VR_UX, umrx->VR_UH, sn_reodering_low, sn_fs)))
		{
			ZLOG_DEBUG("RLC UM stop timer t_Reordering: lcid=%d.\n", umrx->logical_chan);
			rlc_timer_stop(&umrx->t_Reordering);
		}
	}

/*
 -	if t-Reordering is not running (includes the case when t-Reordering is stopped due to actions above):
    -	if VR(UH) > VR(UR):
        -	start t-Reordering;
        -	set VR(UX) to VR(UH).
*/
	if(!rlc_timer_is_running(&umrx->t_Reordering))
	{
		if(RLC_SN_LESS(umrx->VR_UR, umrx->VR_UH, sn_fs))
		{
			ZLOG_DEBUG("RLC UM start timer t_Reordering: lcid=%d duration=%u.\n", 
					umrx->logical_chan, umrx->t_Reordering.duration);
			rlc_timer_start(&umrx->t_Reordering);
			
			umrx->VR_UX = umrx->VR_UH;
		}
	}
	
	ZLOG_DEBUG("RLC UM Counter after processing: lcid=%d VR_UR=%u VR_UX=%u VR_UH=%u.\n", 
			umrx->logical_chan, umrx->VR_UR, umrx->VR_UX, umrx->VR_UH);
	
	/* deliver intact SDU to upper */
	rlc_um_rx_delivery_sdu(umrx, &umrx->sdu_assembly_q);
	
	return 0;	
}

/***********************************************************************************/
/* Function : rlc_um_rx_delivery_sdu                                               */
/***********************************************************************************/
/* Description : - delivery intact SDU to upper                                    */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   sdu_assembly_q     | i  | assemblied RLC SDU queue                            */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void rlc_um_rx_delivery_sdu(rlc_entity_um_rx_t *umrx, dllist_node_t *sdu_assembly_q)
{
	rlc_sdu_t *sdu;
	
	sdu = (rlc_sdu_t *)sdu_assembly_q->next;
	while((dllist_node_t *)sdu != sdu_assembly_q)
	{
		if(sdu->intact){
			ZLOG_DEBUG("deliver a SDU to upper: lcid=%d size=%u n_segment=%u.\n", 
					umrx->logical_chan, sdu->size, sdu->n_segment);
			if(umrx->deliv_sdu)
				umrx->deliv_sdu(umrx, sdu);
			else
				rlc_dump_sdu(sdu);
			
			dllist_remove(sdu_assembly_q, (dllist_node_t *)sdu);
			rlc_sdu_free(sdu);
			sdu = (rlc_sdu_t *)sdu_assembly_q->next;
		}
		else
			break;
	}
}

/***********************************************************************************/
/* Function : rlc_um_rxseg_free                                                    */
/***********************************************************************************/
/* Description : - free recieved SDU segments                                      */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   data               | i  | no used                                             */
/*   cookie             | i  | rlc_um_pdu_t                                        */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void rlc_um_rxseg_free(void *data, void *cookie)
{
	rlc_um_pdu_free((rlc_um_pdu_t *)cookie);
}

/***********************************************************************************/
/* Function : rlc_um_rx_assemble_sdu                                               */
/***********************************************************************************/
/* Description : - Assemble one PDU to SDUs                                        */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   sdu_assembly_q     | o  | assemblied RLC SDU is put here                      */
/*   pdu                | i  | PDU control info pointer                            */
/*   Return             |    | 0 is success                                        */
/***********************************************************************************/
int rlc_um_rx_assemble_sdu(dllist_node_t *sdu_assembly_q, rlc_um_pdu_t *pdu)
{
	rlc_sdu_t *sdu;
	u32 li_idx = 0, li_len = 0;
	u32 is_first, is_last;
	
	/* RLC_FI_FIRST_LAST	00
	   RLC_FI_FIRST_NLAST	01
	   RLC_FI_NFIRST_LAST	10
	   RLC_FI_NFIRST_NLAST	11 */
	is_first = !(pdu->fi & 0x02);
	is_last = !(pdu->fi & 0x01);
	
	/* first LI */
	if(is_first)
	{
		//check last SDU's state
		sdu = (rlc_sdu_t *)DLLIST_TAIL(sdu_assembly_q);
		if((!DLLIST_IS_HEAD(sdu_assembly_q, sdu)) && (!sdu->intact))
		{
			ZLOG_WARN("SDU not intact, but FI in PDU is RLC_FI_FIRST_XLAST, sn=%u, discard SDU.\n", pdu->sn);
			
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
			ZLOG_WARN("SDU is intact, but FI in PDU is RLC_FI_NFIRST_XLAST, sn=%u.\n", pdu->sn);
			li_idx ++;	//drop this segment
			li_len += pdu->li_s[0];
		}
		else{
			if(sdu->n_segment < RLC_SDU_SEGMENT_MAX)
			{
				ZLOG_DEBUG("assemble not first segment: offset=%u length=%u.\n", 
						sdu->size, pdu->li_s[0]);
				
				sdu->size += pdu->li_s[0];
				sdu->segment[sdu->n_segment].data = pdu->data_ptr;
				sdu->segment[sdu->n_segment].length = pdu->li_s[0];
				sdu->segment[sdu->n_segment].cookie = pdu;
				sdu->segment[sdu->n_segment].free = rlc_um_rxseg_free;
				li_len += pdu->li_s[0];
				sdu->n_segment ++;
				sdu->intact = 1;	//temporaryly set intact to 1
				RLC_REF(pdu);		//increase reference counter
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
	for(; li_idx < pdu->n_li; li_idx ++)
	{
		/* new sdu and append it to queue */
		sdu = rlc_sdu_new();
		dllist_append(sdu_assembly_q, (dllist_node_t *)sdu);

		ZLOG_DEBUG("assemble first segment: length=%u.\n", pdu->li_s[li_idx]);
		
		sdu->size += pdu->li_s[li_idx];
		sdu->segment[sdu->n_segment].data = pdu->data_ptr + li_len;
		sdu->segment[sdu->n_segment].length = pdu->li_s[li_idx];
		sdu->segment[sdu->n_segment].cookie = pdu;
		sdu->segment[sdu->n_segment].free = rlc_um_rxseg_free;
		li_len += pdu->li_s[li_idx];
		sdu->n_segment ++;
		sdu->intact = 1;	//temporaryly set intact to 1
		RLC_REF(pdu);		//increase reference counter
	}
	
	/* last LI */
	if(!is_last)
	{
		sdu->intact = 0;
	}
	
	return 0;
}


/***********************************************************************************/
/* Function : rlc_um_init                                                          */
/***********************************************************************************/
/* Description : - Init RLC UM entity                                              */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   rlc_um             | i  | pointer of RLC UM entity                            */
/*   sn_bits            | i  | Bits number of SN (5 or 10)                         */
/*   UM_Window_Size     | i  | UM Window size: 512 or 16 or 0                      */
/*   t_Reordering       | i  | t_Reodering timer duration                          */
/*   free_pdu          | i  | function to free pdu                                 */
/*   free_sdu          | i   | function to free sdu                                */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void rlc_um_init(rlc_entity_um_t *rlc_um, int sn_bits, u32 UM_Window_Size, u32 t_Reordering,
		void (*free_pdu)(void *, void *), void (*free_sdu)(void *, void *))
{
	memset(rlc_um, 0, sizeof(rlc_entity_um_t));
	if(sn_bits != 5 && sn_bits != 10)
		sn_bits = 10;
	rlc_um->umrx.type = RLC_ENTITY_TYPE_UM;
	rlc_um->umtx.type = RLC_ENTITY_TYPE_UM;
	rlc_um->umrx.sn_max = (1<<sn_bits) - 1;
	rlc_um->umtx.sn_max = (1<<sn_bits) - 1;
	rlc_um->umrx.UM_Window_Size = UM_Window_Size;
	rlc_um->umrx.t_Reordering.duration = t_Reordering;
	rlc_um->umrx.t_Reordering.onexpired_func = t_Reordering_um_func;
	rlc_um->umrx.t_Reordering.param[0] = (u32)&rlc_um->umrx;
	rlc_um->umrx.t_Reordering.param[1] = 0;

	rlc_um->umrx.free_pdu = free_pdu;
	rlc_um->umrx.free_sdu = free_sdu;
	rlc_um->umtx.free_pdu = free_pdu;
	rlc_um->umtx.free_sdu = free_sdu;
	
	dllist_init(&(rlc_um->umrx.sdu_assembly_q));
	dllist_init(&(rlc_um->umtx.sdu_tx_q));
}

/***********************************************************************************/
/* Function : rlc_um_set_deliv_func                                                */
/***********************************************************************************/
/* Description : - deliver reassembled SDUs to Upper                               */
/*                 Provided by Upper (PDCP etc)                                    */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   rlc_um             | i  | UM entity                                           */
/*   deliv_sdu          | i  | delivery function provided by upper                 */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void rlc_um_set_deliv_func(rlc_entity_um_t *rlc_um, void (*deliv_sdu)(struct rlc_entity_um_rx *, rlc_sdu_t *))
{
	if(rlc_um)
		rlc_um->umrx.deliv_sdu = deliv_sdu;
}


/***********************************************************************************/
/* Function : rlc_um_reestablish                                                   */
/***********************************************************************************/
/* Description : - Re-establishment procedure                                      */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   rlcum              | o  | UM entity                                           */
/*   Return             |    | 0 is success                                        */
/***********************************************************************************/
int rlc_um_reestablish(rlc_entity_um_t *rlcum)
{
	u16 sn;
	rlc_entity_um_rx_t *umrx;
	rlc_entity_um_tx_t *umtx;
	rlc_sdu_t *sdu;

	if(rlcum == NULL)
		return -1;

	umrx = &rlcum->umrx;
	umtx = &rlcum->umtx;
/* 
-	if it is a receiving UM RLC entity:
	-	when possible, reassemble RLC SDUs from UMD PDUs with SN < VR(UH), remove RLC headers when 
	doing so and deliver all reassembled RLC SDUs to upper layer in ascending order of the RLC SN, if not 
	delivered before;
	-	discard all remaining UMD PDUs;
*/
	/* force reassemble SDU */
	sn = umrx->VR_UR;
	while(RLC_SN_LESS(sn, umrx->VR_UH, (RLC_SN_MAX_10BITS+1)))
	{
		if(umrx->pdu[sn])
		{
			ZLOG_DEBUG("Re-Establishment: Try to assembly RLC UM SDU from PDU: lcid=%d sn=%u.\n", umrx->logical_chan, sn);
			rlc_um_rx_assemble_sdu(&umrx->sdu_assembly_q, umrx->pdu[sn]);

			rlc_um_pdu_free(umrx->pdu[sn]);
			umrx->pdu[sn] = NULL;
		}
		sn = RLC_MOD((sn + 1), (RLC_SN_MAX_10BITS+1));
	}

	rlc_um_rx_delivery_sdu(umrx, &umrx->sdu_assembly_q);

	/* if any partly SDU in assembly queue */
	if(!DLLIST_EMPTY(&umrx->sdu_assembly_q))
	{
		sdu = (rlc_sdu_t *)(umrx->sdu_assembly_q.next);

		dllist_remove(&umrx->sdu_assembly_q, (dllist_node_t *)sdu);
		
		/* free sdu control info */
		rlc_sdu_free(sdu);
	}

/* 
-	if it is a transmitting UM RLC entity:
	-	discard all RLC SDUs;
*/
	while(!DLLIST_EMPTY(&umtx->sdu_tx_q))
	{
		sdu = (rlc_sdu_t *)(umtx->sdu_tx_q.next);

		dllist_remove(&umtx->sdu_tx_q, (dllist_node_t *)sdu);
		rlc_sdu_free(sdu);
	}
	
/* 
-	stop and reset all timers;
-	reset all state variables to their initial values.
*/
	if(rlc_timer_is_running(&umrx->t_Reordering))
		rlc_timer_stop(&umrx->t_Reordering);

	umrx->n_discard_pdu = 0;
	umrx->n_good_pdu = 0;
	umrx->VR_UR = 0;
	umrx->VR_UH = 0;
	umrx->VR_UX = 0;

	umtx->n_sdu = 0;
	umtx->sdu_total_size = 0;
	umtx->VT_US = 0;
	
	return 0;
}


