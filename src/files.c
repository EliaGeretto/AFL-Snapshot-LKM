#include "hook.h"
#include "debug.h"
#include "linux/fdtable.h"
#include "linux/fs.h"
#include "linux/gfp.h"
#include "linux/kallsyms.h"
#include "linux/mm.h"
#include "linux/printk.h"
#include "linux/sched.h"
#include "linux/sched/signal.h"
#include "linux/types.h"
#include "task_data.h"
#include "snapshot.h"

static int save_file_offset(const void *p, struct file *file, unsigned int fd)
{
	loff_t *offsets = (loff_t *)p;

	offsets[fd] = vfs_llseek(file, 0, SEEK_CUR);
	DBG_PRINT("recording fd: %u, offset: %lld\n", fd, offsets[fd]);

	return 0;
}

static int restore_file_offset(const void *p, struct file *file,
			       unsigned int fd)
{
	loff_t *offsets = (loff_t *)p;
	loff_t res = -1;

	/* Restore offset only when valid */
	if (offsets[fd] < 0)
		return 0;

	res = vfs_llseek(file, offsets[fd], SEEK_SET);

	DBG_PRINT("restoring fd: %u, offset: %lld, res: %lld\n", fd, offsets[fd],
		 res);

	if (res < 0) {
		FATAL("error while seeking back fd %u: %lld", fd, res);
		return res;
	} else if (res != offsets[fd]) {
		FATAL("could not seek fd %u back to old position (%lld), "
		       "current position (%lld)",
		       fd, offsets[fd], res);
		return 1;
	}

	return 0;
}

int take_files_snapshot(struct task_data *data)
{
	struct open_files_snapshot *files_snap = &data->ss.ss_files;

	struct files_struct *current_files = current->files;
	struct files_struct *files_copy = NULL;

	unsigned int max_fds = 0;
	loff_t *offsets = NULL;

	int error = 0;

	if (!(data->config & AFL_SNAPSHOT_FDS)) {
		files_snap->files = NULL;
		files_snap->offsets = NULL;
		return 0;
	}

	DBG_PRINT("duplicating files structure for current thread\n");
	files_copy = dup_fd(current_files, NR_OPEN_MAX, &error);
	if (!files_copy)
		goto out;

	/*
	 * Locking is not necessary because files_snapshot was just created,
	 * this is the only reference.
	 */
	max_fds = rcu_dereference_raw(files_copy->fdt)->max_fds;

	DBG_PRINT("allocating memory for %u offsets\n", max_fds);
	offsets = kmalloc_array(max_fds, sizeof(loff_t), GFP_KERNEL);
	if (!offsets) {
		error = -ENOMEM;
		goto out_release;
	}

	DBG_PRINT("saving offsets for all fds\n");
	memset(offsets, -1, max_fds * sizeof(loff_t));
	error = iterate_fd(files_copy, 0, save_file_offset, offsets);
	if (error)
		goto out_release;

	files_snap->files = files_copy;
	files_snap->offsets = offsets;

	return 0;

out_release:
	put_files_struct(files_copy);
	kfree(offsets);

out:
	return error;
}

int recover_files_snapshot(struct task_data *data)
{
	struct open_files_snapshot *files_snap = &data->ss.ss_files;
	struct task_struct *current_task = current;
	struct task_struct *task_iter = NULL;

	struct files_struct *restored_files = NULL;
	struct files_struct *current_old_files = NULL;
	struct files_struct *old_files = NULL;
	int error = 0;

	if (!(data->config & AFL_SNAPSHOT_FDS))
		return 0;

	if (!files_snap->files || !files_snap->offsets) {
		error = -EINVAL;
		goto out;
	}

	DBG_PRINT("duplicating snapshotted files structure\n");
	restored_files = dup_fd(files_snap->files, NR_OPEN_MAX, &error);
	if (!restored_files)
		goto out;

	DBG_PRINT("seeking all files back to the original position\n");
	error = iterate_fd(restored_files, 0, restore_file_offset,
			   files_snap->offsets);
	if (error)
		goto out_release;

	current_old_files = current_task->files;

	for_each_thread (current_task, task_iter) {
		if (task_iter->files != current_old_files)
			continue;

		DBG_PRINT("replacing files structure for thread in current "
			 "process\n");

		if (task_iter != current_task)
			atomic_inc(&restored_files->count);

		old_files = task_iter->files;
		task_lock(task_iter);
		task_iter->files = restored_files;
		task_unlock(task_iter);
		put_files_struct(old_files);
	}

	return 0;

out_release:
	put_files_struct(restored_files);

out:
	return error;
}

void clean_files_snapshot(struct task_data *data)
{
	struct open_files_snapshot *files_snap = &data->ss.ss_files;

	if (files_snap->files) {
		DBG_PRINT("dropping files structure snapshot\n");
		put_files_struct(files_snap->files);
		files_snap->files = NULL;
	}

	if (files_snap->offsets) {
		DBG_PRINT("freeing offsets array\n");
		kfree(files_snap->offsets);
		files_snap->offsets = NULL;
	}
}
