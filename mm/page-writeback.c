/*
 * mm/page-writeback.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 * Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *
 * Contains functions related to writing back dirty pages at the
 * address_space level.
 *
 * 10Apr2002	Andrew Morton
 *		Initial version
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/init.h>
#include <linux/backing-dev.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/blkdev.h>
#include <linux/mpage.h>
#include <linux/rmap.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/syscalls.h>
#include <linux/buffer_head.h>
#include <linux/pagevec.h>
#include <trace/events/writeback.h>

/* The following parameters are exported via /proc/sys/vm */

/*
 * Start background writeback (via writeback threads) at this percentage
 */
int dirty_background_ratio = 10;

/*
 * dirty_background_bytes starts at 0 (disabled) so that it is a function of
 * dirty_background_ratio * the amount of dirtyable memory
 */
unsigned long dirty_background_bytes;

/*
 * free highmem will not be subtracted from the total free memory
 * for calculating free ratios if vm_highmem_is_dirtyable is true
 */
int vm_highmem_is_dirtyable;

/*
 * The generator of dirty data starts writeback at this percentage
 */
int vm_dirty_ratio = 20;

/*
 * vm_dirty_bytes starts at 0 (disabled) so that it is a function of
 * vm_dirty_ratio * the amount of dirtyable memory
 */
unsigned long vm_dirty_bytes;

/*
 * The interval between `kupdate'-style writebacks
 */
unsigned int dirty_writeback_interval = 5 * 100; /* centiseconds */

/*
 * The longest time for which data is allowed to remain dirty
 */
unsigned int dirty_expire_interval = 30 * 100; /* centiseconds */

/*
 * Flag that makes the machine dump writes/reads and block dirtyings.
 */
int block_dump;

/*
 * Flag that puts the machine in "laptop mode". Doubles as a timeout in jiffies:
 * a full sync is triggered after this time elapses without any disk activity.
 */
int laptop_mode;

EXPORT_SYMBOL(laptop_mode);

/* End of sysctl-exported parameters */


/*
 * Scale the writeback cache size proportional to the relative writeout speeds.
 *
 * We do this by keeping a floating proportion between BDIs, based on page
 * writeback completions [end_page_writeback()]. Those devices that write out
 * pages fastest will get the larger share, while the slower will get a smaller
 * share.
 *
 * We use page writeout completions because we are interested in getting rid of
 * dirty pages. Having them written out is the primary goal.
 *
 * We introduce a concept of time, a period over which we measure these events,
 * because demand can/will vary over time. The length of this period itself is
 * measured in page writeback completions.
 *
 */
static struct prop_descriptor vm_completions;
static struct prop_descriptor vm_dirties;

/*
 * couple the period to the dirty_ratio:
 *
 *   period/2 ~ roundup_pow_of_two(dirty limit)
 */
static int calc_period_shift(void)
{
	unsigned long dirty_total;

	if (vm_dirty_bytes)
		dirty_total = vm_dirty_bytes / PAGE_SIZE;
	else
		dirty_total = (vm_dirty_ratio * determine_dirtyable_memory()) /
				100;
	return ilog2(dirty_total - 1) - 1;
}

/*
 * update the period when the dirty threshold changes.
 */
static void update_completion_period(void)
{
	int shift = calc_period_shift();
	prop_change_shift(&vm_completions, shift);
	prop_change_shift(&vm_dirties, shift);
}

int dirty_background_ratio_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret;

	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (ret == 0 && write)
		dirty_background_bytes = 0;
	return ret;
}

int dirty_background_bytes_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret;

	ret = proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
	if (ret == 0 && write)
		dirty_background_ratio = 0;
	return ret;
}

int dirty_ratio_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int old_ratio = vm_dirty_ratio;
	int ret;

	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (ret == 0 && write && vm_dirty_ratio != old_ratio) {
		update_completion_period();
		vm_dirty_bytes = 0;
	}
	return ret;
}


int dirty_bytes_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	unsigned long old_bytes = vm_dirty_bytes;
	int ret;

	ret = proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
	if (ret == 0 && write && vm_dirty_bytes != old_bytes) {
		update_completion_period();
		vm_dirty_ratio = 0;
	}
	return ret;
}

/*
 * Increment the BDI's writeout completion count and the global writeout
 * completion count. Called from test_clear_page_writeback().
 */
static inline void __bdi_writeout_inc(struct backing_dev_info *bdi)
{
	__prop_inc_percpu_max(&vm_completions, &bdi->completions,
			      bdi->max_prop_frac);
}

void bdi_writeout_inc(struct backing_dev_info *bdi)
{
	unsigned long flags;

	local_irq_save(flags);
	__bdi_writeout_inc(bdi);
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(bdi_writeout_inc);

void task_dirty_inc(struct task_struct *tsk)
{
	prop_inc_single(&vm_dirties, &tsk->dirties);
}

/*
 * Obtain an accurate fraction of the BDI's portion.
 */
static void bdi_writeout_fraction(struct backing_dev_info *bdi,
		long *numerator, long *denominator)
{
	if (bdi_cap_writeback_dirty(bdi)) {
		prop_fraction_percpu(&vm_completions, &bdi->completions,
				numerator, denominator);
	} else {
		*numerator = 0;
		*denominator = 1;
	}
}

static inline void task_dirties_fraction(struct task_struct *tsk,
		long *numerator, long *denominator)
{
	prop_fraction_single(&vm_dirties, &tsk->dirties,
				numerator, denominator);
}

/*
 * task_dirty_limit - scale down dirty throttling threshold for one task
 *
 * task specific dirty limit:
 *
 *   dirty -= (dirty/8) * p_{t}
 *
 * To protect light/slow dirtying tasks from heavier/fast ones, we start
 * throttling individual tasks before reaching the bdi dirty limit.
 * Relatively low thresholds will be allocated to heavy dirtiers. So when
 * dirty pages grow large, heavy dirtiers will be throttled first, which will
 * effectively curb the growth of dirty pages. Light dirtiers with high enough
 * dirty threshold may never get throttled.
 */
static unsigned long task_dirty_limit(struct task_struct *tsk,
				       unsigned long bdi_dirty)
{
	long numerator, denominator;
	unsigned long dirty = bdi_dirty;
	u64 inv = dirty / TASK_SOFT_DIRTY_LIMIT;

	task_dirties_fraction(tsk, &numerator, &denominator);
	inv *= numerator;
	do_div(inv, denominator);

	dirty -= inv;

	return max(dirty, bdi_dirty/2);
}

/*
 *
 */
static unsigned int bdi_min_ratio;

int bdi_set_min_ratio(struct backing_dev_info *bdi, unsigned int min_ratio)
{
	int ret = 0;

	spin_lock_bh(&bdi_lock);
	if (min_ratio > bdi->max_ratio) {
		ret = -EINVAL;
	} else {
		min_ratio -= bdi->min_ratio;
		if (bdi_min_ratio + min_ratio < 100) {
			bdi_min_ratio += min_ratio;
			bdi->min_ratio += min_ratio;
		} else {
			ret = -EINVAL;
		}
	}
	spin_unlock_bh(&bdi_lock);

	return ret;
}

int bdi_set_max_ratio(struct backing_dev_info *bdi, unsigned max_ratio)
{
	int ret = 0;

	if (max_ratio > 100)
		return -EINVAL;

	spin_lock_bh(&bdi_lock);
	if (bdi->min_ratio > max_ratio) {
		ret = -EINVAL;
	} else {
		bdi->max_ratio = max_ratio;
		bdi->max_prop_frac = (PROP_FRAC_BASE * max_ratio) / 100;
	}
	spin_unlock_bh(&bdi_lock);

	return ret;
}
EXPORT_SYMBOL(bdi_set_max_ratio);

/*
 * Work out the current dirty-memory clamping and background writeout
 * thresholds.
 *
 * The main aim here is to lower them aggressively if there is a lot of mapped
 * memory around.  To avoid stressing page reclaim with lots of unreclaimable
 * pages.  It is better to clamp down on writers than to start swapping, and
 * performing lots of scanning.
 *
 * We only allow 1/2 of the currently-unmapped memory to be dirtied.
 *
 * We don't permit the clamping level to fall below 5% - that is getting rather
 * excessive.
 *
 * We make sure that the background writeout level is below the adjusted
 * clamping level.
 */

static unsigned long highmem_dirtyable_memory(unsigned long total)
{
#ifdef CONFIG_HIGHMEM
	int node;
	unsigned long x = 0;

	for_each_node_state(node, N_HIGH_MEMORY) {
		struct zone *z =
			&NODE_DATA(node)->node_zones[ZONE_HIGHMEM];

		x += zone_page_state(z, NR_FREE_PAGES) +
		     zone_reclaimable_pages(z);
	}
	/*
	 * Make sure that the number of highmem pages is never larger
	 * than the number of the total dirtyable memory. This can only
	 * occur in very strange VM situations but we want to make sure
	 * that this does not occur.
	 */
	return min(x, total);
#else
	return 0;
#endif
}

/**
 * determine_dirtyable_memory - amount of memory that may be used
 *
 * Returns the numebr of pages that can currently be freed and used
 * by the kernel for direct mappings.
 */
unsigned long determine_dirtyable_memory(void)
{
	unsigned long x;

	x = global_page_state(NR_FREE_PAGES) + global_reclaimable_pages();

	if (!vm_highmem_is_dirtyable)
		x -= highmem_dirtyable_memory(x);

	return x + 1;	/* Ensure that we never return 0 */
}

/*
 * global_dirty_limits - background-writeback and dirty-throttling thresholds
 *
 * Calculate the dirty thresholds based on sysctl parameters
 * - vm.dirty_background_ratio  or  vm.dirty_background_bytes
 * - vm.dirty_ratio             or  vm.dirty_bytes
 * The dirty limits will be lifted by 1/4 for PF_LESS_THROTTLE (ie. nfsd) and
 * runtime tasks.
 */
void global_dirty_limits(unsigned long *pbackground, unsigned long *pdirty)
{
	unsigned long background;
	unsigned long dirty;
	unsigned long available_memory = determine_dirtyable_memory();
	struct task_struct *tsk;

	if (vm_dirty_bytes)
		dirty = DIV_ROUND_UP(vm_dirty_bytes, PAGE_SIZE);
	else {
		int dirty_ratio;

		dirty_ratio = vm_dirty_ratio;
		if (dirty_ratio < 5)
			dirty_ratio = 5;
		dirty = (dirty_ratio * available_memory) / 100;
	}

	if (dirty_background_bytes)
		background = DIV_ROUND_UP(dirty_background_bytes, PAGE_SIZE);
	else
		background = (dirty_background_ratio * available_memory) / 100;

	/*
	 * Ensure at least 1/4 gap between background and dirty thresholds, so
	 * that when dirty throttling starts at (background + dirty)/2, it's at
	 * the entrance of bdi soft throttle threshold, so as to avoid being
	 * hard throttled.
	 */
	if (background > dirty - dirty * 2 / BDI_SOFT_DIRTY_LIMIT)
		background = dirty - dirty * 2 / BDI_SOFT_DIRTY_LIMIT;

	tsk = current;
	if (tsk->flags & PF_LESS_THROTTLE || rt_task(tsk)) {
		background += background / 4;
		dirty += dirty / 4;
	}
	*pbackground = background;
	*pdirty = dirty;
}

/*
 * bdi_dirty_limit - @bdi's share of dirty throttling threshold
 *
 * Allocate high/low dirty limits to fast/slow devices, in order to prevent
 * - starving fast devices
 * - piling up dirty pages (that will take long time to sync) on slow devices
 *
 * The bdi's share of dirty limit will be adapting to its throughput and
 * bounded by the bdi->min_ratio and/or bdi->max_ratio parameters, if set.
 */
unsigned long bdi_dirty_limit(struct backing_dev_info *bdi, unsigned long dirty)
{
	u64 bdi_dirty;
	long numerator, denominator;

	/*
	 * Calculate this BDI's share of the dirty ratio.
	 */
	bdi_writeout_fraction(bdi, &numerator, &denominator);

	bdi_dirty = (dirty * (100 - bdi_min_ratio)) / 100;
	bdi_dirty *= numerator;
	do_div(bdi_dirty, denominator);

	bdi_dirty += (dirty * bdi->min_ratio) / 100;
	if (bdi_dirty > (dirty * bdi->max_ratio) / 100)
		bdi_dirty = dirty * bdi->max_ratio / 100;

	return bdi_dirty;
}

/*
 * After a task dirtied this many pages, balance_dirty_pages_ratelimited_nr()
 * will look to see if it needs to start dirty throttling.
 *
 * If ratelimit_pages is too low then big NUMA machines will call the expensive
 * global_page_state() too often. So scale it adaptively to the safety margin
 * (the number of pages we may dirty without exceeding the dirty limits).
 */
static unsigned long ratelimit_pages(struct backing_dev_info *bdi)
{
	unsigned long background_thresh;
	unsigned long dirty_thresh;
	unsigned long dirty_pages;

	global_dirty_limits(&background_thresh, &dirty_thresh);
	dirty_pages = global_page_state(NR_FILE_DIRTY) +
		      global_page_state(NR_WRITEBACK) +
		      global_page_state(NR_UNSTABLE_NFS);

	if (dirty_pages <= (dirty_thresh + background_thresh) / 2)
		goto out;

	dirty_thresh = bdi_dirty_limit(bdi, dirty_thresh);
	dirty_pages  = bdi_stat(bdi, BDI_RECLAIMABLE) +
		       bdi_stat(bdi, BDI_WRITEBACK);

	if (dirty_pages < dirty_thresh)
		goto out;

	return 1;
out:
	return 1 + int_sqrt(dirty_thresh - dirty_pages);
}

void bdi_update_write_bandwidth(struct backing_dev_info *bdi,
				unsigned long *bw_time,
				s64 *bw_written)
{
	unsigned long written;
	unsigned long elapsed;
	unsigned long bw;
	unsigned long w;

	if (*bw_written == 0)
		goto snapshot;

	elapsed = jiffies - *bw_time;
	if (elapsed < HZ/100)
		return;

	/*
	 * When there lots of tasks throttled in balance_dirty_pages(), they
	 * will each try to update the bandwidth for the same period, making
	 * the bandwidth drift much faster than the desired rate (as in the
	 * single dirtier case). So do some rate limiting.
	 */
	if (jiffies - bdi->write_bandwidth_update_time < elapsed)
		goto snapshot;

	written = percpu_counter_read(&bdi->bdi_stat[BDI_WRITTEN]) - *bw_written;
	bw = (HZ * PAGE_CACHE_SIZE * written + elapsed/2) / elapsed;
	w = min(elapsed / (HZ/100), 128UL);
	bdi->write_bandwidth = (bdi->write_bandwidth * (1024-w) + bw * w) >> 10;
	bdi->write_bandwidth_update_time = jiffies;
snapshot:
	*bw_written = percpu_counter_read(&bdi->bdi_stat[BDI_WRITTEN]);
	*bw_time = jiffies;
}

/*
 * balance_dirty_pages() must be called by processes which are generating dirty
 * data.  It looks at the number of dirty pages in the machine and will force
 * the caller to perform writeback if the system is over `vm_dirty_ratio'.
 * If we're over `background_thresh' then the writeback threads are woken to
 * perform some writeout.
 */
static void balance_dirty_pages(struct address_space *mapping,
				unsigned long pages_dirtied)
{
	long nr_reclaimable;
	long nr_dirty, bdi_dirty;  /* = file_dirty + writeback + unstable_nfs */
	long bdi_prev_dirty = 0;
	unsigned long background_thresh;
	unsigned long dirty_thresh;
	unsigned long bdi_thresh;
	unsigned long task_thresh;
	unsigned long bw;
	unsigned long pause = 0;
	bool dirty_exceeded = false;
	struct backing_dev_info *bdi = mapping->backing_dev_info;
	unsigned long bw_time;
	s64 bw_written = 0;

	for (;;) {
		/*
		 * Unstable writes are a feature of certain networked
		 * filesystems (i.e. NFS) in which data may have been
		 * written to the server's write cache, but has not yet
		 * been flushed to permanent storage.
		 */
		nr_reclaimable = global_page_state(NR_FILE_DIRTY) +
					global_page_state(NR_UNSTABLE_NFS);
		nr_dirty = nr_reclaimable + global_page_state(NR_WRITEBACK);

		global_dirty_limits(&background_thresh, &dirty_thresh);

		/*
		 * Throttle it only when the background writeback cannot
		 * catch-up. This avoids (excessively) small writeouts
		 * when the bdi limits are ramping up.
		 */
		if (nr_dirty <= (background_thresh + dirty_thresh) / 2)
			break;

		bdi_thresh = bdi_dirty_limit(bdi, dirty_thresh);
		task_thresh = task_dirty_limit(current, bdi_thresh);

		/*
		 * In order to avoid the stacked BDI deadlock we need
		 * to ensure we accurately count the 'dirty' pages when
		 * the threshold is low.
		 *
		 * Otherwise it would be possible to get thresh+n pages
		 * reported dirty, even though there are thresh-m pages
		 * actually dirty; with m+n sitting in the percpu
		 * deltas.
		 */
		if (bdi_thresh < 2*bdi_stat_error(bdi)) {
			bdi_dirty = bdi_stat_sum(bdi, BDI_RECLAIMABLE) +
				    bdi_stat_sum(bdi, BDI_WRITEBACK);
		} else {
			bdi_dirty = bdi_stat(bdi, BDI_RECLAIMABLE) +
				    bdi_stat(bdi, BDI_WRITEBACK);
		}

		/*
		 * bdi_thresh takes time to ramp up from the initial 0,
		 * especially for slow devices.
		 *
		 * It's possible that at the moment dirty throttling starts,
		 * 	bdi_dirty = nr_dirty
		 * 		  = (background_thresh + dirty_thresh) / 2
		 * 		  >> bdi_thresh
		 * Then the task could be blocked for a dozen second to flush
		 * all the exceeded (bdi_dirty - bdi_thresh) pages. So offer a
		 * complementary way to break out of the loop when 250ms worth
		 * of dirty pages have been cleaned during our pause time.
		 */
		if (nr_dirty < dirty_thresh &&
		    bdi_prev_dirty - bdi_dirty >
		    bdi->write_bandwidth >> (PAGE_CACHE_SHIFT + 2))
			break;
		bdi_prev_dirty = bdi_dirty;

		if (bdi_dirty >= task_thresh) {
			pause = HZ/10;
			goto pause;
		}

		/*
		 * When bdi_dirty grows closer to bdi_thresh, it indicates more
		 * concurrent dirtiers. Proportionally lower the max throttle
		 * bandwidth. This will resist bdi_dirty from approaching to
		 * close to task_thresh, and help reduce fluctuations of pause
		 * time when there are lots of dirtiers.
		 */
		bw = bdi->write_bandwidth;
		bw = bw * (bdi_thresh - bdi_dirty);
		bw = bw / (bdi_thresh / BDI_SOFT_DIRTY_LIMIT + 1);

		bw = bw * (task_thresh - bdi_dirty);
		bw = bw / (bdi_thresh / TASK_SOFT_DIRTY_LIMIT + 1);

		pause = HZ * (pages_dirtied << PAGE_CACHE_SHIFT) / (bw + 1);
		pause = clamp_val(pause, 1, HZ/10);

pause:
		trace_balance_dirty_pages(bdi,
					  bdi_dirty,
					  bdi_thresh,
					  task_thresh,
					  pages_dirtied,
					  pause);
		bdi_update_write_bandwidth(bdi, &bw_time, &bw_written);
		__set_current_state(TASK_INTERRUPTIBLE);
		io_schedule_timeout(pause);
		bdi_update_write_bandwidth(bdi, &bw_time, &bw_written);

		/*
		 * The bdi thresh is somehow "soft" limit derived from the
		 * global "hard" limit. The former helps to prevent heavy IO
		 * bdi or process from holding back light ones; The latter is
		 * the last resort safeguard.
		 */
		dirty_exceeded = (bdi_dirty > bdi_thresh) ||
				  (nr_dirty > dirty_thresh);

		if (!dirty_exceeded)
			break;

		if (!bdi->dirty_exceeded)
			bdi->dirty_exceeded = 1;
	}

	if (!dirty_exceeded && bdi->dirty_exceeded)
		bdi->dirty_exceeded = 0;

	if (pause == 0 && nr_dirty < background_thresh)
		current->nr_dirtied_pause = ratelimit_pages(bdi);
	else if (pause == 1)
		current->nr_dirtied_pause += current->nr_dirtied_pause / 32 + 1;
	else if (pause >= HZ/10)
		/*
		 * when repeated, writing 1 page per 100ms on slow devices,
		 * i-(i+2)/4 will be able to reach 1 but never reduce to 0.
		 */
		current->nr_dirtied_pause -= (current->nr_dirtied_pause+2) >> 2;

	if (writeback_in_progress(bdi))
		return;

	/*
	 * In laptop mode, we wait until hitting the higher threshold before
	 * starting background writeout, and then write out all the way down
	 * to the lower threshold.  So slow writers cause minimal disk activity.
	 *
	 * In normal mode, we start background writeout at the lower
	 * background_thresh, to keep the amount of dirty memory low.
	 */
	if ((laptop_mode && dirty_exceeded) ||
	    (!laptop_mode && (nr_reclaimable > background_thresh)))
		bdi_start_background_writeback(bdi);
}

void set_page_dirty_balance(struct page *page, int page_mkwrite)
{
	if (set_page_dirty(page) || page_mkwrite) {
		struct address_space *mapping = page_mapping(page);

		if (mapping)
			balance_dirty_pages_ratelimited(mapping);
	}
}

/**
 * balance_dirty_pages_ratelimited_nr - balance dirty memory state
 * @mapping: address_space which was dirtied
 * @nr_pages_dirtied: number of pages which the caller has just dirtied
 *
 * Processes which are dirtying memory should call in here once for each page
 * which was newly dirtied.  The function will periodically check the system's
 * dirty state and will initiate writeback if needed.
 *
 * On really big machines, global_page_state() is expensive, so try to avoid
 * calling it too often (ratelimiting).  But once we're over the dirty memory
 * limit we disable the ratelimiting, to prevent individual processes from
 * overshooting the limit by (ratelimit_pages) each.
 */
void balance_dirty_pages_ratelimited_nr(struct address_space *mapping,
					unsigned long nr_pages_dirtied)
{
	struct backing_dev_info *bdi = mapping->backing_dev_info;

	current->nr_dirtied += nr_pages_dirtied;

	if (unlikely(!current->nr_dirtied_pause))
		current->nr_dirtied_pause = ratelimit_pages(bdi);

	/*
	 * Check the rate limiting. Also, we do not want to throttle real-time
	 * tasks in balance_dirty_pages(). Period.
	 */
	if (unlikely(current->nr_dirtied >= current->nr_dirtied_pause ||
		     bdi->dirty_exceeded)) {
		balance_dirty_pages(mapping, current->nr_dirtied);
		current->nr_dirtied = 0;
	}
}
EXPORT_SYMBOL(balance_dirty_pages_ratelimited_nr);

void throttle_vm_writeout(gfp_t gfp_mask)
{
	unsigned long background_thresh;
	unsigned long dirty_thresh;

        for ( ; ; ) {
		global_dirty_limits(&background_thresh, &dirty_thresh);

                /*
                 * Boost the allowable dirty threshold a bit for page
                 * allocators so they don't get DoS'ed by heavy writers
                 */
                dirty_thresh += dirty_thresh / 10;      /* wheeee... */

                if (global_page_state(NR_UNSTABLE_NFS) +
			global_page_state(NR_WRITEBACK) <= dirty_thresh)
                        	break;
                congestion_wait(BLK_RW_ASYNC, HZ/10);

		/*
		 * The caller might hold locks which can prevent IO completion
		 * or progress in the filesystem.  So we cannot just sit here
		 * waiting for IO to complete.
		 */
		if ((gfp_mask & (__GFP_FS|__GFP_IO)) != (__GFP_FS|__GFP_IO))
			break;
        }
}

/*
 * sysctl handler for /proc/sys/vm/dirty_writeback_centisecs
 */
int dirty_writeback_centisecs_handler(ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	proc_dointvec(table, write, buffer, length, ppos);
	bdi_arm_supers_timer();
	return 0;
}

#ifdef CONFIG_BLOCK
void laptop_mode_timer_fn(unsigned long data)
{
	struct request_queue *q = (struct request_queue *)data;
	int nr_pages = global_page_state(NR_FILE_DIRTY) +
		global_page_state(NR_UNSTABLE_NFS);

	/*
	 * We want to write everything out, not just down to the dirty
	 * threshold
	 */
	if (bdi_has_dirty_io(&q->backing_dev_info))
		bdi_start_writeback(&q->backing_dev_info, nr_pages);
}

/*
 * We've spun up the disk and we're in laptop mode: schedule writeback
 * of all dirty data a few seconds from now.  If the flush is already scheduled
 * then push it back - the user is still using the disk.
 */
void laptop_io_completion(struct backing_dev_info *info)
{
	mod_timer(&info->laptop_mode_wb_timer, jiffies + laptop_mode);
}

/*
 * We're in laptop mode and we've just synced. The sync's writes will have
 * caused another writeback to be scheduled by laptop_io_completion.
 * Nothing needs to be written back anymore, so we unschedule the writeback.
 */
void laptop_sync_completion(void)
{
	struct backing_dev_info *bdi;

	rcu_read_lock();

	list_for_each_entry_rcu(bdi, &bdi_list, bdi_list)
		del_timer(&bdi->laptop_mode_wb_timer);

	rcu_read_unlock();
}
#endif

/*
 * Called early on to tune the page writeback dirty limits.
 *
 * We used to scale dirty pages according to how total memory
 * related to pages that could be allocated for buffers (by
 * comparing nr_free_buffer_pages() to vm_total_pages.
 *
 * However, that was when we used "dirty_ratio" to scale with
 * all memory, and we don't do that any more. "dirty_ratio"
 * is now applied to total non-HIGHPAGE memory (by subtracting
 * totalhigh_pages from vm_total_pages), and as such we can't
 * get into the old insane situation any more where we had
 * large amounts of dirty pages compared to a small amount of
 * non-HIGHMEM memory.
 *
 * But we might still want to scale the dirty_ratio by how
 * much memory the box has..
 */
void __init page_writeback_init(void)
{
	int shift;

	shift = calc_period_shift();
	prop_descriptor_init(&vm_completions, shift);
	prop_descriptor_init(&vm_dirties, shift);
}

/**
 * write_cache_pages - walk the list of dirty pages of the given address space and write all of them.
 * @mapping: address space structure to write
 * @wbc: subtract the number of written pages from *@wbc->nr_to_write
 * @writepage: function called for each page
 * @data: data passed to writepage function
 *
 * If a page is already under I/O, write_cache_pages() skips it, even
 * if it's dirty.  This is desirable behaviour for memory-cleaning writeback,
 * but it is INCORRECT for data-integrity system calls such as fsync().  fsync()
 * and msync() need to guarantee that all the data which was dirty at the time
 * the call was made get new I/O started against them.  If wbc->sync_mode is
 * WB_SYNC_ALL then we were called for data integrity and we must wait for
 * existing IO to complete.
 */
int write_cache_pages(struct address_space *mapping,
		      struct writeback_control *wbc, writepage_t writepage,
		      void *data)
{
	int ret = 0;
	int done = 0;
	struct pagevec pvec;
	int nr_pages;
	pgoff_t uninitialized_var(writeback_index);
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	pgoff_t done_index;
	int cycled;
	int range_whole = 0;

	pagevec_init(&pvec, 0);
	if (wbc->range_cyclic) {
		writeback_index = mapping->writeback_index; /* prev offset */
		index = writeback_index;
		if (index == 0)
			cycled = 1;
		else
			cycled = 0;
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_CACHE_SHIFT;
		end = wbc->range_end >> PAGE_CACHE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = 1;
		cycled = 1; /* ignore range_cyclic tests */

		/*
		 * If this is a data integrity sync, cap the writeback to the
		 * current end of file. Any extension to the file that occurs
		 * after this is a new write and we don't need to write those
		 * pages out to fulfil our data integrity requirements. If we
		 * try to write them out, we can get stuck in this scan until
		 * the concurrent writer stops adding dirty pages and extending
		 * EOF.
		 */
		if (wbc->sync_mode == WB_SYNC_ALL &&
		    wbc->range_end == LLONG_MAX) {
			end = i_size_read(mapping->host) >> PAGE_CACHE_SHIFT;
		}
	}

retry:
	done_index = index;
	while (!done && (index <= end)) {
		int i;

		nr_pages = pagevec_lookup_tag(&pvec, mapping, &index,
			      PAGECACHE_TAG_DIRTY,
			      min(end - index, (pgoff_t)PAGEVEC_SIZE-1) + 1);
		if (nr_pages == 0)
			break;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			/*
			 * At this point, the page may be truncated or
			 * invalidated (changing page->mapping to NULL), or
			 * even swizzled back from swapper_space to tmpfs file
			 * mapping. However, page->index will not change
			 * because we have a reference on the page.
			 */
			if (page->index > end) {
				/*
				 * can't be range_cyclic (1st pass) because
				 * end == -1 in that case.
				 */
				done = 1;
				break;
			}

			done_index = page->index + 1;

			lock_page(page);

			/*
			 * Page truncated or invalidated. We can freely skip it
			 * then, even for data integrity operations: the page
			 * has disappeared concurrently, so there could be no
			 * real expectation of this data interity operation
			 * even if there is now a new, dirty page at the same
			 * pagecache address.
			 */
			if (unlikely(page->mapping != mapping)) {
continue_unlock:
				unlock_page(page);
				continue;
			}

			if (!PageDirty(page)) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			if (PageWriteback(page)) {
				if (wbc->sync_mode != WB_SYNC_NONE)
					wait_on_page_writeback(page);
				else
					goto continue_unlock;
			}

			BUG_ON(PageWriteback(page));
			if (!clear_page_dirty_for_io(page))
				goto continue_unlock;

			trace_wbc_writepage(wbc, mapping->backing_dev_info);
			ret = (*writepage)(page, wbc, data);
			if (unlikely(ret)) {
				if (ret == AOP_WRITEPAGE_ACTIVATE) {
					unlock_page(page);
					ret = 0;
				} else {
					/*
					 * done_index is set past this page,
					 * so media errors will not choke
					 * background writeout for the entire
					 * file. This has consequences for
					 * range_cyclic semantics (ie. it may
					 * not be suitable for data integrity
					 * writeout).
					 */
					done = 1;
					break;
				}
			}

			/*
			 * We stop writing back only if we are not doing
			 * integrity sync. In case of integrity sync we have to
			 * keep going until we have written all the pages
			 * we tagged for writeback prior to entering this loop.
			 */
			if (--wbc->nr_to_write <= 0 &&
			    wbc->sync_mode == WB_SYNC_NONE) {
				done = 1;
				break;
			}
		}
		pagevec_release(&pvec);
		cond_resched();
	}
	if (!cycled && !done) {
		/*
		 * range_cyclic:
		 * We hit the last page and there is more work to be done: wrap
		 * back to the start of the file
		 */
		cycled = 1;
		index = 0;
		end = writeback_index - 1;
		goto retry;
	}
	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = done_index;

	return ret;
}
EXPORT_SYMBOL(write_cache_pages);

/*
 * Function used by generic_writepages to call the real writepage
 * function and set the mapping flags on error
 */
static int __writepage(struct page *page, struct writeback_control *wbc,
		       void *data)
{
	struct address_space *mapping = data;
	int ret = mapping->a_ops->writepage(page, wbc);
	mapping_set_error(mapping, ret);
	return ret;
}

/**
 * generic_writepages - walk the list of dirty pages of the given address space and writepage() all of them.
 * @mapping: address space structure to write
 * @wbc: subtract the number of written pages from *@wbc->nr_to_write
 *
 * This is a library function, which implements the writepages()
 * address_space_operation.
 */
int generic_writepages(struct address_space *mapping,
		       struct writeback_control *wbc)
{
	/* deal with chardevs and other special file */
	if (!mapping->a_ops->writepage)
		return 0;

	return write_cache_pages(mapping, wbc, __writepage, mapping);
}

EXPORT_SYMBOL(generic_writepages);

int do_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	int ret;

	if (wbc->nr_to_write <= 0)
		return 0;
	if (mapping->a_ops->writepages)
		ret = mapping->a_ops->writepages(mapping, wbc);
	else
		ret = generic_writepages(mapping, wbc);
	return ret;
}

/**
 * write_one_page - write out a single page and optionally wait on I/O
 * @page: the page to write
 * @wait: if true, wait on writeout
 *
 * The page must be locked by the caller and will be unlocked upon return.
 *
 * write_one_page() returns a negative error code if I/O failed.
 */
int write_one_page(struct page *page, int wait)
{
	struct address_space *mapping = page->mapping;
	int ret = 0;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 1,
	};

	BUG_ON(!PageLocked(page));

	if (wait)
		wait_on_page_writeback(page);

	if (clear_page_dirty_for_io(page)) {
		page_cache_get(page);
		ret = mapping->a_ops->writepage(page, &wbc);
		if (ret == 0 && wait) {
			wait_on_page_writeback(page);
			if (PageError(page))
				ret = -EIO;
		}
		page_cache_release(page);
	} else {
		unlock_page(page);
	}
	return ret;
}
EXPORT_SYMBOL(write_one_page);

/*
 * For address_spaces which do not use buffers nor write back.
 */
int __set_page_dirty_no_writeback(struct page *page)
{
	if (!PageDirty(page))
		return !TestSetPageDirty(page);
	return 0;
}

/*
 * Helper function for set_page_dirty family.
 * NOTE: This relies on being atomic wrt interrupts.
 */
void account_page_dirtied(struct page *page, struct address_space *mapping)
{
	if (mapping_cap_account_dirty(mapping)) {
		__inc_zone_page_state(page, NR_FILE_DIRTY);
		__inc_bdi_stat(mapping->backing_dev_info, BDI_RECLAIMABLE);
		task_dirty_inc(current);
		task_io_account_write(PAGE_CACHE_SIZE);
	}
}

/*
 * For address_spaces which do not use buffers.  Just tag the page as dirty in
 * its radix tree.
 *
 * This is also used when a single buffer is being dirtied: we want to set the
 * page dirty in that case, but not all the buffers.  This is a "bottom-up"
 * dirtying, whereas __set_page_dirty_buffers() is a "top-down" dirtying.
 *
 * Most callers have locked the page, which pins the address_space in memory.
 * But zap_pte_range() does not lock the page, however in that case the
 * mapping is pinned by the vma's ->vm_file reference.
 *
 * We take care to handle the case where the page was truncated from the
 * mapping by re-checking page_mapping() inside tree_lock.
 */
int __set_page_dirty_nobuffers(struct page *page)
{
	if (!TestSetPageDirty(page)) {
		struct address_space *mapping = page_mapping(page);
		struct address_space *mapping2;

		if (!mapping)
			return 1;

		spin_lock_irq(&mapping->tree_lock);
		mapping2 = page_mapping(page);
		if (mapping2) { /* Race with truncate? */
			BUG_ON(mapping2 != mapping);
			WARN_ON_ONCE(!PagePrivate(page) && !PageUptodate(page));
			account_page_dirtied(page, mapping);
			radix_tree_tag_set(&mapping->page_tree,
				page_index(page), PAGECACHE_TAG_DIRTY);
		}
		spin_unlock_irq(&mapping->tree_lock);
		if (mapping->host) {
			/* !PageAnon && !swapper_space */
			__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
		}
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(__set_page_dirty_nobuffers);

/*
 * When a writepage implementation decides that it doesn't want to write this
 * page for some reason, it should redirty the locked page via
 * redirty_page_for_writepage() and it should then unlock the page and return 0
 */
int redirty_page_for_writepage(struct writeback_control *wbc, struct page *page)
{
	wbc->pages_skipped++;
	return __set_page_dirty_nobuffers(page);
}
EXPORT_SYMBOL(redirty_page_for_writepage);

/*
 * Dirty a page.
 *
 * For pages with a mapping this should be done under the page lock
 * for the benefit of asynchronous memory errors who prefer a consistent
 * dirty state. This rule can be broken in some special cases,
 * but should be better not to.
 *
 * If the mapping doesn't provide a set_page_dirty a_op, then
 * just fall through and assume that it wants buffer_heads.
 */
int set_page_dirty(struct page *page)
{
	struct address_space *mapping = page_mapping(page);

	if (likely(mapping)) {
		int (*spd)(struct page *) = mapping->a_ops->set_page_dirty;
#ifdef CONFIG_BLOCK
		if (!spd)
			spd = __set_page_dirty_buffers;
#endif
		return (*spd)(page);
	}
	if (!PageDirty(page)) {
		if (!TestSetPageDirty(page))
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(set_page_dirty);

/*
 * set_page_dirty() is racy if the caller has no reference against
 * page->mapping->host, and if the page is unlocked.  This is because another
 * CPU could truncate the page off the mapping and then free the mapping.
 *
 * Usually, the page _is_ locked, or the caller is a user-space process which
 * holds a reference on the inode by having an open file.
 *
 * In other cases, the page should be locked before running set_page_dirty().
 */
int set_page_dirty_lock(struct page *page)
{
	int ret;

	lock_page_nosync(page);
	ret = set_page_dirty(page);
	unlock_page(page);
	return ret;
}
EXPORT_SYMBOL(set_page_dirty_lock);

/*
 * Clear a page's dirty flag, while caring for dirty memory accounting.
 * Returns true if the page was previously dirty.
 *
 * This is for preparing to put the page under writeout.  We leave the page
 * tagged as dirty in the radix tree so that a concurrent write-for-sync
 * can discover it via a PAGECACHE_TAG_DIRTY walk.  The ->writepage
 * implementation will run either set_page_writeback() or set_page_dirty(),
 * at which stage we bring the page's dirty flag and radix-tree dirty tag
 * back into sync.
 *
 * This incoherency between the page's dirty flag and radix-tree tag is
 * unfortunate, but it only exists while the page is locked.
 */
int clear_page_dirty_for_io(struct page *page)
{
	struct address_space *mapping = page_mapping(page);

	BUG_ON(!PageLocked(page));

	ClearPageReclaim(page);
	if (mapping && mapping_cap_account_dirty(mapping)) {
		/*
		 * Yes, Virginia, this is indeed insane.
		 *
		 * We use this sequence to make sure that
		 *  (a) we account for dirty stats properly
		 *  (b) we tell the low-level filesystem to
		 *      mark the whole page dirty if it was
		 *      dirty in a pagetable. Only to then
		 *  (c) clean the page again and return 1 to
		 *      cause the writeback.
		 *
		 * This way we avoid all nasty races with the
		 * dirty bit in multiple places and clearing
		 * them concurrently from different threads.
		 *
		 * Note! Normally the "set_page_dirty(page)"
		 * has no effect on the actual dirty bit - since
		 * that will already usually be set. But we
		 * need the side effects, and it can help us
		 * avoid races.
		 *
		 * We basically use the page "master dirty bit"
		 * as a serialization point for all the different
		 * threads doing their things.
		 */
		if (page_mkclean(page))
			set_page_dirty(page);
		/*
		 * We carefully synchronise fault handlers against
		 * installing a dirty pte and marking the page dirty
		 * at this point. We do this by having them hold the
		 * page lock at some point after installing their
		 * pte, but before marking the page dirty.
		 * Pages are always locked coming in here, so we get
		 * the desired exclusion. See mm/memory.c:do_wp_page()
		 * for more comments.
		 */
		if (TestClearPageDirty(page)) {
			dec_zone_page_state(page, NR_FILE_DIRTY);
			dec_bdi_stat(mapping->backing_dev_info,
					BDI_RECLAIMABLE);
			return 1;
		}
		return 0;
	}
	return TestClearPageDirty(page);
}
EXPORT_SYMBOL(clear_page_dirty_for_io);

int test_clear_page_writeback(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	int ret;

	if (mapping) {
		struct backing_dev_info *bdi = mapping->backing_dev_info;
		unsigned long flags;

		spin_lock_irqsave(&mapping->tree_lock, flags);
		ret = TestClearPageWriteback(page);
		if (ret) {
			radix_tree_tag_clear(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_WRITEBACK);
			if (bdi_cap_account_writeback(bdi)) {
				__dec_bdi_stat(bdi, BDI_WRITEBACK);
				__inc_bdi_stat(bdi, BDI_WRITTEN);
				__bdi_writeout_inc(bdi);
			}
		}
		spin_unlock_irqrestore(&mapping->tree_lock, flags);
	} else {
		ret = TestClearPageWriteback(page);
	}
	if (ret)
		dec_zone_page_state(page, NR_WRITEBACK);
	return ret;
}

int test_set_page_writeback(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	int ret;

	if (mapping) {
		struct backing_dev_info *bdi = mapping->backing_dev_info;
		unsigned long flags;

		spin_lock_irqsave(&mapping->tree_lock, flags);
		ret = TestSetPageWriteback(page);
		if (!ret) {
			radix_tree_tag_set(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_WRITEBACK);
			if (bdi_cap_account_writeback(bdi))
				__inc_bdi_stat(bdi, BDI_WRITEBACK);
		}
		if (!PageDirty(page))
			radix_tree_tag_clear(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_DIRTY);
		spin_unlock_irqrestore(&mapping->tree_lock, flags);
	} else {
		ret = TestSetPageWriteback(page);
	}
	if (!ret)
		inc_zone_page_state(page, NR_WRITEBACK);
	return ret;

}
EXPORT_SYMBOL(test_set_page_writeback);

/*
 * Return true if any of the pages in the mapping are marked with the
 * passed tag.
 */
int mapping_tagged(struct address_space *mapping, int tag)
{
	int ret;
	rcu_read_lock();
	ret = radix_tree_tagged(&mapping->page_tree, tag);
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(mapping_tagged);
