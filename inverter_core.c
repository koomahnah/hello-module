#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include "list.h"

#define INVERTER_MAJOR		0
#define INVERTER_MINOR		0
#define INVERTER_DEVICES	2
#define MAX_WRITE_SIZE		128*1024
#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(args...)	printk(args)
#else
#define DEBUG_PRINT(args...)
#endif

MODULE_LICENSE("Dual BSD/GPL");

static dev_t my_dev;
static int inverter_devices;
static int inverter_major = INVERTER_MAJOR;
static int inverter_minor = INVERTER_MINOR;
static int inverter_open(struct inode *inode, struct file *filp);
static int inverter_release(struct inode *inode, struct file *filp); 
static ssize_t inverter_write(struct file *f, const char __user *u, size_t s, loff_t *t);
static loff_t inverter_llseek(struct file *f, loff_t l, int i);
static int inverter_read(struct file *f, char __user *u, size_t s, loff_t *l);
static int __read_inverted(struct list_node *pnode, char __user *u, size_t s, int dev_s, loff_t *f_pos);
static int __read_plain(struct list_node *pnode, char __user *u, size_t s, loff_t *f_pos);



struct inverter_dev{
	struct cdev cdev;
	struct list_node root;
	int size;
	int invert;
	unsigned long long _written;
	int _new_nodes;
};

static struct inverter_dev my_inverter_dev;

struct file_operations inverter_fops = {
	.owner =	THIS_MODULE,
	.open =		inverter_open,
	.read = 	inverter_read,
	.write = 	inverter_write,
	.release = 	inverter_release,
	.llseek =	inverter_llseek,
};
static void inverter_setup_cdev(struct inverter_dev *dev, int index){
	int err, devn = MKDEV(inverter_major, inverter_minor + index);
	cdev_init(&dev->cdev, &inverter_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &inverter_fops;
	err = cdev_add(&dev->cdev, devn, 1);
	if(err){
		printk(KERN_ALERT "Oh no! No device added.\n");
	}
}

static int inverter_open(struct inode *inode, struct file *filp){
	struct inverter_dev *this_dev = container_of(inode->i_cdev, struct inverter_dev, cdev);
	unsigned int major = imajor(inode);
	unsigned int minor = iminor(inode);
	filp->private_data = this_dev;
	this_dev->_written = 0;
	this_dev->_new_nodes = 0;
	DEBUG_PRINT(KERN_ALERT "Whoaa, opened! Major %i, minor %i.\n", major, minor);
	DEBUG_PRINT(KERN_ALERT "Size of struct list_node is %i. Just saying", sizeof(struct list_node));
	if(filp->f_flags & O_RDWR){
		DEBUG_PRINT(KERN_ALERT "RDWR flag set.\n");
	}
	if(filp->f_flags & O_TRUNC){
		DEBUG_PRINT(KERN_ALERT "TRUNC flag set.\n");
		list_trunc(&this_dev->root);
		this_dev->size = 0;
	}
	if(filp->f_flags & O_APPEND){
		DEBUG_PRINT(KERN_ALERT "APPEND flag set.\n");
	}
	if(minor)
		this_dev->invert = 1;
	else
		this_dev->invert = 0;
	return 0;
}
static int inverter_release(struct inode *inode, struct file *filp){
	DEBUG_PRINT(KERN_ALERT "Whooo... released. Written %llu since open. %i new nodes. ", my_inverter_dev._written, my_inverter_dev._new_nodes);
	return 0;
}

static int inverter_read(struct file *f, char __user *u, size_t s, loff_t *f_pos){
	struct inverter_dev *this_dev = f->private_data;
	struct list_node *pnode = &(this_dev->root);
	DEBUG_PRINT(KERN_ALERT "inverter_read, size_t: %i, major: %i, minor: %i, offset: %lld\n", s, imajor(f->f_inode), iminor(f->f_inode), *f_pos);
	if(*f_pos>this_dev->size){
		DEBUG_PRINT(KERN_ALERT "inverter_read, looking too far, failure.\n");
		return -1;
	}
	if(*f_pos+s>this_dev->size){
		DEBUG_PRINT(KERN_ALERT "inverter_read, truncated. Size given: %i, loff given: %lld\n", s, *f_pos);
		s = this_dev->size - *f_pos;
		DEBUG_PRINT(KERN_ALERT "s became: %i", s);
	}
	if(s == 0) return 0;
	
	if(this_dev->invert)
		return __read_inverted(pnode, u, s, this_dev->size, f_pos);
	else
		return __read_plain(pnode, u, s, f_pos);
}

static int __read_plain(struct list_node *pnode, char __user *u, size_t s, loff_t *f_pos){
	size_t s_save = s;
	size_t min;
	int i;
	int nodes_skip = (int)(*f_pos) / INVERTER_NODE_SIZE;
	int off = (int)(*f_pos) % INVERTER_NODE_SIZE;

	for(i=0;i<nodes_skip;i++){
		if(pnode->next == NULL){
			printk(KERN_ALERT "Something's wrong, pnode->next points to NULL.");
			return -1;
		}
		pnode = pnode->next;
	}
	min = s < (INVERTER_NODE_SIZE - off) ? s : (INVERTER_NODE_SIZE - off);
	if(copy_to_user(u, pnode->data + off, min)){
		printk(KERN_ALERT "__read_plain, copying failure.");
		return -EFAULT;
	}
	s -= min;
	while(s>0){
		if(pnode->next == NULL){
			printk(KERN_ALERT "Something's wrong, in copying loop pnode->next points to NULL.");
			return -1;
		}
		pnode = pnode->next;
		min = s < INVERTER_NODE_SIZE ? s : INVERTER_NODE_SIZE;
		if(copy_to_user(u + s_save - s, pnode->data, min)){
			printk(KERN_ALERT "__read_plain, copying failure.");
			return -EFAULT;
		}
		s -= min;
	}
	DEBUG_PRINT(KERN_ALERT "__read_plain, returning %i", (int)s_save);
	(*f_pos) += s_save;
	return s_save;
}

static int __read_inverted(struct list_node *pnode, char __user *u, size_t s, int dev_s, loff_t *f_pos){
	size_t s_save = s;
	int i, min;
	int f_pos_inv = dev_s - (int)(*f_pos) - 1;
	int nodes_skip = f_pos_inv / INVERTER_NODE_SIZE;
	int off = f_pos_inv % INVERTER_NODE_SIZE;

	DEBUG_PRINT(KERN_ALERT "__read_inverted, size is %i, dev_s is %i, off is %i, nodes_skip is %i", (int)s, dev_s, off, nodes_skip);
	for(i=0;i<nodes_skip;i++){
		if(pnode->next == NULL){
			DEBUG_PRINT(KERN_ALERT "Something's wrong, pnode->next points to NULL.");
			return -1;
		}
		DEBUG_PRINT(KERN_ALERT "Rewinded.");
		pnode = pnode->next;
	}
	min = (off - (int)s_save + 1) > 0 ? (off - (int)s_save + 1) : 0;
	DEBUG_PRINT(KERN_ALERT "Trying to copy from i=%i to i=%i, size is %i", off, min, (int)s);
	for(i=off;i>=min;i--){
		//DEBUG_PRINT(KERN_ALERT "Calling copy_to_user(u + %i, pnode->data + %i, 1)", (int)(s_save - s), i);
		if(copy_to_user(u + (int)(s_save - s), pnode->data + i, 1)){
			printk(KERN_ALERT "__read_inverted, copying failure.");
			return -EFAULT;
		}
		s--;
	}
	while(s > 0){
		if(pnode->prev == NULL){
			printk(KERN_ALERT "Something's wrong, in copying loop pnode->prev points to NULL.");
			return -1;
		}
		pnode = pnode->prev;
		min = (INVERTER_NODE_SIZE - (int)s) > 0 ? (INVERTER_NODE_SIZE - (int)s) : 0;
		//DEBUG_PRINT(KERN_ALERT "Main loop: rewinded. Copying from i=%i to i=%i", INVERTER_NODE_SIZE - 1, min);
		for(i=INVERTER_NODE_SIZE - 1;i>=min;i--){
			if(copy_to_user(u + (int)(s_save - s), pnode->data + i, 1)){
				printk(KERN_ALERT "__read_inverted, copying failure.");
				return -EFAULT;
			}
			s--;
		}
	}
	DEBUG_PRINT(KERN_ALERT "__read_inverted, returning %i", (int)s_save);
	(*f_pos) += s_save;
	return s_save;
}

static ssize_t inverter_write(struct file *f, const char __user *u, size_t s, loff_t *f_pos){
	int off, nodes_skip;
	struct inverter_dev *this_dev = f->private_data;
	int min, i;
	struct list_node *pnode = &(this_dev->root);
	const int s_save = s < MAX_WRITE_SIZE ? s : MAX_WRITE_SIZE;
	s = s_save;

	DEBUG_PRINT(KERN_ALERT "inverter_write, size_t: %i, major: %i, minor: %i, offset given: %lld, internal offset: %lld", s, imajor(f->f_inode), iminor(f->f_inode), *f_pos, f->f_pos);

	if(this_dev->invert){
		printk(KERN_ALERT "inverter_write, write not permitted.");
		return -EACCES;
	}
	if(f->f_flags & O_APPEND){
		DEBUG_PRINT(KERN_ALERT "APPEND flag set. f_pos is now %i\n", this_dev->size);
		*f_pos = this_dev->size;
	}
	if(*f_pos > this_dev->size)
		return -EFBIG;
	off = (int)(*f_pos) % INVERTER_NODE_SIZE;
	nodes_skip = (int)(*f_pos) / INVERTER_NODE_SIZE;
	DEBUG_PRINT(KERN_ALERT "Offset is %i, nodes to skip: %i", off, nodes_skip);
	for(i=0;i<nodes_skip;i++){
		if(pnode->next == NULL){
			list_extend(pnode);
			this_dev->_new_nodes++;
		}
		pnode = pnode->next;
	}
	min = s < (INVERTER_NODE_SIZE - off) ? s : (INVERTER_NODE_SIZE - off);
	if(copy_from_user(pnode->data + off, u, min)){
		printk(KERN_ALERT "inverter_write, copying failure.");
		return -EFAULT;
	}
	s -= min;
	while(s>0){
		if(pnode->next == NULL){
			list_extend(pnode);
			this_dev->_new_nodes++;
		}
		pnode = pnode->next;
		min = s < INVERTER_NODE_SIZE ? s : INVERTER_NODE_SIZE;
		if(copy_from_user(pnode->data, u + s_save - s, min)){
			printk(KERN_ALERT "inverter_write, copying failure.");
			return -EFAULT;
		}
		s -= min;
	}

	*f_pos += s_save;
	this_dev->size = *f_pos;
	DEBUG_PRINT(KERN_ALERT "inverter_write, written %i, given f_pos now is %lld, internal f_pos is %lld, bye!\n", s_save, *f_pos, f->f_pos);
	this_dev->_written += s_save;
	return s_save;
}


static loff_t inverter_llseek(struct file *f, loff_t l, int whence){
	struct inverter_dev *this_dev = f->private_data;
	switch(whence){
	case SEEK_SET:
		if(l >= this_dev->size)
			return -1;
		f->f_pos=l;
		break;
	case SEEK_CUR:
		if(f->f_pos + l >= this_dev->size)
			return -1;
		f->f_pos+=l;
		break;
	case SEEK_END:
		if(l+this_dev->size>=this_dev->size)
			return -1;
		else f->f_pos = l + this_dev->size;
		break;
	}
	return f->f_pos;
}
static int inverter_init(void)
{
	int result;
	int i;
	my_inverter_dev.root.prev = NULL;
	my_inverter_dev.root.next = NULL;
	for(i=0;i<INVERTER_NODE_SIZE;i++)
		my_inverter_dev.root.data[i] = 'x';
	inverter_devices = INVERTER_DEVICES;
	my_inverter_dev.size = 0;
	if(inverter_major){
		my_dev = MKDEV(inverter_major, inverter_minor);
		result = register_chrdev_region(my_dev, inverter_devices, "inverter");
	} else {
		result = alloc_chrdev_region(&my_dev, inverter_minor, inverter_devices, "inverter");
	}
	if(result < 0){
		printk(KERN_ALERT "Damn it, so wrong! No major number assigned.\n");
		return result;
	}
	inverter_major = MAJOR(my_dev);
	inverter_minor = MINOR(my_dev);
	inverter_setup_cdev(&my_inverter_dev, 0);
	inverter_setup_cdev(&my_inverter_dev, 1);
	DEBUG_PRINT(KERN_ALERT "Hello, world. Major: %i\n", MAJOR(my_dev));

	return 0;
}
static void inverter_exit(void)
{
	list_trunc(&my_inverter_dev.root);
	cdev_del(&(my_inverter_dev.cdev));
	unregister_chrdev_region(my_dev, 2);
	DEBUG_PRINT(KERN_ALERT "Goodbye, cruel world\n");
}
module_init(inverter_init);
module_exit(inverter_exit);
