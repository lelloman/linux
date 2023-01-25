#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include "internal.h"

#define BUFSIZE 512

static bool enabled = true;
static unsigned long int low_ratio_numerator = 3;
static unsigned long int low_ratio_denominator = 8;
static unsigned long int boost_minwm_ratio = 2;

static ssize_t enabled_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
    int len;
    char buf[BUFSIZE];
     if (*ppos > 0)
         return 0;
    if (count < BUFSIZE)
        return -EFAULT;
    
    len = sprintf(buf, "enabled = %s\n", enabled ? "YES" : "no");

    if (copy_to_user(ubuf, buf, len))
        return -EFAULT;

    *ppos = len;
    return len;
}

static ssize_t enabled_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
    struct zone *zone;
    int scanned_values;
    int in_enabled;

    char buf[BUFSIZE];
    if (*ppos > 0 || count > BUFSIZE || count == 0)
        return 0;

    if (copy_from_user(buf, ubuf, count))
        return -EFAULT;

    scanned_values = sscanf(buf, "%d", &in_enabled);
    if (scanned_values != 1)
        return -EINVAL;
    
    enabled = in_enabled != 0;
    printk("atomic boost set enabled %d\n", enabled);
    if (!enabled) {
        for_each_populated_zone(zone) {
            zone->atomic_boost = 0;
        }
    }
    return count;    
}

static struct proc_ops enabled_ops =
{
    .proc_write = enabled_write,
    .proc_read = enabled_read
};

static ssize_t boost_minwm_ratio_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
    int len;
    char buf[BUFSIZE];
     if (*ppos > 0)
         return 0;
    if (count < BUFSIZE)
        return -EFAULT;
    
    len = sprintf(buf, "boost:minwm = %ld:1\n", boost_minwm_ratio);

    if (copy_to_user(ubuf, buf, len))
        return -EFAULT;

    *ppos = len;
    return len;
}

static ssize_t boost_minwm_ratio_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
    int scanned_values;
    unsigned long int in_boost_minwm_ratio;

    char buf[BUFSIZE];
    if (*ppos > 0 || count > BUFSIZE || count == 0)
        return 0;

    if (copy_from_user(buf, ubuf, count))
        return -EFAULT;

    scanned_values = sscanf(buf, "%ld", &in_boost_minwm_ratio);
    if (scanned_values != 1)
        return -EINVAL;

    boost_minwm_ratio = in_boost_minwm_ratio;
    printk("atomic boost set boost:minwm ratio to %ld:1\n", boost_minwm_ratio);
    return count;    
}

static struct proc_ops boost_minwm_ratio_ops = 
{
	.proc_write = boost_minwm_ratio_write,
    .proc_read = boost_minwm_ratio_read
};

static ssize_t low_ratio_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
    int len;
    char buf[BUFSIZE];
     if (*ppos > 0)
         return 0;
    if (count < BUFSIZE)
        return -EFAULT;
    
    len = sprintf(buf, "minw ratio = %ld/%ld\n", low_ratio_numerator, low_ratio_denominator);

    if (copy_to_user(ubuf, buf, len))
        return -EFAULT;

    *ppos = len;
    return len;
}

static ssize_t low_ratio_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
    int scanned_values;
    unsigned long int in_low_ratio_numerator, in_low_ratio_denominator;

    char buf[BUFSIZE];
    if (*ppos > 0 || count > BUFSIZE || count == 0)
        return 0;

    if (copy_from_user(buf, ubuf, count))
        return -EFAULT;

    scanned_values = sscanf(buf, "%ld/%ld", &in_low_ratio_numerator, &in_low_ratio_denominator);
    if (scanned_values != 2)
        return -EINVAL;

    low_ratio_numerator = in_low_ratio_numerator;
    low_ratio_denominator = in_low_ratio_denominator;
    printk("atomic boost set minwm ratio to %ld/%ld\n", low_ratio_numerator, low_ratio_denominator);
    return count;    
}

static struct proc_ops low_ratio_ops = 
{
	.proc_write = low_ratio_write,
    .proc_read = low_ratio_read
};

static void __handle_atomic_boost(struct zone* zone)
{
    unsigned long int min_wmark_pages, free_pages;
    if (zone->atomic_boost)
        return;

    min_wmark_pages = min_wmark_pages(zone);
    free_pages = zone_page_state(zone, NR_FREE_PAGES);
    if (free_pages * low_ratio_denominator < min_wmark_pages * low_ratio_numerator) {
        zone->atomic_boost = min_wmark_pages * boost_minwm_ratio;
        printk("atomic boosting zone %s to %ld with %ld minwm\n", zone->name, zone->atomic_boost, min_wmark_pages);
    }
}

static const char *root_dir_name = "atomic_boost";
static struct proc_dir_entry* root_dir_entry;

static const char *low_ratio_name = "low_ratio";
static struct proc_dir_entry *low_ratio_entry;

static const char *enabled_name = "enabled";
static struct proc_dir_entry *enabled_entry;

static const char *boost_minwm_ratio_name = "boost_minwm_ratio";
static struct proc_dir_entry *boost_minwm_ratio_entry;

static int __init atomic_boost_init(void)
{
    root_dir_entry = proc_mkdir(root_dir_name, NULL);
    boost_minwm_ratio_entry = proc_create(boost_minwm_ratio_name, 0660, root_dir_entry, &boost_minwm_ratio_ops);
    enabled_entry = proc_create(enabled_name, 0660, root_dir_entry, &enabled_ops);
    low_ratio_entry = proc_create(low_ratio_name, 0660, root_dir_entry, &low_ratio_ops);

    printk("atomic_boost init\n");
    return 0;
}

static void __exit atomic_boost_exit(void)
{
    
}

void handle_atomic_boost(struct zone* zone)
{
    if (enabled) {
        __handle_atomic_boost(zone);
    }
}

module_init(atomic_boost_init);