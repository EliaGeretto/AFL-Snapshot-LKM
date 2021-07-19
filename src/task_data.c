#include "task_data.h"
#include "debug.h"

#include <linux/slab.h>

static LIST_HEAD(task_data_list);
static DEFINE_SPINLOCK(task_data_lock);

static void task_data_free_callback(struct rcu_head *rcu)
{
	struct task_data *data = container_of(rcu, struct task_data, rcu);
	struct vmrange *range, *next;

	DBG_PRINT("dropping task_data: %p\n", data);

	list_for_each_entry_safe(range, next, &data->blocklist, node) {
		list_del(&range->node);
		kfree(range);
	}

	list_for_each_entry_safe(range, next, &data->allowlist, node) {
		list_del(&range->node);
		kfree(range);
	}

	kfree(data);
}

struct task_data *get_task_data(const struct task_struct *tsk)
{
	struct task_data *data = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu (data, &task_data_list, list) {
		if (data->tsk == tsk) {
			rcu_read_unlock();
			return data;
		}
	}
	rcu_read_unlock();

	return NULL;
}

struct task_data *ensure_task_data(const struct task_struct *tsk)
{
	struct task_data *data = NULL;

	data = get_task_data(tsk);
	if (data)
		return data;

	data = kzalloc(sizeof(struct task_data), GFP_KERNEL);
	if (!data) {
		FATAL("allocation of new task_data struct failed!\n");
		return NULL;
	}

	data->tsk = tsk;

	INIT_LIST_HEAD(&data->ss.all_vmas);
	INIT_LIST_HEAD(&data->ss.snapshotted_vmas);

	hash_init(data->ss.ss_pages);
	INIT_LIST_HEAD(&data->ss.dirty_pages);

	INIT_LIST_HEAD(&data->allowlist);
	INIT_LIST_HEAD(&data->blocklist);

	spin_lock(&task_data_lock);
	list_add_rcu(&data->list, &task_data_list);
	spin_unlock(&task_data_lock);

	return data;
}

void remove_task_data(struct task_data *data)
{
	spin_lock(&task_data_lock);
	list_del_rcu(&data->list);
	spin_unlock(&task_data_lock);

	call_rcu(&data->rcu, task_data_free_callback);
}
