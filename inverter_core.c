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
#define MAX_WRITE_SIZE		(128*1024)

MODULE_LICENSE("Dual BSD/GPL");

static dev_t my_dev;
static int inverter_devices;
static int inverter_major = INVERTER_MAJOR;
static int inverter_minor = INVERTER_MINOR;
static int inverter_open(struct inode *inode, struct file *filp);
static int inverter_release(struct inode *inode, struct file *filp);
static ssize_t inverter_write(struct file *f, const char __user *u,
			      size_t s, loff_t *t);
static loff_t inverter_llseek(struct file *f, loff_t l, int i);
static ssize_t inverter_read(struct file *f, char __user *u, size_t s,
			 loff_t *l);
static ssize_t __read_inverted(struct list_node *pnode, char __user *u,
			   size_t s, size_t dev_s, loff_t *f_pos);
static ssize_t __read_plain(struct list_node *pnode, char __user *u, size_t s,
			loff_t *f_pos);

struct inverter_dev {
	struct cdev cdev;
	struct list_node root;
	size_t size;
	int invert;
	unsigned long long _written;
	int _new_nodes;
};

static struct inverter_dev my_inverter_dev;

const struct file_operations inverter_fops = {
	.owner = THIS_MODULE,
	.open = inverter_open,
	.read = inverter_read,
	.write = inverter_write,
	.release = inverter_release,
	.llseek = inverter_llseek,
};

static void inverter_setup_cdev(struct inverter_dev *dev, int index)
{
	int err, devn = MKDEV(inverter_major, inverter_minor + index);
	cdev_init(&dev->cdev, &inverter_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &inverter_fops;
	err = cdev_add(&dev->cdev, devn, 1);
	if (err)
		printk(KERN_ALERT "Oh no! No device added.\n");
}

static int inverter_open(struct inode *inode, struct file *filp)
{
	struct inverter_dev *this_dev =
	    container_of(inode->i_cdev, struct inverter_dev, cdev);
	unsigned int major = imajor(inode);
	unsigned int minor = iminor(inode);
	filp->private_data = this_dev;
	this_dev->_written = 0;
	this_dev->_new_nodes = 0;
	pr_devel("Whoaa, opened! Major %i, minor %i.\n",
		    major, minor);
	pr_devel(KERN_ALERT
		    "Size of struct list_node is %zx. Just saying",
		    sizeof(struct list_node));
	if (filp->f_flags & O_RDWR)
		pr_devel("RDWR flag set.\n");
	if (filp->f_flags & O_TRUNC) {
		pr_devel("TRUNC flag set.\n");
		list_trunc(&this_dev->root);
		this_dev->size = 0;
	}
	if (filp->f_flags & O_APPEND)
		pr_devel("APPEND flag set.\n");
	this_dev->invert = !!minor;
	return 0;
}

static int inverter_release(struct inode *inode, struct file *filp)
{
	pr_devel(KERN_ALERT
		    "Whooo... released. Written %llu since open. %i new nodes. ",
		    my_inverter_dev._written, my_inverter_dev._new_nodes);
	return 0;
}

static ssize_t inverter_read(struct file *f, char __user *u, size_t s,
			 loff_t *f_pos)
{
	struct inverter_dev *this_dev = f->private_data;
	struct list_node *pnode = &(this_dev->root);
	pr_devel(KERN_ALERT
		    "inverter_read, size_t: %zx, major: %i, minor: %i, offset: %lld\n",
		    s, imajor(f->f_inode), iminor(f->f_inode), *f_pos);
	if (*f_pos > this_dev->size) {
		pr_devel(KERN_ALERT
			    "inverter_read, looking too far, failure.\n");
		return -1;
	}
	if (*f_pos + s > this_dev->size) {
		pr_devel(KERN_ALERT
			    "inverter_read, truncated. Size given: %zx, loff given: %lld\n",
			    s, *f_pos);
		s = this_dev->size - *f_pos;
		pr_devel("s became: %zx", s);
	}
	if (s == 0)
		return 0;

	if (this_dev->invert)
		return __read_inverted(pnode, u, s, this_dev->size, f_pos);
	else
		return __read_plain(pnode, u, s, f_pos);
}

static ssize_t __read_plain(struct list_node *pnode, char __user *u, size_t s,
			loff_t *f_pos)
{
	size_t s_save = s;
	size_t min;
	loff_t i;
	loff_t nodes_skip = (*f_pos) / INVERTER_NODE_SIZE;
	loff_t off = (*f_pos) % INVERTER_NODE_SIZE;

	for (i = 0; i < nodes_skip; i++) {
		if (pnode->next == NULL) {
			printk(KERN_ALERT
			       "Something's wrong, pnode->next points to NULL.");
			return -1;
		}
		pnode = pnode->next;
	}
	min =
	    s <
	    (INVERTER_NODE_SIZE - off) ? s : (INVERTER_NODE_SIZE - off);
	if (copy_to_user(u, pnode->data + off, min)) {
		printk(KERN_ALERT "__read_plain, copying failure.");
		return -EFAULT;
	}
	s -= min;
	while (s > 0) {
		if (pnode->next == NULL) {
			printk(KERN_ALERT
			       "Something's wrong, in copying loop pnode->next points to NULL.");
			return -1;
		}
		pnode = pnode->next;
		min = s < INVERTER_NODE_SIZE ? s : INVERTER_NODE_SIZE;
		if (copy_to_user(u + s_save - s, pnode->data, min)) {
			printk(KERN_ALERT
			       "__read_plain, copying failure.");
			return -EFAULT;
		}
		s -= min;
	}
	pr_devel("__read_plain, returning %zx", s_save);
	(*f_pos) += s_save;
	return s_save;
}

static ssize_t __read_inverted(struct list_node *pnode, char __user *u,
			   size_t s, size_t dev_s, loff_t *f_pos)
{
	size_t s_save = s;
	loff_t i, min;
	loff_t f_pos_inv = (loff_t)dev_s - (*f_pos) - 1;
	loff_t nodes_skip = f_pos_inv / INVERTER_NODE_SIZE;
	loff_t off = f_pos_inv % INVERTER_NODE_SIZE;

	pr_devel(KERN_ALERT
		    "__read_inverted, size is %zx, dev_s is %zx, off is %lld, nodes_skip is %lld",
		    s, dev_s, off, nodes_skip);
	for (i = 0; i < nodes_skip; i++) {
		if (pnode->next == NULL) {
			pr_devel(KERN_ALERT
				    "Something's wrong, pnode->next points to NULL.");
			return -1;
		}
		pr_devel("Rewinded.");
		pnode = pnode->next;
	}
	min = (off - (loff_t) s_save + 1) > 0 ? (off - (loff_t) s_save + 1) : 0;
	pr_devel(KERN_ALERT
		    "Trying to copy from i=%lld to i=%lld, size is %zx", off,
		    min, s);
	for (i = off; i >= min; i--) {
		/* pr_devel("Calling copy_to_user(u + %i, pnode->data + %i, 1)", (int)(s_save - s), i); */
		if (copy_to_user
		    (u + (int) (s_save - s), pnode->data + i, 1)) {
			printk(KERN_ALERT
			       "__read_inverted, copying failure.");
			return -EFAULT;
		}
		s--;
	}
	while (s > 0) {
		if (pnode->prev == NULL) {
			printk(KERN_ALERT
			       "Something's wrong, in copying loop pnode->prev points to NULL.");
			return -1;
		}
		pnode = pnode->prev;
		min =
		    (INVERTER_NODE_SIZE - (int) s) >
		    0 ? (INVERTER_NODE_SIZE - (int) s) : 0;
		/* pr_devel("Main loop: rewinded. Copying from i=%i to i=%i", INVERTER_NODE_SIZE - 1, min); */
		for (i = INVERTER_NODE_SIZE - 1; i >= min; i--) {
			if (copy_to_user
			    (u + (int) (s_save - s), pnode->data + i, 1)) {
				printk(KERN_ALERT
				       "__read_inverted, copying failure.");
				return -EFAULT;
			}
			s--;
		}
	}
	pr_devel("__read_inverted, returning %zx",
		    s_save);
	(*f_pos) += s_save;
	return s_save;
}

static ssize_t inverter_write(struct file *f, const char __user *u,
			      size_t s, loff_t *f_pos)
{
	loff_t off, nodes_skip;
	struct inverter_dev *this_dev = f->private_data;
	loff_t min, i;
	struct list_node *pnode = &(this_dev->root);
	const size_t s_save = s < MAX_WRITE_SIZE ? s : MAX_WRITE_SIZE;
	s = s_save;

	pr_devel(KERN_ALERT
		    "inverter_write, size_t: %zx, major: %i, minor: %i, offset given: %lld, internal offset: %lld",
		    s, imajor(f->f_inode), iminor(f->f_inode), *f_pos,
		    f->f_pos);

	if (this_dev->invert) {
		printk(KERN_ALERT "inverter_write, write not permitted.");
		return -EACCES;
	}
	if (f->f_flags & O_APPEND) {
		pr_devel(KERN_ALERT
			    "APPEND flag set. f_pos is now %zx\n",
			    this_dev->size);
		*f_pos = this_dev->size;
	}
	if (*f_pos > this_dev->size)
		return -EFBIG;
	off = (*f_pos) % INVERTER_NODE_SIZE;
	nodes_skip = (*f_pos) / INVERTER_NODE_SIZE;
	pr_devel("Offset is %lld, nodes to skip: %lld", off,
		    nodes_skip);
	for (i = 0; i < nodes_skip; i++) {
		if (pnode->next == NULL) {
			list_extend(pnode);
			this_dev->_new_nodes++;
		}
		pnode = pnode->next;
	}
	min =
	    (loff_t)s <
	    (INVERTER_NODE_SIZE - off) ? (loff_t)s : (INVERTER_NODE_SIZE - off);
	if (copy_from_user(pnode->data + off, u, min)) {
		printk(KERN_ALERT "inverter_write, copying failure.");
		return -EFAULT;
	}
	s -= min;
	while (s > 0) {
		if (pnode->next == NULL) {
			list_extend(pnode);
			this_dev->_new_nodes++;
		}
		pnode = pnode->next;
		min = s < INVERTER_NODE_SIZE ? s : INVERTER_NODE_SIZE;
		if (copy_from_user(pnode->data, u + s_save - s, min)) {
			printk(KERN_ALERT
			       "inverter_write, copying failure.");
			return -EFAULT;
		}
		s -= min;
	}

	*f_pos += s_save;
	this_dev->size = *f_pos;
	pr_devel(KERN_ALERT
		    "inverter_write, written %zx, given f_pos now is %lld, internal f_pos is %lld, bye!\n",
		    s_save, *f_pos, f->f_pos);
	this_dev->_written += s_save;
	return s_save;
}


static loff_t inverter_llseek(struct file *f, loff_t l, int whence)
{
	struct inverter_dev *this_dev = f->private_data;
	switch (whence) {
	case SEEK_SET:
		if (l >= this_dev->size)
			return -1;
		f->f_pos = l;
		break;
	case SEEK_CUR:
		if (f->f_pos + l >= this_dev->size)
			return -1;
		f->f_pos += l;
		break;
	case SEEK_END:
		if (l + this_dev->size >= this_dev->size)
			return -1;
		else
			f->f_pos = l + this_dev->size;
		break;
	}
	return f->f_pos;
}

static int inverter_init(void)
{
	int result;
	my_inverter_dev.root.prev = NULL;
	my_inverter_dev.root.next = NULL;
	inverter_devices = INVERTER_DEVICES;
	my_inverter_dev.size = 0;
	if (inverter_major) {
		my_dev = MKDEV(inverter_major, inverter_minor);
		result =
		    register_chrdev_region(my_dev, inverter_devices,
					   "inverter");
	} else {
		result =
		    alloc_chrdev_region(&my_dev, inverter_minor,
					inverter_devices, "inverter");
	}
	if (result < 0) {
		printk(KERN_ALERT
		       "Damn it, so wrong! No major number assigned.\n");
		return result;
	}
	inverter_major = MAJOR(my_dev);
	inverter_minor = MINOR(my_dev);
	inverter_setup_cdev(&my_inverter_dev, 0);
	inverter_setup_cdev(&my_inverter_dev, 1);
	pr_devel("Hello, world. Major: %i\n", MAJOR(my_dev));

	return 0;
}

static void inverter_exit(void)
{
	list_trunc(&my_inverter_dev.root);
	cdev_del(&(my_inverter_dev.cdev));
	unregister_chrdev_region(my_dev, 2);
	pr_devel("Goodbye, cruel world\n");
}

module_init(inverter_init);
module_exit(inverter_exit);
