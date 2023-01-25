#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include "internal.h"

#define BUFSIZE 512

static bool enabled = true;
static u8 run_interval = 1;
static u64 min_increase_interval_ns = NSEC_PER_MSEC * 1000;
static u64 min_decrease_interval_ns = NSEC_PER_SEC * 10;
static unsigned long boost_step = 25000;
static unsigned long int low_ratio_numerator = 3;
static unsigned long int low_ratio_denominator = 8;


static const char* conf_entry_name = "atomic_boost_conf";
static struct proc_dir_entry* conf_entry;

static ssize_t conf_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
    int in_enabled, in_run_interval, scanned_values;
    u64 in_min_decrease_ns, in_min_increase_ns;
    unsigned long int in_boost_step, in_numerator, in_denominator;
    struct zone *zone;
    char buf[BUFSIZE];
    if (*ppos > 0 || count > BUFSIZE || count == 0)
        return 0;

    if (copy_from_user(buf, ubuf, count))
        return -EFAULT;

    if (count == 1 && buf[0] == 10) 
        return 1;

    scanned_values = sscanf(buf, "%d %d %lld %lld %ld %ld %ld", &in_enabled, &in_run_interval, &in_min_increase_ns, &in_min_decrease_ns, &in_boost_step, &in_numerator, &in_denominator);
    if (scanned_values > 0) {
        enabled = in_enabled;
        if (!enabled) {
            for_each_populated_zone(zone) {
                zone->atomic_boost = 0;
            }
        }
        printk("atomic boost %s\n", enabled ? "ENABLED" : "off");
        if (scanned_values == 1)
            return count;
    }

    if (scanned_values == 7) {
        run_interval = in_run_interval;
        min_increase_interval_ns = in_min_increase_ns;
        min_decrease_interval_ns = in_min_decrease_ns;
        boost_step = in_boost_step;
        low_ratio_numerator = in_numerator;
        low_ratio_denominator = in_denominator;
        
        printk("atomic boost set all params\n");
    	return count;
    }
    return -EINVAL;
}

static ssize_t conf_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
    int len;
    char buf[BUFSIZE];
     if (*ppos > 0)
         return 0;
    if (count < BUFSIZE)
        return -EFAULT;
    
    len = sprintf(buf, "%d %d %lld %lld %ld %ld %ld", enabled, run_interval, min_increase_interval_ns, min_decrease_interval_ns, boost_step, low_ratio_numerator, low_ratio_denominator);

    if (copy_to_user(ubuf, buf, len))
        return -EFAULT;

    *ppos = len;
    return len;
}

static struct proc_ops conf_ops = 
{
	.proc_write = conf_write,
    .proc_read = conf_read
};

static void __handle_atomic_boost(struct zone* zone)
{
    unsigned long int min_watermark, free_pages;

    if (zone->atomic_boost)
        return;

    min_watermark = min_wmark_pages(zone);
    free_pages = zone_page_state(zone, NR_FREE_PAGES);
    if (free_pages*low_ratio_denominator < min_watermark*low_ratio_numerator) {
        unsigned long int max_boost = div_u64(zone->present_pages, 6);
        zone->atomic_boost = min_t(unsigned long int, max_boost, boost_step);
        printk("atomic boosting zone %s to %ld\n", zone->name, zone->atomic_boost);
    }
}

static int __init my_module_init(void)
{
    conf_entry = proc_create(conf_entry_name, 0660, NULL, &conf_ops);

    return 0;
}

static void __exit my_module_exit(void)
{
    
}

void handle_atomic_boost(struct zone* zone)
{
    if (enabled) {
        __handle_atomic_boost(zone);
    }
}

module_init(my_module_init);