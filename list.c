#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include "list.h"

struct list_node* list_extend(struct list_node *pnode){
	struct list_node *new_node;
	int i;
	if(pnode->next != NULL){
		printk(KERN_ALERT "Trying to extend already extended. Nothing done.");
		return NULL;
	}
	new_node = kmalloc(sizeof(struct list_node), GFP_KERNEL);
	if(new_node == NULL) return NULL;
	pnode->next = new_node;
	new_node->prev = pnode;
	new_node->next = NULL;
	for(i=0;i<INVERTER_NODE_SIZE;i++)
		new_node->data[i] = 'e';
	return new_node;
}

// removes everything that's after pnode
void list_trunc(struct list_node *pnode){
	struct list_node *tmpnode = pnode;
	int freed = 0;
	while(tmpnode->next != NULL)
		tmpnode = tmpnode->next;
	while(tmpnode != pnode){
		tmpnode = tmpnode->prev;
		freed++;
		kfree(tmpnode->next);
	}
	printk(KERN_ALERT "list_trunc, %i nodes freed.", freed);
	pnode->next = NULL;
}
