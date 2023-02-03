#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include "internal.h"

#define str_buf_size 512

static unsigned long min_boost = 1 << 10;

static u8 run_counter = 0;

static bool enabled = true;
static u8 run_interval = 1;
static u64 min_increase_interval_ns = NSEC_PER_MSEC * 1000;
static u64 min_decrease_interval_ns = NSEC_PER_SEC * 10;
static unsigned long boost_step = 25000;
static unsigned long int low_ratio_numerator = 3;
static unsigned long int low_ratio_denominator = 8;


static const char* conf_entry_name = "mfkb";
static struct proc_dir_entry* conf_entry;

static u64 last_scale_up_time_ns = 0;
static u64 last_scale_down_time_ns = 0;

static ssize_t conf_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
    int in_enabled, in_run_interval, scanned_values;
    u64 in_min_decrease_ns, in_min_increase_ns;
    unsigned long int in_boost_step, in_numerator, in_denominator;
    struct zone *zone;
    char buf[str_buf_size];
    if (*ppos > 0 || count > str_buf_size)
        return -EFAULT;

    if (copy_from_user(buf, ubuf, count))
        return -EFAULT;

    scanned_values = sscanf(buf, "%d %d %lld %lld %ld %ld %ld", &in_enabled, &in_run_interval, &in_min_increase_ns, &in_min_decrease_ns, &in_boost_step, &in_numerator, &in_denominator);
    if (scanned_values != 7) 
        return -EFAULT;

    enabled = in_enabled;
    run_interval = in_run_interval;
    min_increase_interval_ns = in_min_increase_ns;
    min_decrease_interval_ns = in_min_decrease_ns;
    boost_step = in_boost_step;
    low_ratio_numerator = in_numerator;
    low_ratio_denominator = in_denominator;

    if (!enabled) {
        for_each_populated_zone(zone) {
            zone->atomic_boost = 0;
        }
    }

    printk("[ASD] mfkb %s\n", enabled ? "ENABLED" : "off");
    
	return count;
}

static ssize_t conf_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
    int len;
    char buf[str_buf_size];
    printk("[ASD] conf read %p %d %p", ubuf, count, ppos);
     if (*ppos > 0)
         return 0;
    if (count < str_buf_size)
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

static void handle_atomic_boost(struct zone* zone)
{
    bool low, high;
    u64 now;
    unsigned long int min_watermark, max_watermark, free_pages, next_boost, max_boost;
    min_watermark = min_wmark_pages(zone);
    max_watermark = high_wmark_pages(zone);
    free_pages = zone_page_state(zone, NR_FREE_PAGES);
    low = free_pages*low_ratio_denominator < min_watermark*low_ratio_numerator;
    high = free_pages > max_watermark;
    max_boost = div_u64(zone->present_pages, 4);
    if (low) {
        now = ktime_get_ns();
        if (now - last_scale_up_time_ns > min_increase_interval_ns) {
            last_scale_up_time_ns = now;
            next_boost = zone->atomic_boost + boost_step;
            zone->atomic_boost = next_boost > max_boost ? max_boost : next_boost;
            printk("[ASD] MFKBOP increasing %s atomic boost to %ld\n", zone->name, zone->atomic_boost);
        }
        return;
    }

    if (high && zone->atomic_boost) {
        now = ktime_get_ns();
        if (now - last_scale_up_time_ns > min_decrease_interval_ns && now - last_scale_down_time_ns > min_decrease_interval_ns) {
            next_boost = zone->atomic_boost > boost_step ? zone->atomic_boost - boost_step : 0;
            zone->atomic_boost = next_boost < min_boost ? 0 : next_boost;
            printk("[ASD] MFKBOP decreasing %s atomic boost to %ld\n", zone->name, zone->atomic_boost);
        }
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

void wakeup_minfkb_throttle(const struct alloc_context* ac)
{
    if (!(run_counter++ % run_interval) && enabled) {
        //struct zoneref *z;
        struct zone *zone;
        //pg_data_t *last_pgdat = NULL;
        //enum zone_type highest_zoneidx = ac->highest_zoneidx;
        // for_each_zone_zonelist_nodemask(zone, z, ac->zonelist, highest_zoneidx,
		// 			ac->nodemask) {
        //     if (!populated_zone(zone))
        //         continue;
        //     if (last_pgdat != zone->zone_pgdat) {
        //         handle_atomic_boost(zone);
        //         last_pgdat = zone->zone_pgdat;
        //     }
        // }
        for_each_populated_zone(zone) {
            handle_atomic_boost(zone);
        }
    }
}

module_init(my_module_init);