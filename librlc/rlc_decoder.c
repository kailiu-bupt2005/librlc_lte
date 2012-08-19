/* rlc_decoder.c
 * decode RLC PDU in Hex into text info.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>

#include "rlc.h"

static u8 rlc_pdu[10000];

static char *rlc_mode_str[] = {
	"AM",
	"UM 5 Bits SN",
	"UM 10 Bits SN",
};

extern void bitcpy(unsigned long *dst, int dst_idx, const unsigned long *src, int src_idx, u32 n);

int read_one_byte(FILE *fp, u8 *byte)
{
	char hex[2], c;
	u32 idx = 0;
	u32 n;
	
	/* convert Hex text to Hex  value */
	n = fread(&c, 1, 1, fp);
	while(n == 1)
	{
		c = (char)toupper((int)c);
		
		// ignore 0X
		if((c == 'X') && (idx == 1) && (hex[0]==0))
			idx = 0;

		// ignore all non Hex digit
		if(isxdigit(c))
		{
			if(isdigit(c))
				hex[idx++] = c-'0';
			else
				hex[idx++] = c-'A'+10;
		}

		if(idx < 2)
			n = fread(&c, 1, 1, fp);
		else
			break;
	}

	if(idx > 1)
	{
		*byte = hex[0] << 4 | hex[1];
		return 1;
	}

	return 0;
}

void decode_rlc_um5(u8 *pdu, u32 nByte)
{
	rlc_um_pdu_head_5bits_t *um_pdu;
	u32 li_s[RLC_LI_NUM_MAX];
	u32 n_li = 0;
	u8 *data_ptr;
	u8 i;
	u8 headlen = 1;

	um_pdu = (rlc_um_pdu_head_5bits_t *)pdu;

	printf("UM PDU: FI=%u E=%u SN=%u\n", um_pdu->fi, um_pdu->e, um_pdu->sn);

	n_li = rlc_parse_li(um_pdu->e, (rlc_li_t *)&pdu[headlen], nByte-headlen, &data_ptr, li_s);
	for(i=0; i<n_li; i++)
		printf("  LI %u: %u\n", i+1, li_s[i]);
}

void decode_rlc_um10(u8 *pdu, u32 nByte)
{
	rlc_um_pdu_head_10bits_t *um_pdu;
	u32 li_s[RLC_LI_NUM_MAX];
	u32 n_li = 0;
	u8 *data_ptr;
	u8 i;
	u8 headlen = 2;

	um_pdu = (rlc_um_pdu_head_10bits_t *)pdu;

	printf("UM PDU: FI=%u E=%u SN=%u\n", um_pdu->fi, um_pdu->e, um_pdu->sn);

	n_li = rlc_parse_li(um_pdu->e, (rlc_li_t *)&pdu[headlen], nByte-headlen, &data_ptr, li_s);
	for(i=0; i<n_li; i++)
		printf("  LI %u: %u\n", i+1, li_s[i]);
}

#define MAXINFO_NUM 128
void decode_rlc_am_status_pdu(u8 *pdu, u32 nByte)
{
	rlc_am_status_pdu_head_t *am_pdu;
	u32 n=0;
	u8 i;
	u32 bit_offset = 15;
	u32 e1;
	nacksn_info_t ninfo[MAXINFO_NUM];
	u8 *buf_ptr = pdu;
	u16 nack_sn;

	am_pdu = (rlc_am_status_pdu_head_t *)pdu;
	printf("AM Status PDU: CPT=%u ACK_SN=%u E1=%u\n", am_pdu->cpt, am_pdu->ack_sn, am_pdu->e1);

	e1 = am_pdu->e1;
	
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
		if((buf_ptr-pdu) > nByte)
		{
			printf("NACK info exceeds the buf_len=%u\n", nByte);
			break;
		}
		
		n++;
	}

	for(i=0; i<n; i++)
	{
		printf("  NACK_SN=%u ", ninfo[i].nacksn.nack_sn);
		if(ninfo[i].nacksn.e2)
			printf("SO_Start=%u SO_End=%u\n", ninfo[i].so.sostart, ninfo[i].so.soend);
		else
			printf("\n");
	}
}

void decode_rlc_am_data_pdu(u8 *pdu, u32 nByte)
{
	rlc_am_pdu_head_t *am_pdu;
	u32 li_s[RLC_LI_NUM_MAX];
	u32 n_li = 0;
	u8 *data_ptr;
	u8 i;
	u8 headlen = 2;

	am_pdu = (rlc_am_pdu_head_t *)pdu;

	printf("AM Data PDU: RF=%u Poll=%u FI=%u E=%u sn=%u\n", am_pdu->rf, am_pdu->p, am_pdu->fi, am_pdu->e, am_pdu->sn);

	n_li = rlc_parse_li(am_pdu->e, (rlc_li_t *)&pdu[headlen], nByte-headlen, &data_ptr, li_s);
	for(i=0; i<n_li; i++)
		printf("  LI %u: %u\n", i+1, li_s[i]);
}

void decode_rlc_am_data_pdu_reseg(u8 *pdu, u32 nByte)
{
	rlc_am_pdu_segment_head_t *am_pdu;
	u32 li_s[RLC_LI_NUM_MAX];
	u32 n_li = 0;
	u8 *data_ptr;
	u8 i;
	u8 headlen = 4;
	
	am_pdu = (rlc_am_pdu_segment_head_t *)pdu;

	printf("AM Data PDU: RF=%u Poll=%u FI=%u E=%u sn=%u LSF=%u SO=%u\n", 
			am_pdu->rf, am_pdu->p, am_pdu->fi, am_pdu->e, am_pdu->sn, am_pdu->lsf, am_pdu->so);

	n_li = rlc_parse_li(am_pdu->e, (rlc_li_t *)&pdu[headlen], nByte-headlen, &data_ptr, li_s);
	for(i=0; i<n_li; i++)
		printf("  LI %u: %u\n", i+1, li_s[i]);
}

void decode_rlc_am(u8 *pdu, u32 nByte)
{
	rlc_am_pdu_head_t *am_pdu;

	am_pdu = (rlc_am_pdu_head_t *)pdu;

	if(am_pdu->dc == 0)
		decode_rlc_am_status_pdu(pdu, nByte);
	else if(am_pdu->rf == 0)
	{
		if(nByte > 2) 
			decode_rlc_am_data_pdu(pdu, nByte);
	}
	else
	{
		if(nByte > 4)
			decode_rlc_am_data_pdu_reseg(pdu, nByte);
	}
}

int main(int argc, char *argv[])
{
	FILE *fp;
	u32 nByte = 0;
	u32 rlc_mode = 0;

	if(argc < 2)
	{
		printf("rlc_decoder <PDU Hex file> [0 (AM)| 1 (UM 5) | 2 (UM 10)]\n");
		return 0;
	}

	if(argc >= 3)
		sscanf(argv[2], "%d", &rlc_mode);

	if(rlc_mode > 2) rlc_mode = 0;
	printf("RLC MODE: %s\n", rlc_mode_str[rlc_mode]);
	
	fp = fopen(argv[1], "r");
	if(fp == NULL)
	{
		printf("Can't open PDU Hex file: %s\n", argv[1]);
		return 0;
	}

	/* read hex into rlc_pdu */
	while(read_one_byte(fp, &rlc_pdu[nByte]))
		nByte ++;

	fclose(fp);

	switch(rlc_mode)
	{
		case 1:
			if(nByte > 2) 
				decode_rlc_um5(rlc_pdu, nByte);
			break;
		case 2:
			if(nByte > 2)
				decode_rlc_um10(rlc_pdu, nByte);
			break;
		default:
			if(nByte >= 2)
				decode_rlc_am(rlc_pdu, nByte);
			break;
	}
	
	return 0;
}


