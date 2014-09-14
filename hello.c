#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/gpio.h>

#define HELLO_MAJOR	0
#define HELLO_MINOR	0
#define HELLO_DEVICES	2
#define HELLO_DATA_SIZE	50

MODULE_LICENSE("Dual BSD/GPL");

static dev_t my_dev;
static int hello_devices;
static int hello_major = HELLO_MAJOR;
static int hello_minor = HELLO_MINOR;
static int hello_open(struct inode *inode, struct file *filp);
static int hello_release(struct inode *inode, struct file *filp); 
static int hello_read(struct file *f, char __user *u, size_t s, loff_t *l);
static ssize_t hello_write(struct file *f, const char __user *u, size_t s, loff_t *t);
static loff_t hello_llseek(struct file *f, loff_t l, int i);

struct hello_dev{
	struct cdev cdev;
	char hello_data[HELLO_DATA_SIZE];
	int hello_data_amount;
	int invert;
};

static struct hello_dev my_hello_dev;

struct file_operations hello_fops = {
	.owner =	THIS_MODULE,
	.open =		hello_open,
	.read = 	hello_read,
	.write = 	hello_write,
	.release = 	hello_release,
	.llseek =	hello_llseek,
};
static void hello_setup_cdev(struct hello_dev *dev, int index){
	int err, devn = MKDEV(hello_major, hello_minor + index);
	cdev_init(&dev->cdev, &hello_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &hello_fops;
	err = cdev_add(&dev->cdev, devn, 1);
	if(err)
		printk(KERN_ALERT "Oh no! No device added.\n");
}

static int hello_open(struct inode *inode, struct file *filp){
	struct hello_dev *this_dev = container_of(inode->i_cdev, struct hello_dev, cdev);
	unsigned int major = imajor(inode);
	unsigned int minor = iminor(inode);
	int i;
	filp->private_data = this_dev;
	printk(KERN_ALERT "Whoaa, opened! Major %i, minor %i.\n", major, minor);
	if(filp->f_flags & O_RDWR)
		printk(KERN_ALERT "RDWR flag set.\n");
	if(filp->f_flags & O_TRUNC){
		printk(KERN_ALERT "TRUNC flag set.\n");
		for(i=0;i<this_dev->hello_data_amount;i++)
			this_dev->hello_data[i] = 0;
		this_dev->hello_data_amount = 0;
	}
	if(filp->f_flags & O_APPEND)
		printk(KERN_ALERT "APPEND flag set.\n");
	if(minor)
		this_dev->invert = 1;
	else
		this_dev->invert = 0;
	return 0;
}
static int hello_release(struct inode *inode, struct file *filp){
	printk(KERN_ALERT "Whooo... released.\n");
	return 0;
}

static int hello_read(struct file *f, char __user *u, size_t s, loff_t *f_pos){
	char *buf = kmalloc(s, GFP_KERNEL);
	struct hello_dev *this_dev = f->private_data;
	loff_t i;
	printk(KERN_ALERT "Hello_read, size_t: %i, major: %i, minor: %i, offset: %lld\n", s, imajor(f->f_inode), iminor(f->f_inode), *f_pos);
	if(*f_pos>this_dev->hello_data_amount){
		printk(KERN_ALERT "Hello_read, looking too far, failure.\n");
		return -1;
	}
	if(*f_pos+s>this_dev->hello_data_amount){
		printk(KERN_ALERT "Hello_read, truncated. Size given: %i, loff given: %lld\n", s, *f_pos);
		s = this_dev->hello_data_amount - *f_pos;
		printk(KERN_ALERT "s became: %i", s);
	}
	if(this_dev->invert)
		for(i=0;i<s;i++)
			buf[s-i-1] = this_dev->hello_data[i+*f_pos];
	else
		for(i=0;i<s;i++)
			buf[i] = this_dev->hello_data[i+*f_pos];
	
	if(copy_to_user(u, buf, s)!=0){
		printk(KERN_ALERT "Hello_read, copying failure.\n");
		return -EFAULT;
	}
	(*f_pos)+=s;
	printk(KERN_ALERT "Hello_read, f_pos now is %lld, bye!\n", *f_pos);
	kfree(buf);
	return s;
}

static ssize_t hello_write(struct file *f, const char __user *u, size_t s, loff_t *f_pos){
	loff_t i = *f_pos;
	char *buf;
	struct hello_dev *this_dev = f->private_data;

	printk(KERN_ALERT "Hello_write, size_t: %i, major: %i, minor: %i, offset given: %lld, internal offset: %lld", s, imajor(f->f_inode), iminor(f->f_inode), *f_pos, f->f_pos);
	if(f->f_flags & O_RDWR)
		printk(KERN_ALERT "RDWR flag set.\n");
	if(f->f_flags & O_TRUNC)
		printk(KERN_ALERT "TRUNC flag set.\n");
	if(f->f_flags & O_APPEND)
		printk(KERN_ALERT "APPEND flag set.\n");

	if((f->f_flags & O_APPEND) && ((this_dev->hello_data_amount+s)>=HELLO_DATA_SIZE)){
		printk(KERN_ALERT "Too much data to append. Old s: %i, new s: %i", s, HELLO_DATA_SIZE - this_dev->hello_data_amount);
		s = HELLO_DATA_SIZE - this_dev->hello_data_amount;
		if(s==0){
			return -EFBIG;
		}
		*f_pos = this_dev->hello_data_amount;
	}
	else if(f->f_flags & O_APPEND){
		*f_pos = this_dev->hello_data_amount;
	}
	else if(*f_pos+s>=HELLO_DATA_SIZE){
		printk(KERN_ALERT "Too much data to write. f_pos given: %lld, s given: %i, new s: %lld\n", *f_pos, s, HELLO_DATA_SIZE - *f_pos);
		s = HELLO_DATA_SIZE - *f_pos;
		if(s<=0){
			printk(KERN_ALERT "Nothing to write. Returning -1");
			return -EFBIG;
		}
	}

	buf = kmalloc(s, GFP_KERNEL);
	if(buf==NULL){
		printk(KERN_ALERT "Hello_write, kmalloc failure.\n");
		*f_pos = i;
		return -1;
	}
	printk(KERN_ALERT "Copying, size %i\n", s);
	if(copy_from_user(buf, u, s)!=0){
		printk(KERN_ALERT "Hello_write, copying failure.\n");
		*f_pos = i;
		return -EFAULT;
	}
	printk(KERN_ALERT "Data start\n");
	for(i=0;i<s;i++){
		this_dev->hello_data[i+(*f_pos)] = buf[i];
		printk(KERN_ALERT "%lld. Data written at %lld: %c", i, i+(*f_pos), buf[i]);
		}
	(*f_pos)+=s;
	this_dev->hello_data_amount += s;
	printk(KERN_ALERT "Data end\n");

	printk(KERN_ALERT "Hello_write, given f_pos now is %lld, internal f_pos is %lld, bye!\n", *f_pos, f->f_pos);
	kfree(buf);
	return s;
}


static loff_t hello_llseek(struct file *f, loff_t l, int whence){
	struct hello_dev *this_dev = f->private_data;
	switch(whence){
	case SEEK_SET:
		if(l >= HELLO_DATA_SIZE)
			return -1;
		f->f_pos=l;
		break;
	case SEEK_CUR:
		if(f->f_pos + l >= HELLO_DATA_SIZE)
			return -1;
		f->f_pos+=l;
		break;
	case SEEK_END:
		if(l+this_dev->hello_data_amount>=HELLO_DATA_SIZE)
			return -1;
		else f->f_pos = l + this_dev->hello_data_amount;
		break;
	}
	return f->f_pos;
}
static int hello_init(void)
{
	int result;
	int i;
	hello_devices = HELLO_DEVICES;
	my_hello_dev.hello_data_amount = 0;
	if(hello_major){
		my_dev = MKDEV(hello_major, hello_minor);
		result = register_chrdev_region(my_dev, hello_devices, "hello");
	} else {
		result = alloc_chrdev_region(&my_dev, hello_minor, hello_devices, "hello");
	}
	if(result < 0){
		printk(KERN_ALERT "Damn it, so wrong! No major number assigned.\n");
		return result;
	}
	hello_major = MAJOR(my_dev);
	hello_minor = MINOR(my_dev);
	hello_setup_cdev(&my_hello_dev, 0);
	hello_setup_cdev(&my_hello_dev, 1);
	for(i=0;i<HELLO_DATA_SIZE;i++)
		my_hello_dev.hello_data[i] = 0;
	printk(KERN_ALERT "Hello, world. Major: %i\n", MAJOR(my_dev));

	return 0;
}
static void hello_exit(void)
{
	cdev_del(&(my_hello_dev.cdev));
	unregister_chrdev_region(my_dev, 2);
	printk(KERN_ALERT "Goodbye, cruel world\n");
}
module_init(hello_init);
module_exit(hello_exit);
