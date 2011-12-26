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
 * A fast buffer allocator (head file)
 *
 * @History
 * Phuuix Xiong, Create, 01-25-2011
 */

#ifndef _FASTALLOC_H_
#define _FASTALLOC_H_

#include "stdtypes.h"

#define FASTALLOC_DEFAULT_BYTE_ALIGNMENT 31

#define FASTALLOC_HISTORY_FUNINITIALIZED 0
#define FASTALLOC_HISTORY_FALLOCATE 1
#define FASTALLOC_HISTORY_FFREE 2

#define FASTALLOC_ELEMENT_INFO 1
#define FASTALLOC_HISTORY 2
#define FASTALLOC_TRACK_LEVEL FASTALLOC_HISTORY


/* for tracking memory leakage purpose */
typedef struct fastalloc_elemtinfo
{
	u16 flags;
	u16 owner;
	u32 lineno;
	char *filename;
}fastalloc_elementinfo_t;

typedef struct fastalloc_history
{
	u32 flags;
	void *data;
	char *filename;
	u32 lineno;
}fastalloc_history_t;

typedef struct fastalloc
{
	u16 elemt_size;
	u16 byte_align;
	u32 elemt_num;
	
	u8 *bufptr;
	u8 *elemt_base;
	u32 *elemt_stack;
	u32 sp;
	
	u32 alloc_cnt;
	u32 free_cnt;
	
	fastalloc_elementinfo_t *elemt_info;
	u32 history_size;
	u32 history_index;
	fastalloc_history_t *history;
}fastalloc_t;

#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_ELEMENT_INFO
#define FASTALLOC(base) \
	fastalloc_alloc((base), __FILE__, __LINE__)

#define FASTFREE(base, data) \
	fastalloc_free((base), (data), __FILE__, __LINE__)
#else
#define FASTALLOC(base) \
	fastalloc_alloc(base)

#define FASTFREE(base, data) \
	fastalloc_free((base), (data))
#endif

void fastalloc_destroy(fastalloc_t *base);
fastalloc_t *fastalloc_create(u32 elemt_size, u32 elemt_num, u32 alignment_bits, u32 max_history);

#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_ELEMENT_INFO
void *fastalloc_alloc(fastalloc_t *base, char *filename, u32 lineno);
#else
void *fastalloc_alloc(fastalloc_t *base);
#endif
#if FASTALLOC_TRACK_LEVEL >= FASTALLOC_ELEMENT_INFO
void fastalloc_free(fastalloc_t *base, void *data, char *filename, u32 lineno);
#else
void fastalloc_free(fastalloc_t *base, void *data);
#endif

#endif /* _FASTALLOC_H_ */

