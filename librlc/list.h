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
 * A double circled linked list
 *
 * @History
 * Phuuix Xiong, Create, 01-25-2011
 */
#ifndef __LIST_H__
#define __LIST_H__

typedef struct dllist_node
{
	struct dllist_node *prev;
	struct dllist_node *next;
	void *data;
}dllist_node_t;

#define DLLIST_EMPTY(list) ((list)->next == (list))

#define DLLIST_TAIL(list) ((list)->prev)
	
#define DLLIST_HEAD(list) ((list)->next)
	
#define DLLIST_IS_HEAD(list, node) \
	(((dllist_node_t *)(node) == (list)) ? 1 : 0)
		
void dllist_init(dllist_node_t *head);
void dllist_append(dllist_node_t *head, dllist_node_t *node);
void dllist_remove(dllist_node_t *head, dllist_node_t *node);

#endif //__LIST_H__

