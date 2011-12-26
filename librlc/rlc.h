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
 * RLC common definition
 *
 * @History
 * Phuuix Xiong, Create, 01-25-2011
 */
#ifndef _RLC_H_
#define _RLC_H_

#include "stdtypes.h"
#include "rlc_pdu.h"
#include "list.h"
#include "ptimer.h"

#define RLC_MOD(x, y) \
	((x) & ((y)-1))

#define RLC_MAX(x, y) \
	((x) > (y) ? (x) : (y))

#define RLC_MIN(x, y) \
	((x) > (y) ? (y) : (x))

#define RLC_REF(x) \
	(x)->refcnt ++

#define RLC_DEREF(x) \
	(x)->refcnt --

/*
 -	a SN falls within the reordering window if (VR(UH) ¨C UM_Window_Size) <= SN < VR(UH);
 -	a SN falls outside of the reordering window otherwise.
*/
#if 0
#define RLC_SN_IN_RECODERING_WIN(sn, sn_reodering_high, sn_reodering_low, sn_fs) \
	(( 0 <= RLC_MOD(sn - sn_reodering_low, sn_fs)) && \
	(RLC_MOD(sn - sn_reodering_low, sn_fs) < RLC_MOD(sn_reodering_high - sn_reodering_low, sn_fs)))
#endif
	
#define RLC_SN_IN_WINDOW(sn, sn_win_high, sn_win_low, sn_fs) \
	(RLC_MOD(sn - sn_win_low, sn_fs) < RLC_MOD(sn_win_high - sn_win_low, sn_fs))
	
#define RLC_SN_IN_RECODERING_WIN RLC_SN_IN_WINDOW
#define RLC_SN_IN_TRANSMITTING_WIN RLC_SN_IN_WINDOW
#define RLC_SN_IN_RECEIVING_WIN RLC_SN_IN_WINDOW

#define RLC_SN_LESS(sn_small, sn_large, sn_fs) \
	(RLC_MOD(sn_large-sn_small, sn_fs) < (sn_fs>>2) && \
	sn_small != sn_large)

#define RLC_SN_LESSTHAN(sn_small, sn_large, sn_fs) \
	(RLC_MOD(sn_large-sn_small, sn_fs) < (sn_fs>>2))

#define RLC_SN_FS_MAX 1024
#define RLC_ASSEMBLY_QUEUE_SIZE_MAX 64

#define RLC_LI_NUM_MAX 32
#define RLC_SEG_NUM_MAX 32
#define RLC_SDU_SEGMENT_MAX 32

/* macro used by rlc_am_tx_build_pdu() */
#define RLC_AM_FRESH_PDU 0
#define RLC_AM_CTRL_PDU 1
#define RLC_AM_RETX_PDU 2

/**********************************************************************/
/*                RLC SDU                                 */
/**********************************************************************/

typedef struct rlc_sdu_segment
{
	u8 *data;
	u32 length;
	
	void *cookie;
	void (*free)(void *data, void *cookie);
}rlc_sdu_segment_t;

typedef struct rlc_sdu
{
	dllist_node_t node;				/* sdu list */
	rlc_sdu_segment_t segment[RLC_SDU_SEGMENT_MAX];
	u32 size;							/* total size of SDU */
	u32 n_segment;						/* current segment number */
	u32 intact;							/* all segment received */
	u32 offset;							/* read offset */
}rlc_sdu_t;

/**********************************************************************/
/*                RLC TM                                              */
/**********************************************************************/
/* rlc tm entity */
typedef struct rlc_entity_tm
{
	u16 type;							/* RLC entity type */
	u32 logical_chan;					/* logical channel id */
	void *userdata;						/* user data */
	
	s32 sdu_total_size;					/* total size of SDU in Tx queue */
	s32 n_sdu;							/* number of SDU in Tx queue */
	dllist_node_t sdu_tx_q;				/* SDU Tx queue */

	void (*free_sdu)(void *, void *);			/* function to free SDU */
}rlc_entity_tm_t;


/*****************RLC UM Common **********************************************
 * Some assumptions are introduced to simplify design and also for efficiency:
 * 1) PDU is always composed of only one continuous buffer.
 * 2) SDU can be composed of one or more continuous buffers.
 *****************************************************************************/


typedef struct rlc_um_pdu
{
	int refcnt;							/* reference counter */
	u8 *buf_ptr;						/* PDU pointer indicated by MAC */
	u32 buf_len;						/* length of PDU */
	void (*buf_free)(void *, void *);	/* used to free buf_ptr */
	void *cookie;						/* cookie used for free_pdu() */
	
	u32 fi;
	u32 sn;
	s32 n_li;							/* really the number of SDU */
	u32 li_s[RLC_LI_NUM_MAX];
	u8 *data_ptr;						/* the 1st SDU in PDU */
}rlc_um_pdu_t;

/**********************************************************************/
/*                RLC UM                                              */
/**********************************************************************/
/* rlc um tx entity */
typedef struct rlc_entity_um_tx
{
	u16 type;							/* type of entity */
	u32 logical_chan;					/* logical channel id */
	void *userdata;						/* user data */
	
	u16 VT_US;							/* VT(US) */
	u16 sn_max;							/* 5 bit SN: 31; 10 bit SN: 1023 */
	
	s32 sdu_total_size;					/* total size of SDU in Tx queue */
	s32 n_sdu;							/* number of SDU in Tx queue */
	dllist_node_t sdu_tx_q;			/* SDU Tx queue */

	void (*free_pdu)(void *, void *);			/* function to free PDU */
	void (*free_sdu)(void *, void *);			/* function to free SDU */
}rlc_entity_um_tx_t;

/* rlc um rx entity */
typedef struct rlc_entity_um_rx
{
	u16 type;							/* type of entity */
	u32 logical_chan;					/* logical channel id */
	void *userdata;						/* user data */
	
	u16 VR_UR;							/* VR(UR) */
	u16 VR_UX;							/* VR(UX) */
	u16 VR_UH;							/* VR(UH) */
	u16 UM_Window_Size;					/* const UM_Window_Size */
	u16 sn_max;							/* 5 bit SN: 31; 10 bit SN: 1023 */
	ptimer_t t_Reordering;			/* timer t-Reordering */
	
	rlc_um_pdu_t *pdu[RLC_SN_FS_MAX];	/* reception buffer */
	void (*deliv_sdu)(struct rlc_entity_um_rx *, rlc_sdu_t *);
	void (*free_pdu)(void *, void *);			/* function to free PDU */
	void (*free_sdu)(void *, void *);			/* function to free SDU */
	
	dllist_node_t sdu_assembly_q;
	
	u32 n_discard_pdu;					/* counter: discarded PDUs */
	u32 n_good_pdu;
}rlc_entity_um_rx_t;

/* rlc um entity */
typedef struct rlc_entity_um
{
	rlc_entity_um_rx_t umrx;
	rlc_entity_um_tx_t umtx;
}rlc_entity_um_t;


/**********************************************************************/
/*                RLC AM                                              */
/**********************************************************************/

/* RLC AM PDU segment */
typedef struct rlc_am_pdu_segment
{
	dllist_node_t node;					/* link with next segment */
	
	u16 start_offset;					
	u16 end_offset;						
	
	int refcnt;							/* reference counter */
	u8 *buf_ptr;						/* PDU pointer indicated by MAC */
	u16 buf_len;						/* length of PDU */
	void *buf_cookie;					/* cookie used to free PDU buffer */
	void (*free)(void *, void *);		/* free buf_ptr */

	u32 fi;
	u32 sn;
	u32 lsf;
	s32 n_li;							/* really the number of SDU */
	u32 li_s[RLC_LI_NUM_MAX];
	u8 *data_ptr;						/* the 1st SDU in PDU */
}rlc_am_pdu_segment_t;

typedef struct rlc_am_pdu_segment_info
{
	u16 start_offset;					/* sostart */
	u16 end_offset;						/* soend */
	u16 pdu_size;						/* save for fast access */
	u16 lsf;							/* last segment flag */
}rlc_am_pdu_segment_info_t;


/* AM Tx PDU control info */
typedef struct rlc_am_tx_pdu_ctrl
{
	dllist_node_t node;					/* link with next AM TX PDU */
	u8 *buf_ptr;						/* original PDU pointer */
	u16 pdu_size;						/* length of PDU */
	void *buf_cookie;					/* cookie used to free PDU buffer */
	void (*buf_free)(void *, void *);	/* function to free buf_ptr */

	u32 fi;
	u32 sn;
	s32 n_li;							/* really the number of SDU */
	u32 li_s[RLC_LI_NUM_MAX];
	u8 *data_ptr;						/* the 1st SDU in PDU */

	u32 RETX_COUNT;						/* RETX_COUNT defined in 36322 */
	
	/* received NACK and to be retransmitted segments */
	u32 i_retransmit_seg;				/* index to first segment */
	u32 n_retransmit_seg;				/* number of segments */
	rlc_am_pdu_segment_info_t retransmit_seg[RLC_SEG_NUM_MAX];
}rlc_am_tx_pdu_ctrl_t;

/* AM Rx PDU control info */
typedef struct rlc_am_rx_pdu_ctrl
{
	u32 delivery_offset;				/* before this offset, delivered SDU */
	u16 is_intact;						/* is whole PDU recieved */

	dllist_node_t rx_segq;				/* received but not delivered segments: rlc_am_pdu_segment_t */
}rlc_am_rx_pdu_ctrl_t;

typedef struct nacksn_info
{
	rlc_spdu_nacksn_t nacksn;
	rlc_spdu_so_t so;
}nacksn_info_t;

struct rlc_entity_am_rx;
struct rlc_entity_am_tx;

/* rlc am tx entity */
typedef struct rlc_entity_am_tx
{
	u16 type;							/* type of entity */
	u32 logical_chan;					/* logical channel id */
	void *userdata;						/* user data */
	
	struct rlc_entity_am_rx *amrx;		/* to brother */
	
	/* transmitting side state variables defined in 36322 */
	u16 VT_A;							/* VT(A) */
	u16 VT_MS;							/* VT(MS) */
	u16 VT_S;							/* VT(S) */
	u16 POLL_SN;						/* POLL_SN */
	u16 sn_max;							/* MAX SN */
	
	/* transmitting side counters defined in 36322 */
	u16 PDU_WITHOUT_POLL;				/* PDU_WITHOUT_POLL */
	u32 BYTE_WITHOUT_POLL;				/* BYTE_WITHOUT_POLL */
	
	/* transmitting side constant defined in 36322 */
	u16 AM_Window_Size;					/* AM_Window_Size */
	u16 maxRetxThreshold;
	u16 pollPDU;
	u16 pollByte;
	u16 poll_bit;
	
	/* transmitting side timer defined in 36322 */
	ptimer_t t_PollRetransmit;			/* t_PollRetransmit */
	ptimer_t t_StatusProhibit;			/* timer t-StatusProhibit: more reasonable to put it on Tx entity */
	
	/* For AM mode, need these functions to handle memory allocation */
	int (*max_retx_notify)(struct rlc_entity_am_tx *, u32);
	void (*free_pdu)(void *, void *);			/* function to free PDU and PDU segment */
	void (*free_sdu)(void *, void *);			/* function to free SDU */
	
	/* SDU queue */
	s32 sdu_total_size;					/* total size of SDU in Tx queue */
	s32 n_sdu;							/* number of SDU in Tx queue */
	dllist_node_t sdu_tx_q;				/* SDU Tx queue */
	
	/* Re-Tx queue: PDUs that are NACKed and need to re-transmit */
	dllist_node_t pdu_retx_q;			/* PDU Re-Tx queue */
	
	/* First Tx PDU: PDU that are waiting for ACK */
	rlc_am_tx_pdu_ctrl_t *txpdu[RLC_SN_FS_MAX];
	
	/* STATUS PDU */
	u32 status_pdu_triggered;
}rlc_entity_am_tx_t;

/* rlc am rx entity */
typedef struct rlc_entity_am_rx
{
	u16 type;
	u32 logical_chan;					/* logical channel id */
	void *userdata;						/* user data */
	
	struct rlc_entity_am_tx *amtx;		/* to brother */
	
	/* receiving side state variables defined in 36322 */
	u16 VR_R;							/* VR(R) */
	u16 VR_X;							/* VR(X) */
	u16 VR_H;							/* VR(H) */
	u16 VR_MS;							/* VR(MS) */
	u16 VR_MR;							/* VR(MR) */
	u16 sn_max;							/* MAX SN */
	
	/* receiving side counters defined in 36322 */
	u16 AM_Window_Size;					/* const AM_Window_Size */
	
	/* receiving side timer defined in 36322 */
	ptimer_t t_Reordering;				/* timer t-Reordering */
	ptimer_t t_StatusPdu;				/* timer to avoid t_PollRetransmit expires */
	
	/* For AM mode, need these functions to handle memory allocation */
	void (*deliv_sdu)(struct rlc_entity_am_rx *, rlc_sdu_t *);
	void (*free_pdu)(void *, void *);			/* function to free PDU and PDU segment */
	void (*free_sdu)(void *, void *);			/* function to free SDU */
	
	rlc_am_rx_pdu_ctrl_t *rxpdu[RLC_SN_FS_MAX];	/* reception buffer */
	
	dllist_node_t sdu_assembly_q;
	
	u32 n_discard_pdu;					/* counter: discarded PDUs */
	u32 n_good_pdu;
}rlc_entity_am_rx_t;

/* rlc am entity */
typedef struct rlc_entity_am
{
	rlc_entity_am_rx_t amrx;
	rlc_entity_am_tx_t amtx;
}rlc_entity_am_t;


/**********************************************************************/
/*                RLC general                                         */
/**********************************************************************/
#define RLC_ENTITY_TYPE_TM 0
#define RLC_ENTITY_TYPE_UM 1
#define RLC_ENTITY_TYPE_AM 2

typedef struct rlc_entity_common_head
{
	u16 type;							/* type of RLC entity: TM, UM or AM */
	u32 logical_chan;					/* logical channel id */
	void *userdata;						/* user data */
}rlc_entity_common_head_t;

typedef union rlc_entity_general
{
	rlc_entity_common_head_t rlc_common;
	rlc_entity_am_t rlc_am;
	rlc_entity_um_t rlc_um;
	rlc_entity_tm_t rlc_tm;
}rlc_entity_general_t;

typedef struct rlc_mem_counter
{
	u32 n_alloc_sdu;
	u32 n_free_sdu;
	u32 n_alloc_um_pdu;
	u32 n_free_um_pdu;
	u32 n_alloc_amrx_pdu;
	u32 n_free_amrx_pdu;
	u32 n_alloc_amtx_pdu;
	u32 n_free_amtx_pdu;
	u32 n_alloc_am_pdu_seg;
	u32 n_free_am_pdu_seg;
}rlc_mem_counter_t;

/**********************************************************************/
/*                RLC APIs and functions                              */
/**********************************************************************/

void rlc_timer_start(ptimer_t *timer);
void rlc_timer_stop(ptimer_t *timer);
int rlc_timer_is_running(ptimer_t *timer);
void rlc_timer_push(u32 time);

void rlc_init();

rlc_sdu_t *rlc_sdu_new();
void rlc_sdu_free(rlc_sdu_t *sdu);
void rlc_dump_sdu(rlc_sdu_t *sdu);
void rlc_serialize_sdu(u8 *data_ptr, rlc_sdu_t *sdu, u32 length);

void rlc_dump_mem_counter();

inline u32 rlc_li_len(u32 n_li);

u32 rlc_parse_li(u32 e, rlc_li_t *li_ptr, u32 size, u8 **data_ptr, u32 *li_s);
u32 rlc_build_li_from_sdu(u32 pdu_size, u32 head_len, dllist_node_t *sdu_q, u32 *li_s);
int rlc_encode_li(rlc_li_t * li_ptr, u32 n_li, u32 li_s[]);
int rlc_encode_sdu(u8 *data_ptr, u32 n_li, u32 li_s[], dllist_node_t *sdu_tx_q);


void rlc_tm_init(rlc_entity_tm_t *rlc_tm, void (*free_sdu)(void *, void *));
int rlc_tm_reestablish(rlc_entity_tm_t *rlctm);
int rlc_tm_rx_process_pdu(rlc_entity_tm_t *tmrx, u8 *buf_ptr, u32 buf_len, void *cookie);
int rlc_tm_tx_build_pdu(rlc_entity_tm_t *tmtx, rlc_sdu_t **out_sdu, u16 pdu_size);
u32 rlc_tm_tx_get_sdu_size(rlc_entity_tm_t *tmtx);
int rlc_tm_tx_sdu_enqueue(rlc_entity_tm_t *tmtx, u8 *buf_ptr, u32 sdu_size, void *cookie);

void rlc_um_init(rlc_entity_um_t *rlc_um, int sn_bits, u32 UM_Window_Size, u32 t_Reordering,
		void (*free_pdu)(void *, void *), void (*free_sdu)(void *, void *));
int rlc_um_rx_process_pdu(rlc_entity_um_rx_t *umrx, u8 *buf_ptr, u32 buf_len, void *cookie);
void rlc_um_rx_delivery_sdu(rlc_entity_um_rx_t *umrx, dllist_node_t *sdu_assembly_q);
int rlc_um_tx_build_pdu(rlc_entity_um_tx_t *umtx, u8 *buf_ptr, u16 pdu_size);
u32 rlc_um_tx_get_sdu_size(rlc_entity_um_tx_t *umtx);
int rlc_um_tx_sdu_enqueue(rlc_entity_um_tx_t *umtx, u8 *buf_ptr, u32 sdu_size, void *cookie);
void rlc_um_set_deliv_func(rlc_entity_um_t *rlc_um, void (*deliv_sdu)(struct rlc_entity_um_rx *, rlc_sdu_t *));
int rlc_um_reestablish(rlc_entity_um_t *rlcum);


void rlc_am_init(rlc_entity_am_t *rlc_am, 
					u32 t_Reordering, 
					u32 t_StatusPdu, 
					u32 t_StatusProhibit, 
					u32 t_PollRetransmit, 
					u16 maxRetxThreshold,
					u16 pollPDU,
					u16 pollByte,
					void (*free_pdu)(void *, void *),
					void (*free_sdu)(void *, void *));
int rlc_am_tx_sdu_enqueue(rlc_entity_am_tx_t *amtx, u8 *buf_ptr, u32 sdu_size, void *cookie);
u32 rlc_am_tx_get_status_pdu_size(rlc_entity_am_tx_t *amtx);
u32 rlc_am_tx_get_fresh_pdu_size(rlc_entity_am_tx_t *amtx);
u32 rlc_am_tx_get_retx_pdu_size(rlc_entity_am_tx_t *amtx);
u32 rlc_am_tx_get_sdu_size(rlc_entity_am_tx_t *amtx);
int rlc_am_tx_build_pdu(rlc_entity_am_tx_t *amtx, u8 *buf_ptr, u16 pdu_size, void *cookie, u32 *pdu_type);
int rlc_am_rx_process_pdu(rlc_entity_am_rx_t *amrx, u8 *buf_ptr, u32 buf_len, void *cookie);
int rlc_am_trigger_status_report(rlc_entity_am_rx_t *amrx, rlc_entity_am_tx_t *amtx, u16 sn, int forced);
void rlc_am_set_deliv_func(rlc_entity_am_t *rlc_am, void (*deliv_sdu)(struct rlc_entity_am_rx *, rlc_sdu_t *));
void rlc_am_set_maxretx_func(rlc_entity_am_t *rlc_am, int (*max_retx)(struct rlc_entity_am_tx *, u32));
int rlc_am_reestablish(rlc_entity_am_t *rlcam);


#endif //_RLC_H_

