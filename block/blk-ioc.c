/*
 * Functions related to io context handling
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/bootmem.h>	/* for max_pfn/max_low_pfn */
#include <linux/slab.h>
#include <linux/idr.h>

#include "blk.h"

/*
 * For io context allocations
 */
static struct kmem_cache *iocontext_cachep;

static void queue_data_dtor(struct io_context *ioc)
{
	if (!hlist_empty(&ioc->cic_list)) {
		struct dev_io_context *cic;

		cic = hlist_entry(ioc->cic_list.first, struct dev_io_context,
								cic_list);
		cic->dtor(ioc);
	}
}

/*
 * IO Context helper functions. put_io_context() returns 1 if there are no
 * more users of this io context, 0 otherwise.
 */
int put_io_context(struct io_context *ioc)
{
	if (ioc == NULL)
		return 1;

	BUG_ON(atomic_long_read(&ioc->refcount) == 0);

	if (atomic_long_dec_and_test(&ioc->refcount)) {
		rcu_read_lock();
		queue_data_dtor(ioc);
		rcu_read_unlock();

		kmem_cache_free(iocontext_cachep, ioc);
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(put_io_context);

static void queue_data_exit(struct io_context *ioc)
{
	rcu_read_lock();

	if (!hlist_empty(&ioc->cic_list)) {
		struct dev_io_context *cic;

		cic = hlist_entry(ioc->cic_list.first, struct dev_io_context,
								cic_list);
		cic->exit(ioc);
	}
	rcu_read_unlock();
}

/* Called by the exiting task */
void exit_io_context(struct task_struct *task)
{
	struct io_context *ioc;

	task_lock(task);
	ioc = task->io_context;
	task->io_context = NULL;
	task_unlock(task);

	if (atomic_dec_and_test(&ioc->nr_tasks))
		queue_data_exit(ioc);

	put_io_context(ioc);
}

struct io_context *alloc_io_context(gfp_t gfp_flags, int node)
{
	struct io_context *ret;

	ret = kmem_cache_alloc_node(iocontext_cachep, gfp_flags, node);
	if (ret) {
		atomic_long_set(&ret->refcount, 1);
		atomic_set(&ret->nr_tasks, 1);
		spin_lock_init(&ret->lock);
		ret->ioprio_changed = 0;
		ret->ioprio = 0;
		ret->last_waited = 0; /* doesn't matter... */
		ret->nr_batch_requests = 0; /* because this is 0 */
		INIT_RADIX_TREE(&ret->radix_root, GFP_ATOMIC | __GFP_HIGH);
		INIT_HLIST_HEAD(&ret->cic_list);
		ret->ioc_data = NULL;
#if defined(CONFIG_BLK_CGROUP) || defined(CONFIG_BLK_CGROUP_MODULE)
		ret->cgroup_changed = 0;
#endif
	}

	return ret;
}

/*
 * If the current task has no IO context then create one and initialise it.
 * Otherwise, return its existing IO context.
 *
 * This returned IO context doesn't have a specifically elevated refcount,
 * but since the current task itself holds a reference, the context can be
 * used in general code, so long as it stays within `current` context.
 */
struct io_context *current_io_context(gfp_t gfp_flags, int node)
{
	struct task_struct *tsk = current;
	struct io_context *ret;

	ret = tsk->io_context;
	if (likely(ret))
		return ret;

	ret = alloc_io_context(gfp_flags, node);
	if (ret) {
		/* make sure set_task_ioprio() sees the settings above */
		smp_wmb();
		tsk->io_context = ret;
	}

	return ret;
}

/*
 * If the current task has no IO context then create one and initialise it.
 * If it does have a context, take a ref on it.
 *
 * This is always called in the context of the task which submitted the I/O.
 */
struct io_context *get_io_context(gfp_t gfp_flags, int node)
{
	struct io_context *ret = NULL;

	/*
	 * Check for unlikely race with exiting task. ioc ref count is
	 * zero when ioc is being detached.
	 */
	do {
		ret = current_io_context(gfp_flags, node);
		if (unlikely(!ret))
			break;
	} while (!atomic_long_inc_not_zero(&ret->refcount));

	return ret;
}
EXPORT_SYMBOL(get_io_context);

static int __init blk_ioc_init(void)
{
	iocontext_cachep = kmem_cache_create("blkdev_ioc",
			sizeof(struct io_context), 0, SLAB_PANIC, NULL);
	return 0;
}
subsys_initcall(blk_ioc_init);

#if defined(CONFIG_IOSCHED_CFQ) || defined(CONFIG_IOSCHED_FIOPS)
#define CIC_DEAD_INDEX_SHIFT	1

static inline void *queue_data_dead_key(struct queue_data *qdata)
{
	return (void *)(qdata->cic_index << CIC_DEAD_INDEX_SHIFT | CIC_DEAD_KEY);
}

int ioc_builder_init(struct ioc_builder *builder)
{
	if (!builder->alloc_ioc || !builder->free_ioc)
		return -ENOMEM;

	builder->ioc_count = alloc_percpu(unsigned long);
	if (!builder->ioc_count)
		return -ENOMEM;

	builder->ioc_gone = NULL;
	spin_lock_init(&builder->ioc_gone_lock);

	return 0;
}
EXPORT_SYMBOL(ioc_builder_init);

void io_context_builder_exit(struct ioc_builder *builder)
{
	DECLARE_COMPLETION_ONSTACK(all_gone);

	builder->ioc_gone = &all_gone;
	/* ioc_gone's update must be visible before reading ioc_count */
	smp_wmb();

	/*
	 * this also protects us from entering cfq_slab_kill() with
	 * pending RCU callbacks
	 */
	if (elv_ioc_count_read(*builder->ioc_count))
		wait_for_completion(&all_gone);

	free_percpu(builder->ioc_count);
}
EXPORT_SYMBOL(io_context_builder_exit);

static DEFINE_SPINLOCK(cic_index_lock);
static DEFINE_IDA(cic_index_ida);
static int builder_alloc_cic_index(struct ioc_builder *builder)
{
	int index, error;
	unsigned long flags;

	do {
		if (!ida_pre_get(&cic_index_ida, GFP_KERNEL))
			return -ENOMEM;

		spin_lock_irqsave(&cic_index_lock, flags);
		error = ida_get_new(&cic_index_ida, &index);
		spin_unlock_irqrestore(&cic_index_lock, flags);
		if (error && error != -EAGAIN)
			return error;
	} while (error);

	return index;
}

static void builder_free_cic_index(struct ioc_builder *builder, int index)
{
	unsigned long flags;

	spin_lock_irqsave(&cic_index_lock, flags);
	ida_remove(&cic_index_ida, index);
	spin_unlock_irqrestore(&cic_index_lock, flags);
}

int ioc_builder_init_queue(struct ioc_builder *builder,
	struct queue_data *qdata, struct request_queue *q)
{
	/*
	 * Don't need take queue_lock in the routine, since we are
	 * initializing the ioscheduler, and nobody is using qdata
	 */
	qdata->cic_index = builder_alloc_cic_index(builder);
	if (qdata->cic_index < 0)
		return -ENOMEM;

	qdata->queue = q;
	INIT_LIST_HEAD(&qdata->cic_list);

	return 0;
}
EXPORT_SYMBOL(ioc_builder_init_queue);

/*
 * Call func for each cic attached to this ioc.
 */
static void
call_for_each_cic(struct io_context *ioc,
		  void (*func)(struct io_context *, struct dev_io_context *))
{
	struct dev_io_context *cic;
	struct hlist_node *n;

	rcu_read_lock();

	hlist_for_each_entry_rcu(cic, n, &ioc->cic_list, cic_list)
		func(ioc, cic);

	rcu_read_unlock();
}

static void queue_data_cic_free_rcu(struct rcu_head *head)
{
	struct dev_io_context *cic;
	struct ioc_builder *builder;

	cic = container_of(head, struct dev_io_context, rcu_head);
	builder = cic->builder;

	builder->free_ioc(builder, cic);
	elv_ioc_count_dec(*builder->ioc_count);

	if (builder->ioc_gone) {
		/*
		 * CFQ scheduler is exiting, grab exit lock and check
		 * the pending io context count. If it hits zero,
		 * complete ioc_gone and set it back to NULL
		 */
		spin_lock(&builder->ioc_gone_lock);
		if (builder->ioc_gone &&
		    !elv_ioc_count_read(*builder->ioc_count)) {
			complete(builder->ioc_gone);
			builder->ioc_gone = NULL;
		}
		spin_unlock(&builder->ioc_gone_lock);
	}
}

static void queue_data_cic_free(struct dev_io_context *cic)
{
	call_rcu(&cic->rcu_head, queue_data_cic_free_rcu);
}

static void cic_free_func(struct io_context *ioc, struct dev_io_context *cic)
{
	unsigned long flags;
	unsigned long dead_key = (unsigned long) cic->key;

	BUG_ON(!(dead_key & CIC_DEAD_KEY));

	spin_lock_irqsave(&ioc->lock, flags);
	radix_tree_delete(&ioc->radix_root, dead_key >> CIC_DEAD_INDEX_SHIFT);
	hlist_del_rcu(&cic->cic_list);
	spin_unlock_irqrestore(&ioc->lock, flags);

	queue_data_cic_free(cic);
}

/*
 * Must be called with rcu_read_lock() held or preemption otherwise disabled.
 * Only two callers of this - ->dtor() which is called with the rcu_read_lock(),
 * and ->trim() which is called with the task lock held
 */
void queue_data_free_io_context(struct io_context *ioc)
{
	/*
	 * ioc->refcount is zero here, or we are called from elv_unregister(),
	 * so no more cic's are allowed to be linked into this ioc.  So it
	 * should be ok to iterate over the known list, we will see all cic's
	 * since no new ones are added.
	 */
	call_for_each_cic(ioc, cic_free_func);
}
EXPORT_SYMBOL(queue_data_free_io_context);

static void __queue_data_exit_single_io_context(struct queue_data *qdata,
					 struct dev_io_context *cic)
{
	struct io_context *ioc = cic->ioc;
	struct ioc_builder *builder = cic->builder;

	list_del_init(&cic->queue_list);

	/*
	 * Make sure dead mark is seen for dead queues
	 */
	smp_wmb();
	cic->key = queue_data_dead_key(qdata);

	rcu_read_lock();
	if (rcu_dereference(ioc->ioc_data) == cic) {
		rcu_read_unlock();
		spin_lock(&ioc->lock);
		rcu_assign_pointer(ioc->ioc_data, NULL);
		spin_unlock(&ioc->lock);
	} else
		rcu_read_unlock();

	if (builder->cic_exit)
		builder->cic_exit(qdata, cic);
}

/* with request_queue lock hold */
void ioc_builder_exit_queue(struct ioc_builder *builder,
	struct queue_data *qdata)
{
	while (!list_empty(&qdata->cic_list)) {
		struct dev_io_context *cic = list_entry(qdata->cic_list.next,
							struct dev_io_context,
							queue_list);

		__queue_data_exit_single_io_context(qdata, cic);
	}

	builder_free_cic_index(builder, qdata->cic_index);
}
EXPORT_SYMBOL(ioc_builder_exit_queue);

static void queue_data_exit_single_io_context(struct io_context *ioc,
				       struct dev_io_context *cic)
{
	struct queue_data *qdata = cic_to_queue_data(cic);

	if (qdata) {
		struct request_queue *q = qdata->queue;
		unsigned long flags;

		spin_lock_irqsave(q->queue_lock, flags);

		/*
		 * Ensure we get a fresh copy of the ->key to prevent
		 * race between exiting task and queue
		 */
		smp_read_barrier_depends();
		if (cic->key == qdata)
			__queue_data_exit_single_io_context(qdata, cic);

		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}

/*
 * The process that ioc belongs to has exited, we need to clean up
 * and put the internal structures we have that belongs to that process.
 */
static void queue_data_exit_io_context(struct io_context *ioc)
{
	call_for_each_cic(ioc, queue_data_exit_single_io_context);
}

static struct dev_io_context *
queue_data_alloc_io_context(struct ioc_builder *builder,
	struct queue_data *qdata, gfp_t gfp_mask)
{
	struct dev_io_context *cic;

	cic = builder->alloc_ioc(builder, qdata, gfp_mask | __GFP_ZERO);

	if (cic) {
		cic->builder = builder;
		if (builder->cic_init)
			builder->cic_init(qdata, cic);
		INIT_LIST_HEAD(&cic->queue_list);
		INIT_HLIST_NODE(&cic->cic_list);
		cic->dtor = queue_data_free_io_context;
		cic->exit = queue_data_exit_io_context;
		elv_ioc_count_inc(*builder->ioc_count);
	}

	return cic;
}

/*
 * We drop dev io contexts lazily, so we may find a dead one.
 */
static void
queue_data_drop_dead_cic(struct queue_data *queue_data, struct io_context *ioc,
		  struct dev_io_context *cic)
{
	unsigned long flags;

	WARN_ON(!list_empty(&cic->queue_list));
	BUG_ON(cic->key != queue_data_dead_key(queue_data));

	spin_lock_irqsave(&ioc->lock, flags);

	BUG_ON(rcu_dereference_check(ioc->ioc_data,
		lockdep_is_held(&ioc->lock)) == cic);

	radix_tree_delete(&ioc->radix_root, queue_data->cic_index);
	hlist_del_rcu(&cic->cic_list);
	spin_unlock_irqrestore(&ioc->lock, flags);

	queue_data_cic_free(cic);
}

struct dev_io_context *
queue_data_cic_lookup(struct queue_data *qdata, struct io_context *ioc)
{
	struct dev_io_context *cic;
	unsigned long flags;

	if (unlikely(!ioc))
		return NULL;

	rcu_read_lock();

	/*
	 * we maintain a last-hit cache, to avoid browsing over the tree
	 */
	cic = rcu_dereference(ioc->ioc_data);
	if (cic && cic->key == qdata) {
		rcu_read_unlock();
		return cic;
	}

	do {
		cic = radix_tree_lookup(&ioc->radix_root, qdata->cic_index);
		rcu_read_unlock();
		if (!cic)
			break;
		if (unlikely(cic->key != qdata)) {
			queue_data_drop_dead_cic(qdata, ioc, cic);
			rcu_read_lock();
			continue;
		}

		spin_lock_irqsave(&ioc->lock, flags);
		rcu_assign_pointer(ioc->ioc_data, cic);
		spin_unlock_irqrestore(&ioc->lock, flags);
		break;
	} while (1);

	return cic;
}
EXPORT_SYMBOL(queue_data_cic_lookup);

/*
 * Add cic into ioc, using qdata as the search key. This enables us to lookup
 * the process specific dev io context when entered from the block layer.
 * Also adds the cic to a per-qdata list, used when this queue is removed.
 */
static int queue_data_cic_link(struct queue_data *qdata,
	struct io_context *ioc, struct dev_io_context *cic, gfp_t gfp_mask)
{
	unsigned long flags;
	int ret;

	ret = radix_tree_preload(gfp_mask);
	if (!ret) {
		cic->ioc = ioc;
		cic->key = qdata;

		spin_lock_irqsave(&ioc->lock, flags);
		ret = radix_tree_insert(&ioc->radix_root,
					qdata->cic_index, cic);
		if (!ret)
			hlist_add_head_rcu(&cic->cic_list, &ioc->cic_list);
		spin_unlock_irqrestore(&ioc->lock, flags);

		radix_tree_preload_end();

		if (!ret) {
			spin_lock_irqsave(qdata->queue->queue_lock, flags);
			list_add(&cic->queue_list, &qdata->cic_list);
			spin_unlock_irqrestore(qdata->queue->queue_lock, flags);
		}
	}

	if (ret && ret != -EEXIST)
		printk(KERN_ERR "block: cic link failed!\n");

	return ret;
}

static void changed_ioprio(struct io_context *ioc,
	struct dev_io_context *gen_cic)
{
	struct ioc_builder *builder = gen_cic->builder;
	if (builder->changed_ioprio)
		builder->changed_ioprio(ioc, gen_cic);
}

static void queue_data_ioc_set_ioprio(struct io_context *ioc)
{
	call_for_each_cic(ioc, changed_ioprio);
	ioc->ioprio_changed = 0;
}

#ifdef CONFIG_CFQ_GROUP_IOSCHED
static void changed_cgroup(struct io_context *ioc,
	struct dev_io_context *gen_cic)
{
	struct ioc_builder *builder = gen_cic->builder;
	if (builder->changed_cgroup)
		builder->changed_cgroup(ioc, gen_cic);
}

static void queue_data_ioc_set_cgroup(struct io_context *ioc)
{
	call_for_each_cic(ioc, changed_cgroup);
	ioc->cgroup_changed = 0;
}
#endif  /* CONFIG_CFQ_GROUP_IOSCHED */

/*
 * Setup general io context and dev io context. There can be several
 * dev io contexts per general io context, if this process is doing io to more
 * than one device managed by elevator.
 */
struct dev_io_context *queue_data_get_io_context(struct ioc_builder *builder,
	struct queue_data *qdata, gfp_t gfp_mask)
{
	struct io_context *ioc = NULL;
	struct dev_io_context *cic;
	int ret;

	might_sleep_if(gfp_mask & __GFP_WAIT);

	ioc = get_io_context(gfp_mask, qdata->queue->node);
	if (!ioc)
		return NULL;

retry:
	cic = queue_data_cic_lookup(qdata, ioc);
	if (cic)
		goto out;

	cic = queue_data_alloc_io_context(builder, qdata, gfp_mask);
	if (cic == NULL)
		goto err;

	ret = queue_data_cic_link(qdata, ioc, cic, gfp_mask);
	if (ret == -EEXIST) {
		/* someone has linked cic to ioc already */
		queue_data_cic_free(cic);
		goto retry;
	} else if (ret)
		goto err_free;

out:
	smp_read_barrier_depends();
	if (unlikely(ioc->ioprio_changed))
		queue_data_ioc_set_ioprio(ioc);

#ifdef CONFIG_CFQ_GROUP_IOSCHED
	if (unlikely(ioc->cgroup_changed))
		queue_data_ioc_set_cgroup(ioc);
#endif
	return cic;
err_free:
	queue_data_cic_free(cic);
err:
	put_io_context(ioc);
	return NULL;
}
EXPORT_SYMBOL(queue_data_get_io_context);
#endif
