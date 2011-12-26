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
 * A fast buffer allocator
 *
 * @History
 * Phuuix Xiong, Create, 01-25-2011
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "fastalloc.h"

/***********************************************************************************/
/* Function : fastalloc_destroy                                                    */
/***********************************************************************************/
/* Description : - Destroy a buffer pool                                           */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   base               | i  | pointer to buffer pool                              */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void fastalloc_destroy(fastalloc_t *base)
{
	if(base)
	{
		if(base->bufptr)
			free(base->bufptr);
		
#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_ELEMENT_INFO
		if(base->elemt_info)
			free(base->elemt_info);
#endif

#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_HISTORY
		if(base->history)
			free(base->history);
#endif

		memset(base, 0, sizeof(fastalloc_t));
		free(base);
	}
}

/***********************************************************************************/
/* Function : fastalloc_create                                                     */
/***********************************************************************************/
/* Description : - Cretae a buffer pool                                            */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   elemt_size         | i  | size of buffer element                              */
/*   elemt_num          | i  | the max number of buffer element in pool            */
/*   alignment_bits     | i  | byte alignment, (1~10)                              */
/*   max_history        | i  | max history stored, take effects only when          */
/*                             FASTALLOC_TRACK_LEVEL >= FASTALLOC_HISTORY          */
/*   Return             |    | pointer of buffer pool                              */
/***********************************************************************************/
fastalloc_t *fastalloc_create(u32 elemt_size, u32 elemt_num, u32 alignment_bits, u32 max_history)
{
	fastalloc_t *base;
	int i;
	u32 data_addr;
	u32 byte_alignment;
	
	/* process parameter */
	if(elemt_size <= 0 || elemt_num <= 0)
		return NULL;
	
	if(alignment_bits <= 0 || alignment_bits > 10)
		byte_alignment = FASTALLOC_DEFAULT_BYTE_ALIGNMENT;
	else
		byte_alignment = 0xFFFFFFFF >> (32-alignment_bits);
		
	elemt_size = (elemt_size + byte_alignment) & (~byte_alignment);
		
#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_HISTORY
	if(max_history <= 0)
		return NULL;
#endif

	base = malloc(sizeof(fastalloc_t));
	if(base == NULL)
		return NULL;
	
	memset(base, 0, sizeof(fastalloc_t));
	base->byte_align = byte_alignment;
	base->elemt_num = elemt_num;
	base->elemt_size = elemt_size;
	
	base->bufptr = malloc(base->elemt_size * (elemt_num + 1));
	if(base->bufptr == NULL)
	{
		fastalloc_destroy(base);
		return NULL;
	}
	
	base->elemt_stack = malloc(sizeof(u8 *) * elemt_num);
	if(base->elemt_stack == NULL)
	{
		fastalloc_destroy(base);
		return NULL;
	}
	
	data_addr = (u32)(base->bufptr + byte_alignment);
	data_addr = data_addr & (~byte_alignment);
	base->elemt_base = (u8 *)data_addr;
	base->sp = elemt_num - 1;
	for(i=0; i<elemt_num; i++)
	{
		base->elemt_stack[i] = data_addr;
		data_addr += elemt_size;
	}
	
#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_ELEMENT_INFO
	base->elemt_info = malloc(sizeof(fastalloc_elementinfo_t) * elemt_num);
	if(base->elemt_info == NULL)
	{
		fastalloc_destroy(base);
		return NULL;
	}
	memset(base->elemt_info, 0, sizeof(fastalloc_elementinfo_t) * elemt_num);
#endif

#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_HISTORY
	base->history = malloc(sizeof(fastalloc_history_t) * max_history);
	if(base->history == NULL)
	{
		fastalloc_destroy(base);
		return NULL;
	}
	/* set flags to uninitialized */
	for(i=0; i<max_history; i++)
	{
		base->history[i].flags = FASTALLOC_HISTORY_FUNINITIALIZED;
	}
	base->history_size = max_history;
#endif

	return base;
}

/***********************************************************************************/
/* Function : fastalloc_alloc                                                      */
/***********************************************************************************/
/* Description : - Allocate a buffer from pool                                     */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   base               | i  | pointer to buffer pool                              */
/*   Return             |    | pointer of buffer                                   */
/***********************************************************************************/
#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_ELEMENT_INFO
void *fastalloc_alloc(fastalloc_t *base, char *filename, u32 lineno)
#else
void *fastalloc_alloc(fastalloc_t *base)
#endif
{
	u8 *data = NULL;
	
	if(base == NULL)
		return NULL;

	/* sp always points to next empty element */
	if(base->sp)
	{
		base->sp --;
		data = (void *)base->elemt_stack[base->sp];
		base->alloc_cnt ++;
		
#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_ELEMENT_INFO
		u32 elemt_index;
		
		elemt_index = (data - base->elemt_base)/base->elemt_size;
		if((base->elemt_info[elemt_index].flags & 0x01) == 0x01)
		{
			ZLOG_ERR("data has been allocated: %p\n", data);
			return NULL;
		}
		base->elemt_info[elemt_index].flags |= 0x01;		//mark as allocated
		base->elemt_info[elemt_index].owner = 0;
		base->elemt_info[elemt_index].lineno = lineno;
		base->elemt_info[elemt_index].filename = filename;
#endif

#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_HISTORY
		base->history[base->history_index].flags = FASTALLOC_HISTORY_FALLOCATE;
		base->history[base->history_index].data = data;
		base->history[base->history_index].lineno = lineno;
		base->history[base->history_index].filename = filename;
		base->history_index = (base->history_index + 1)%(base->history_size);
#endif
	}

	return data;
}

/***********************************************************************************/
/* Function : fastalloc_free                                                       */
/***********************************************************************************/
/* Description : - Return a buffer to pool                                         */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   base               | i  | pointer to buffer pool                              */
/*   data               | i  | pointer of data                                     */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_ELEMENT_INFO
void fastalloc_free(fastalloc_t *base, void *data, char *filename, u32 lineno)
#else
void fastalloc_free(fastalloc_t *base, void *data)
#endif
{
	u8 *elemt;
	
	if(base == NULL)
		return;
	
	/* check data */
	if(data == NULL)
	{
		ZLOG_ERR("data == NULL\n");
		return;
	}
	
	elemt = data;
	if(elemt > base->elemt_base + base->elemt_size * base->elemt_num || elemt < base->elemt_base)
	{
		ZLOG_ERR("data exceed the max allowed value: %p\n", data);
		return;
	}
	
	if((u32)elemt & base->byte_align)
	{
		ZLOG_ERR("invalid data address: %p\n", data);
		return;
	}
	
#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_ELEMENT_INFO
	u32 elemt_index;
		
	elemt_index = (elemt - base->elemt_base)/base->elemt_size;
	
	if((base->elemt_info[elemt_index].flags & 0x01) == 0)
	{
		ZLOG_ERR("data hasn't been allocated: %p\n", data);
		return;
	}
	
	base->elemt_info[elemt_index].flags &= ~0x01;		//mark it as free
	base->elemt_info[elemt_index].owner = 0;
	base->elemt_info[elemt_index].lineno = lineno;
	base->elemt_info[elemt_index].filename = filename;
#endif

	base->elemt_stack[base->sp] = (u32)elemt;
	base->free_cnt ++;
	base->sp ++;
	
#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_HISTORY
	base->history[base->history_index].flags = FASTALLOC_HISTORY_FFREE;		
	base->history[base->history_index].data = data;
	base->history[base->history_index].lineno = lineno;
	base->history[base->history_index].filename = filename;
	base->history_index = (base->history_index + 1)%(base->history_size);
#endif
}

