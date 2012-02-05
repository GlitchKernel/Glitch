/*
 * IOPS based IO scheduler. Based on CFQ.
 *  Copyright (C) 2003 Jens Axboe <axboe <at> kernel.dk>
 *  Shaohua Li <shli <at> kernel.org>
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/jiffies.h>
#include <linux/rbtree.h>
#include <linux/ioprio.h>
#include "blk.h"

#define VIOS_SCALE_SHIFT 10
#define VIOS_SCALE (1 << VIOS_SCALE_SHIFT)

#define VIOS_READ_SCALE (1)
#define VIOS_WRITE_SCALE (1)

struct fiops_rb_root {
	struct rb_root rb;
	struct rb_node *left;
	unsigned count;

	u64 min_vios;
};
#define FIOPS_RB_ROOT	(struct fiops_rb_root) { .rb = RB_ROOT}

struct fiops_data {
	struct queue_data qdata;

	struct fiops_rb_root service_tree;

	unsigned int busy_queues;

	struct work_struct unplug_work;

	unsigned int read_scale;
	unsigned int write_scale;
};

struct fiops_ioc {
	struct dev_io_context dev_ioc;

	unsigned int flags;
	struct fiops_data *fiopsd;
	struct rb_node rb_node;
	u64 vios; /* key in service_tree */
	struct fiops_rb_root *service_tree;

	struct rb_root sort_list;
	struct list_head fifo;

	pid_t pid;
};

static struct kmem_cache *fiops_ioc_pool;
static struct ioc_builder ioc_builder;
#define queue_data_to_fiopsd(ptr) container_of(ptr, struct fiops_data, qdata)
#define dev_ioc_to_fiops_ioc(ptr) container_of(ptr, struct fiops_ioc, dev_ioc)
#define ioc_service_tree(ioc) (&((ioc)->fiopsd->service_tree))

#define RQ_CIC(rq)		\
	((struct fiops_ioc *) (rq)->elevator_private[0])
enum ioc_state_flags {
	FIOPS_IOC_FLAG_on_rr = 0,	/* on round-robin busy list */
};

#define FIOPS_IOC_FNS(name)						\
static inline void fiops_mark_ioc_##name(struct fiops_ioc *ioc)	\
{									\
	ioc->flags |= (1 << FIOPS_IOC_FLAG_##name);			\
}									\
static inline void fiops_clear_ioc_##name(struct fiops_ioc *ioc)	\
{									\
	ioc->flags &= ~(1 << FIOPS_IOC_FLAG_##name);			\
}									\
static inline int fiops_ioc_##name(const struct fiops_ioc *ioc)	\
{									\
	return ((ioc)->flags & (1 << FIOPS_IOC_FLAG_##name)) != 0;	\
}

FIOPS_IOC_FNS(on_rr);
#undef FIOPS_IOC_FNS

/*
 * The below is leftmost cache rbtree addon
 */
static struct fiops_ioc *fiops_rb_first(struct fiops_rb_root *root)
{
	/* Service tree is empty */
	if (!root->count)
		return NULL;

	if (!root->left)
		root->left = rb_first(&root->rb);

	if (root->left)
		return rb_entry(root->left, struct fiops_ioc, rb_node);

	return NULL;
}

static void rb_erase_init(struct rb_node *n, struct rb_root *root)
{
	rb_erase(n, root);
	RB_CLEAR_NODE(n);
}

static void fiops_rb_erase(struct rb_node *n, struct fiops_rb_root *root)
{
	if (root->left == n)
		root->left = NULL;
	rb_erase_init(n, &root->rb);
	--root->count;
}

static inline u64 max_vios(u64 min_vios, u64 vios)
{
	s64 delta = (s64)(vios - min_vios);
	if (delta > 0)
		min_vios = vios;

	return min_vios;
}

static void fiops_update_min_vios(struct fiops_rb_root *service_tree)
{
	struct fiops_ioc *ioc;

	ioc = fiops_rb_first(service_tree);
	if (!ioc)
		return;
	service_tree->min_vios = max_vios(service_tree->min_vios, ioc->vios);
}

/*
 * The fiopsd->service_trees holds all pending fiops_ioc's that have
 * requests waiting to be processed. It is sorted in the order that
 * we will service the queues.
 */
static void fiops_service_tree_add(struct fiops_data *fiopsd,
	struct fiops_ioc *ioc)
{
	struct rb_node **p, *parent;
	struct fiops_ioc *__ioc;
	struct fiops_rb_root *service_tree = ioc_service_tree(ioc);
	u64 vios;
	int left;

	/* New added IOC */
	if (RB_EMPTY_NODE(&ioc->rb_node))
		vios = max_vios(service_tree->min_vios, ioc->vios);
	else {
		vios = ioc->vios;
		/* ioc->service_tree might not equal to service_tree */
		fiops_rb_erase(&ioc->rb_node, ioc->service_tree);
		ioc->service_tree = NULL;
	}

	left = 1;
	parent = NULL;
	ioc->service_tree = service_tree;
	p = &service_tree->rb.rb_node;
	while (*p) {
		struct rb_node **n;

		parent = *p;
		__ioc = rb_entry(parent, struct fiops_ioc, rb_node);

		/*
		 * sort by key, that represents service time.
		 */
		if (vios <  __ioc->vios)
			n = &(*p)->rb_left;
		else {
			n = &(*p)->rb_right;
			left = 0;
		}

		p = n;
	}

	if (left)
		service_tree->left = &ioc->rb_node;

	ioc->vios = vios;
	rb_link_node(&ioc->rb_node, parent, p);
	rb_insert_color(&ioc->rb_node, &service_tree->rb);
	service_tree->count++;

	fiops_update_min_vios(service_tree);
}

/*
 * Update ioc's position in the service tree.
 */
static void fiops_resort_rr_list(struct fiops_data *fiopsd,
	struct fiops_ioc *ioc)
{
	/*
	 * Resorting requires the ioc to be on the RR list already.
	 */
	if (fiops_ioc_on_rr(ioc))
		fiops_service_tree_add(fiopsd, ioc);
}

/*
 * add to busy list of queues for service, trying to be fair in ordering
 * the pending list according to last request service
 */
static void fiops_add_ioc_rr(struct fiops_data *fiopsd, struct fiops_ioc *ioc)
{
	BUG_ON(fiops_ioc_on_rr(ioc));
	fiops_mark_ioc_on_rr(ioc);

	fiopsd->busy_queues++;

	fiops_resort_rr_list(fiopsd, ioc);
}

/*
 * Called when the ioc no longer has requests pending, remove it from
 * the service tree.
 */
static void fiops_del_ioc_rr(struct fiops_data *fiopsd, struct fiops_ioc *ioc)
{
	BUG_ON(!fiops_ioc_on_rr(ioc));
	fiops_clear_ioc_on_rr(ioc);

	if (!RB_EMPTY_NODE(&ioc->rb_node)) {
		fiops_rb_erase(&ioc->rb_node, ioc->service_tree);
		ioc->service_tree = NULL;
	}

	BUG_ON(!fiopsd->busy_queues);
	fiopsd->busy_queues--;
}

/*
 * rb tree support functions
 */
static void fiops_del_rq_rb(struct request *rq)
{
	struct fiops_ioc *ioc = RQ_CIC(rq);

	elv_rb_del(&ioc->sort_list, rq);
}

static void fiops_add_rq_rb(struct request *rq)
{
	struct fiops_ioc *ioc = RQ_CIC(rq);
	struct fiops_data *fiopsd = ioc->fiopsd;

	elv_rb_add(&ioc->sort_list, rq);

	if (!fiops_ioc_on_rr(ioc))
		fiops_add_ioc_rr(fiopsd, ioc);
}

static void fiops_reposition_rq_rb(struct fiops_ioc *ioc, struct request *rq)
{
	elv_rb_del(&ioc->sort_list, rq);
	fiops_add_rq_rb(rq);
}

static void fiops_remove_request(struct request *rq)
{
	list_del_init(&rq->queuelist);
	fiops_del_rq_rb(rq);
}

static u64 fiops_scaled_vios(struct fiops_data *fiopsd,
	struct fiops_ioc *ioc, struct request *rq)
{
	if (rq_data_dir(rq) == READ)
		return VIOS_SCALE;
	else
		return VIOS_SCALE * fiopsd->write_scale / fiopsd->read_scale;
}

/* return vios dispatched */
static u64 fiops_dispatch_request(struct fiops_data *fiopsd,
	struct fiops_ioc *ioc)
{
	struct request *rq;
	struct request_queue *q = fiopsd->qdata.queue;

	rq = rq_entry_fifo(ioc->fifo.next);

	fiops_remove_request(rq);
	elv_dispatch_sort(q, rq);

	return fiops_scaled_vios(fiopsd, ioc, rq);
}

static int fiops_forced_dispatch(struct fiops_data *fiopsd)
{
	struct fiops_ioc *ioc;
	int dispatched = 0;

	while ((ioc = fiops_rb_first(&fiopsd->service_tree)) != NULL) {
		while (!list_empty(&ioc->fifo)) {
			fiops_dispatch_request(fiopsd, ioc);
			dispatched++;
		}
		if (fiops_ioc_on_rr(ioc))
			fiops_del_ioc_rr(fiopsd, ioc);
	}
	return dispatched;
}

static struct fiops_ioc *fiops_select_ioc(struct fiops_data *fiopsd)
{
	struct fiops_ioc *ioc;

	if (RB_EMPTY_ROOT(&fiopsd->service_tree.rb))
		return NULL;
	ioc = fiops_rb_first(&fiopsd->service_tree);
	return ioc;
}

static void fiops_charge_vios(struct fiops_data *fiopsd,
	struct fiops_ioc *ioc, u64 vios)
{
	struct fiops_rb_root *service_tree = ioc->service_tree;
	ioc->vios += vios;

	if (RB_EMPTY_ROOT(&ioc->sort_list))
		fiops_del_ioc_rr(fiopsd, ioc);
	else
		fiops_resort_rr_list(fiopsd, ioc);

	fiops_update_min_vios(service_tree);
}

static int fiops_dispatch_requests(struct request_queue *q, int force)
{
	struct fiops_data *fiopsd = q->elevator->elevator_data;
	struct fiops_ioc *ioc;
	u64 vios;

	if (unlikely(force))
		return fiops_forced_dispatch(fiopsd);

	ioc = fiops_select_ioc(fiopsd);
	if (!ioc)
		return 0;

	vios = fiops_dispatch_request(fiopsd, ioc);

	fiops_charge_vios(fiopsd, ioc, vios);
	return 1;
}

static void fiops_insert_request(struct request_queue *q, struct request *rq)
{
	struct fiops_ioc *ioc = RQ_CIC(rq);

	rq_set_fifo_time(rq, jiffies);

	list_add_tail(&rq->queuelist, &ioc->fifo);

	fiops_add_rq_rb(rq);
}

/*
 * scheduler run of queue, if there are requests pending and no one in the
 * driver that will restart queueing
 */
static inline void fiops_schedule_dispatch(struct fiops_data *fiopsd)
{
	if (fiopsd->busy_queues)
		kblockd_schedule_work(fiopsd->qdata.queue,
				      &fiopsd->unplug_work);
}

static int
fiops_set_request(struct request_queue *q, struct request *rq, gfp_t gfp_mask)
{
	struct fiops_data *fiopsd = q->elevator->elevator_data;
	struct dev_io_context *dev_ioc;
	struct fiops_ioc *cic;

	might_sleep_if(gfp_mask & __GFP_WAIT);

	dev_ioc = queue_data_get_io_context(&ioc_builder, &fiopsd->qdata,
		gfp_mask);
	if (!dev_ioc)
		goto queue_fail;

	cic = dev_ioc_to_fiops_ioc(dev_ioc);

	/*
	 * we hold a reference of dev_ioc and nobody else set this request,
	 * doesn't need locking
	 */
	rq->elevator_private[0] = cic;

	return 0;

queue_fail:
	fiops_schedule_dispatch(fiopsd);
	return 1;
}

static void fiops_put_request(struct request *rq)
{
	struct fiops_ioc *ioc = RQ_CIC(rq);

	if (ioc) {
		rq->elevator_private[0] = NULL;
		put_io_context(ioc->dev_ioc.ioc);
	}
}

static struct request *
fiops_find_rq_fmerge(struct fiops_data *fiopsd, struct bio *bio)
{
	struct task_struct *tsk = current;
	struct dev_io_context *gen_cic;
	struct fiops_ioc *cic;

	gen_cic = queue_data_cic_lookup(&fiopsd->qdata, tsk->io_context);
	if (!gen_cic)
		return NULL;
	cic = dev_ioc_to_fiops_ioc(gen_cic);

	if (cic) {
		sector_t sector = bio->bi_sector + bio_sectors(bio);

		return elv_rb_find(&cic->sort_list, sector);
	}

	return NULL;
}

static int fiops_merge(struct request_queue *q, struct request **req,
		     struct bio *bio)
{
	struct fiops_data *fiopsd = q->elevator->elevator_data;
	struct request *__rq;

	__rq = fiops_find_rq_fmerge(fiopsd, bio);
	if (__rq && elv_rq_merge_ok(__rq, bio)) {
		*req = __rq;
		return ELEVATOR_FRONT_MERGE;
	}

	return ELEVATOR_NO_MERGE;
}

static void fiops_merged_request(struct request_queue *q, struct request *req,
			       int type)
{
	if (type == ELEVATOR_FRONT_MERGE) {
		struct fiops_ioc *ioc = RQ_CIC(req);

		fiops_reposition_rq_rb(ioc, req);
	}
}

static void
fiops_merged_requests(struct request_queue *q, struct request *rq,
		    struct request *next)
{
	struct fiops_ioc *ioc = RQ_CIC(rq);
	struct fiops_data *fiopsd = q->elevator->elevator_data;
	/*
	 * reposition in fifo if next is older than rq
	 */
	if (!list_empty(&rq->queuelist) && !list_empty(&next->queuelist) &&
	    time_before(rq_fifo_time(next), rq_fifo_time(rq))) {
		list_move(&rq->queuelist, &next->queuelist);
		rq_set_fifo_time(rq, rq_fifo_time(next));
	}

	fiops_remove_request(next);

	ioc = RQ_CIC(next);
	/*
	 * all requests of this task are merged to other tasks, delete it
	 * from the service tree.
	 */
	if (fiops_ioc_on_rr(ioc) && RB_EMPTY_ROOT(&ioc->sort_list))
		fiops_del_ioc_rr(fiopsd, ioc);
}

static int fiops_allow_merge(struct request_queue *q, struct request *rq,
			   struct bio *bio)
{
	struct fiops_data *fiopsd = q->elevator->elevator_data;
	struct dev_io_context *gen_cic;
	struct fiops_ioc *cic;

	/*
	 * Lookup the ioc that this bio will be queued with. Allow
	 * merge only if rq is queued there.
	 */
	gen_cic = queue_data_cic_lookup(&fiopsd->qdata, current->io_context);
	if (!gen_cic)
		return false;
	cic = dev_ioc_to_fiops_ioc(gen_cic);

	return cic == RQ_CIC(rq);
}

static void fiops_exit_queue(struct elevator_queue *e)
{
	struct fiops_data *fiopsd = e->elevator_data;
	struct request_queue *q = fiopsd->qdata.queue;

	cancel_work_sync(&fiopsd->unplug_work);

	spin_lock_irq(q->queue_lock);

	ioc_builder_exit_queue(&ioc_builder, &fiopsd->qdata);

	spin_unlock_irq(q->queue_lock);
	kfree(fiopsd);
}

static void fiops_kick_queue(struct work_struct *work)
{
	struct fiops_data *fiopsd =
		container_of(work, struct fiops_data, unplug_work);
	struct request_queue *q = fiopsd->qdata.queue;

	spin_lock_irq(q->queue_lock);
	__blk_run_queue(q);
	spin_unlock_irq(q->queue_lock);
}

static void *fiops_init_queue(struct request_queue *q)
{
	struct fiops_data *fiopsd;

	fiopsd = kzalloc_node(sizeof(*fiopsd), GFP_KERNEL, q->node);
	if (!fiopsd)
		return NULL;

	if (ioc_builder_init_queue(&ioc_builder, &fiopsd->qdata, q)) {
		kfree(fiopsd);
		return NULL;
	}

	fiopsd->service_tree = FIOPS_RB_ROOT;

	INIT_WORK(&fiopsd->unplug_work, fiops_kick_queue);

	fiopsd->read_scale = VIOS_READ_SCALE;
	fiopsd->write_scale = VIOS_WRITE_SCALE;

	return fiopsd;
}

static void fiops_slab_kill(void)
{
	/*
	 * Caller already ensured that pending RCU callbacks are completed,
	 * so we should have no busy allocations at this point.
	 */
	if (fiops_ioc_pool)
		kmem_cache_destroy(fiops_ioc_pool);
}

static int __init fiops_slab_setup(void)
{
	fiops_ioc_pool = KMEM_CACHE(fiops_ioc, 0);
	if (!fiops_ioc_pool)
		return -ENOMEM;

	return 0;
}

static struct dev_io_context *
fiops_alloc_ioc(struct ioc_builder *builder, struct queue_data *qdata,
	gfp_t gfp_mask)
{
	struct fiops_ioc *ioc = kmem_cache_alloc_node(fiops_ioc_pool,
		gfp_mask, qdata->queue->node);
	if (ioc)
		return &ioc->dev_ioc;
	return NULL;
}

static void fiops_free_ioc(struct ioc_builder *builder,
	struct dev_io_context *dev_ioc)
{
	struct fiops_ioc *ioc = dev_ioc_to_fiops_ioc(dev_ioc);
	kmem_cache_free(fiops_ioc_pool, ioc);
}

static void fiops_init_cic(struct queue_data *qdata,
	struct dev_io_context *gen_cic)
{
	struct fiops_data *fiopsd = queue_data_to_fiopsd(qdata);
	struct fiops_ioc *ioc = dev_ioc_to_fiops_ioc(gen_cic);

	RB_CLEAR_NODE(&ioc->rb_node);
	INIT_LIST_HEAD(&ioc->fifo);
	ioc->sort_list = RB_ROOT;

	ioc->fiopsd = fiopsd;

	ioc->pid = current->pid;
}

static void fiops_exit_cic(struct queue_data *qdata,
	struct dev_io_context *gen_cic)
{
	struct fiops_ioc *ioc = dev_ioc_to_fiops_ioc(gen_cic);

	WARN_ON(fiops_ioc_on_rr(ioc));
}

static struct ioc_builder ioc_builder = {
	.alloc_ioc = fiops_alloc_ioc,
	.free_ioc = fiops_free_ioc,
	.cic_init = fiops_init_cic,
	.cic_exit = fiops_exit_cic,
};

/*
 * sysfs parts below -->
 */
static ssize_t
fiops_var_show(unsigned int var, char *page)
{
	return sprintf(page, "%d\n", var);
}

static ssize_t
fiops_var_store(unsigned int *var, const char *page, size_t count)
{
	char *p = (char *) page;

	*var = simple_strtoul(p, &p, 10);
	return count;
}

#define SHOW_FUNCTION(__FUNC, __VAR)					\
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct fiops_data *fiopsd = e->elevator_data;			\
	return fiops_var_show(__VAR, (page));				\
}
SHOW_FUNCTION(fiops_read_scale_show, fiopsd->read_scale);
SHOW_FUNCTION(fiops_write_scale_show, fiopsd->write_scale);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX)				\
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count)	\
{									\
	struct fiops_data *fiopsd = e->elevator_data;			\
	unsigned int __data;						\
	int ret = fiops_var_store(&__data, (page), count);		\
	if (__data < (MIN))						\
		__data = (MIN);						\
	else if (__data > (MAX))					\
		__data = (MAX);						\
	*(__PTR) = __data;						\
	return ret;							\
}
STORE_FUNCTION(fiops_read_scale_store, &fiopsd->read_scale, 1, 100);
STORE_FUNCTION(fiops_write_scale_store, &fiopsd->write_scale, 1, 100);
#undef STORE_FUNCTION

#define FIOPS_ATTR(name) \
	__ATTR(name, S_IRUGO|S_IWUSR, fiops_##name##_show, fiops_##name##_store)

static struct elv_fs_entry fiops_attrs[] = {
	FIOPS_ATTR(read_scale),
	FIOPS_ATTR(write_scale),
	__ATTR_NULL
};

static struct elevator_type iosched_fiops = {
	.ops = {
		.elevator_merge_fn =		fiops_merge,
		.elevator_merged_fn =		fiops_merged_request,
		.elevator_merge_req_fn =	fiops_merged_requests,
		.elevator_allow_merge_fn =	fiops_allow_merge,
		.elevator_dispatch_fn =		fiops_dispatch_requests,
		.elevator_add_req_fn =		fiops_insert_request,
		.elevator_former_req_fn =	elv_rb_former_request,
		.elevator_latter_req_fn =	elv_rb_latter_request,
		.elevator_set_req_fn =		fiops_set_request,
		.elevator_put_req_fn =		fiops_put_request,
		.elevator_init_fn =		fiops_init_queue,
		.elevator_exit_fn =		fiops_exit_queue,
		.trim =				queue_data_free_io_context,
	},
	.elevator_attrs =	fiops_attrs,
	.elevator_name =	"fiops",
	.elevator_owner =	THIS_MODULE,
};

static int __init fiops_init(void)
{
	if (fiops_slab_setup())
		return -ENOMEM;
	if (ioc_builder_init(&ioc_builder)) {
		fiops_slab_kill();
		return -ENOMEM;
	}

	elv_register(&iosched_fiops);

	return 0;
}

static void __exit fiops_exit(void)
{
	elv_unregister(&iosched_fiops);
	io_context_builder_exit(&ioc_builder);
	fiops_slab_kill();
}

module_init(fiops_init);
module_exit(fiops_exit);

MODULE_AUTHOR("Jens Axboe, Shaohua Li <shli <at> kernel.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IOPS based IO scheduler");
