#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>

static struct task_struct *my_thread;
static DECLARE_WAIT_QUEUE_HEAD(wq);
static int condition = 0;

static const size_t str_buf_size = 512;

static u64 min_min_free_kbytes = 5000;
static u64 max_min_free_kbytes = 160000;

static u8 run_counter = 0;
static u8 run_interval = 2;
static u64 min_scale_down_interval_ns = 5000000000;

bool enabled = true;

static const char* root_dir_name = "mfkb";
static struct proc_dir_entry* root_dir_entry;

static struct proc_dir_entry *enabled_entry;
static const char* enabled_entry_name = "enabled";

static ssize_t enabled_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
    int in_value, scanned_values;
    char buf[str_buf_size];
    if (*ppos > 0 || count > str_buf_size)
        return -EFAULT;

    if (copy_from_user(buf, ubuf, count))
        return -EFAULT;

    scanned_values = sscanf(buf, "%d", &in_value);
    if (scanned_values != 1) 
        return -EFAULT;

    enabled = in_value;
    printk("[ASD] mfkb %s\n", enabled ? "ENABLED" : "off");
    
	return count;
}

static ssize_t enabled_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
    int len;
    char buf[str_buf_size];

    if (*ppos > 0)
        return 0;
    if (count < str_buf_size)
        return -EFAULT;
    
    len = sprintf(buf, "%d", enabled);

    if (copy_to_user(ubuf, buf, len))
        return -EFAULT;

    *ppos = len;
    return len;
}

static struct proc_ops enabled_ops = 
{
	.proc_write = enabled_write,
    .proc_read = enabled_read
};

int my_thread_func(void *data)
{
    static u64 last_scale_time_ns = 0;
    while (!kthread_should_stop()) {
        struct zone *zone;
        bool low = false, high = true;
        unsigned long sleep_us = 0;


        if (run_counter++ % run_interval) {
            wait_event_interruptible(wq, condition != 0);
            continue;
        }

        condition = 0;
        //printk("[ASD] Thread woke up\n");

        for_each_populated_zone(zone) {
            unsigned long min_watermark, max_watermark, free_pages;
            bool zone_low, zone_high;
            min_watermark = min_wmark_pages(zone);
            max_watermark = high_wmark_pages(zone);
            free_pages = zone_page_state(zone, NR_FREE_PAGES);
            zone_low = free_pages*3 < min_watermark*2;
            zone_high = free_pages > max_watermark;
            low = low || zone_low;
            high = high && zone_high;
        }
        if (low) {
            u64 new_min_free_kbytes = min_free_kbytes * 2;
            if (new_min_free_kbytes > max_min_free_kbytes) {
                printk("[ASD] Cannot scale up, min free kbytes is at max.\n");
            } else {
                printk("[ASD] MFKBOP recalculating min free kbytes memory is LOW -> SCALING UP %d\n", new_min_free_kbytes);
                min_free_kbytes = new_min_free_kbytes;
                last_scale_time_ns = ktime_get_ns();
                setup_per_zone_wmarks();
                sleep_us = 1000;
            }
        } else if (high) {
            if (min_free_kbytes == min_min_free_kbytes) {
                //printk("[ASD] high memory but already at minimum scaling, ignoring.\n");
                wait_event_interruptible(wq, condition != 0);
            } else {
                u64 elapsed = ktime_get_ns() - last_scale_time_ns;
                bool cooled_down = elapsed > min_scale_down_interval_ns;
                if (cooled_down) {
                    min_free_kbytes /= 2;
                    min_free_kbytes = min_free_kbytes < min_min_free_kbytes ? min_min_free_kbytes : min_free_kbytes;
                    last_scale_time_ns = ktime_get_ns();
                    setup_per_zone_wmarks();
                    printk("[ASD] MFKBOP memory is HIGH elapsed ns %lld -> SCALING DOWN %d\n", elapsed, min_free_kbytes);
                    sleep_us = 1000;
                } else {
                    sleep_us = 100;
                    //printk("[ASD] memory is HIGH elapsed ns %lld -> ignoring\n", elapsed);
                }
            }
        } else {
            sleep_us = 500;
        }

        if (sleep_us) {
            //set_current_state(TASK_INTERRUPTIBLE);
            //schedule_timeout(msecs_to_jiffies(1));
            usleep_range(sleep_us, sleep_us+100);
        } else {
            wait_event_interruptible(wq, condition != 0);
        }
    }

    return 0;
}

static int __init my_module_init(void)
{
    root_dir_entry = proc_mkdir(root_dir_name, NULL);
    enabled_entry = proc_create(enabled_entry_name, 0660, root_dir_entry, &enabled_ops);

    my_thread = kthread_run(my_thread_func, NULL, "my_thread");
    if (IS_ERR(my_thread)) {
        return -1;
    }
    printk("[ASD] kthread started");
    return 0;
}

static void __exit my_module_exit(void)
{
    kthread_stop(my_thread);
}

void wakeup_minfkb_throttle(void)
{
    if (enabled) {
        condition = 1;
        wake_up_interruptible(&wq);
    }
}

module_init(my_module_init);