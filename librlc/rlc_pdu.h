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
 *   RLC PDU format definitions.
 *
 * @History
 * Phuuix Xiong, Create, 01-25-2011
 */
#ifndef _RLC_PDU_H_
#define _RLC_PDU_H_

#define RLC_FI_FIRST_LAST 0x00
#define RLC_FI_FIRST_NLAST 0x01
#define RLC_FI_NFIRST_LAST 0x02
#define RLC_FI_NFIRST_NLAST 0x03

#define RLC_SN_MAX_5BITS ((1<<5)-1)
#define RLC_SN_MAX_10BITS ((1<<10)-1)

#define RLC_LI_VALUE_MAX 2047

#define RLC_AM_DC_CTRL_PDU 0
#define RLC_AM_DC_DATA_PDU 1

#define RLC_AM_SO_END 0x7FFF

#pragma pack(1)
#if (1) //__BYTE_ORDER == __BIG_ENDIANS
/* Need to define data stucture as below even RLC pairs both run on little endian CPU. */
typedef struct rlc_um_pdu_head_5bits
{
	u32 fi:2;
	u32 e:1;
	u32 sn:5;
}rlc_um_pdu_head_5bits_t;

typedef struct rlc_um_pdu_head_10bits
{
	u32 r1_1:1;
	u32 r1_2:1;
	u32 r1_3:1;
	u32 fi:2;
	u32 e:1;
	u32 sn:10;
}rlc_um_pdu_head_10bits_t;

typedef struct rlc_am_pdu_head
{
	u32 dc:1;
	u32 rf:1;
	u32 p:1;
	u32 fi:2;
	u32 e:1;
	u32 sn:10;
}rlc_am_pdu_head_t;

typedef struct rlc_am_pdu_segment_head
{
	u32 dc:1;
	u32 rf:1;
	u32 p:1;
	u32 fi:2;
	u32 e:1;
	u32 sn:10;
		
	u32 lsf:1;
	u32 so:15;
}rlc_am_pdu_segment_head_t;

typedef struct rlc_am_status_pdu_head
{
	u32 dc:1;
	u32 cpt:3;
	u32 ack_sn:10;
	u32 e1:1;
	u32 more:1;
}rlc_am_status_pdu_head_t;

typedef struct rlc_li
{
	u32 e1:1;
	u32 li1:11;
	u32 e2:1;
	u32 li2:11;
}rlc_li_t;

/* NACK_SN/E1/E2 set in Status PDU */
typedef struct rlc_spdu_nacksn
{
	u32 nack_sn:10;
	u32 e1:1;
	u32 e2:1;
	u32 reserved:20;
}rlc_spdu_nacksn_t;

/* SOstart/SOend in Status PDU */
typedef struct rlc_spdu_so
{
	u32 sostart:15;
	u32 soend:15;
	u32 reserved:2;
}rlc_spdu_so_t;

#else /* __BYTE_ORDER == __LITTLE_ENDIAN */ 
#warning "__LITTLE_ENDIAN__ won't work!!! Please enable __BIG_ENDIANS macro even RLC pairs both run on little endian CPU."

typedef struct rlc_um_pdu_head_5bits
{
	u32 sn:5;
	u32 e:1;
	u32 fi:2;
}rlc_um_pdu_head_5bits_t;

typedef struct rlc_um_pdu_head_10bits
{
	u32 sn:10;
	u32 e:1;
	u32 fi:2;
	u32 r1_3:1;
	u32 r1_2:1;
	u32 r1_1:1;
}rlc_um_pdu_head_10bits_t;

typedef struct rlc_am_pdu_head
{
	u32 sn:10;
	u32 e:1;
	u32 fi:2;
	u32 p:1;
	u32 rf:1;
	u32 dc:1;
}rlc_am_pdu_head_t;

typedef struct rlc_am_pdu_segment_head
{
	u32 so:15;
	u32 lsf:1;
		
	u32 sn:10;
	u32 e:1;
	u32 fi:2;
	u32 p:1;
	u32 rf:1;
	u32 dc:1;
}rlc_am_pdu_segment_head_t;

typedef struct rlc_am_status_pdu_head
{
	u32 more:1;
	u32 e1:1;
	u32 ack_sn:10;
	u32 cpt:3;
	u32 dc:1;
}rlc_am_status_pdu_head_t;

typedef struct rlc_li
{
	u32 li2:11;
	u32 e2:1;
	u32 li1:11;
	u32 e1:1;
}rlc_li_t;

/* NACK_SN/E1/E2 set in Status PDU */
typedef struct rlc_spdu_nacksn
{
	u32 reserved:20;
	u32 e2:1;
	u32 e1:1;
	u32 nack_sn:10;
}rlc_spdu_nacksn_t;

/* SOstart/SOend in Status PDU */
typedef struct rlc_spdu_so
{
	u32 reserved:2;
	u32 soend:15;
	u32 sostart:15;
}rlc_spdu_so_t;
#endif /* __BYTE_ORDER == __BIG_ENDIAN */
#pragma pack()

#endif //_RLC_PDU_H_
