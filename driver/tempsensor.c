/*
 * tempsensor.c - Simulated temperature sensor character device driver
 *
 * Creates /dev/tempsensor. Each read() returns the current simulated
 * temperature (as a text string, tenths of a degree Celsius, e.g. "45.3\n").
 * The value does a random walk so it drifts realistically over time.
 *
 * Supports two ioctl commands (see tempsensor_ioctl.h):
 *   TEMP_IOC_RESET      - reset temperature to baseline (25.0 C)
 *   TEMP_IOC_SET_DRIFT   - set how much the value can move per read
 *
 * Capstone teaching driver - not for production use.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/version.h>
#include "tempsensor_ioctl.h"

#define DEVICE_NAME "tempsensor"
#define CLASS_NAME  "tempsensor_class"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Capstone Project");
MODULE_DESCRIPTION("Simulated temperature sensor character device");

static dev_t dev_num;
static struct cdev temp_cdev;
static struct class *temp_class;
static struct device *temp_device;
static DEFINE_MUTEX(temp_lock);

/* State: current temperature in tenths of a degree C, and drift step */
static int current_temp_tenths = 250;   /* 25.0 C baseline */
static int drift_tenths = 10;           /* default: up to 1.0 C swing per read */

static int temp_open(struct inode *inode, struct file *file)
{
    pr_info("tempsensor: device opened\n");
    return 0;
}

static int temp_release(struct inode *inode, struct file *file)
{
    pr_info("tempsensor: device closed\n");
    return 0;
}

static ssize_t temp_read(struct file *file, char __user *buf,
                          size_t count, loff_t *ppos)
{
    char out[16];
    int len;
    int step;
    unsigned int r;

    if (*ppos > 0)
        return 0; /* one reading per open/read cycle for simple `cat` usage */

    mutex_lock(&temp_lock);

    /* random walk: step in [-drift_tenths, +drift_tenths] */
    get_random_bytes(&r, sizeof(r));
    step = (int)(r % (2 * drift_tenths + 1)) - drift_tenths;
    current_temp_tenths += step;

    /* clamp to a believable range: 0.0 C to 120.0 C */
    if (current_temp_tenths < 0)
        current_temp_tenths = 0;
    if (current_temp_tenths > 1200)
        current_temp_tenths = 1200;

    len = scnprintf(out, sizeof(out), "%d.%d\n",
                     current_temp_tenths / 10, current_temp_tenths % 10);

    mutex_unlock(&temp_lock);

    if (count < len)
        return -EINVAL;
    if (copy_to_user(buf, out, len))
        return -EFAULT;

    *ppos += len;
    return len;
}

static long temp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int new_drift;

    switch (cmd) {
    case TEMP_IOC_RESET:
        mutex_lock(&temp_lock);
        current_temp_tenths = 250;
        mutex_unlock(&temp_lock);
        pr_info("tempsensor: reset to baseline (25.0 C)\n");
        break;

    case TEMP_IOC_SET_DRIFT:
        if (copy_from_user(&new_drift, (int __user *)arg, sizeof(int)))
            return -EFAULT;
        if (new_drift < 0 || new_drift > 500)
            return -EINVAL;
        mutex_lock(&temp_lock);
        drift_tenths = new_drift;
        mutex_unlock(&temp_lock);
        pr_info("tempsensor: drift set to %d.%d C per read\n",
                new_drift / 10, new_drift % 10);
        break;

    default:
        return -ENOTTY;
    }
    return 0;
}

static const struct file_operations temp_fops = {
    .owner          = THIS_MODULE,
    .open           = temp_open,
    .release        = temp_release,
    .read           = temp_read,
    .unlocked_ioctl = temp_ioctl,
};

static int __init tempsensor_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("tempsensor: failed to allocate device number\n");
        return ret;
    }

    cdev_init(&temp_cdev, &temp_fops);
    ret = cdev_add(&temp_cdev, dev_num, 1);
    if (ret < 0) {
        unregister_chrdev_region(dev_num, 1);
        pr_err("tempsensor: failed to add cdev\n");
        return ret;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    temp_class = class_create(CLASS_NAME);
#else
    temp_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(temp_class)) {
        cdev_del(&temp_cdev);
        unregister_chrdev_region(dev_num, 1);
        pr_err("tempsensor: failed to create class\n");
        return PTR_ERR(temp_class);
    }

    temp_device = device_create(temp_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(temp_device)) {
        class_destroy(temp_class);
        cdev_del(&temp_cdev);
        unregister_chrdev_region(dev_num, 1);
        pr_err("tempsensor: failed to create device\n");
        return PTR_ERR(temp_device);
    }

    pr_info("tempsensor: module loaded, /dev/%s ready (major=%d minor=%d)\n",
            DEVICE_NAME, MAJOR(dev_num), MINOR(dev_num));
    return 0;
}

static void __exit tempsensor_exit(void)
{
    device_destroy(temp_class, dev_num);
    class_destroy(temp_class);
    cdev_del(&temp_cdev);
    unregister_chrdev_region(dev_num, 1);
    pr_info("tempsensor: module unloaded\n");
}

module_init(tempsensor_init);
module_exit(tempsensor_exit);
