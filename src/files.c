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
#include "task_data.h"
#include "snapshot.h"

static int save_file_offset(const void *p, struct file *file, unsigned int fd)
{
	loff_t *offsets = (loff_t *)p;
	offsets[fd] = vfs_llseek(file, 0, SEEK_CUR);
	pr_debug("recording fd: %u, offset: %lld\n", fd, offsets[fd]);
	return 0;
}

static int restore_file_offset(const void *p, struct file *file,
			       unsigned int fd)
{
	loff_t *offsets = (loff_t *)p;

	/* Restore offset only when valid */
	if (offsets[fd] >= 0) {
		loff_t res = vfs_llseek(file, offsets[fd], SEEK_SET);
		pr_debug("restoring fd: %u, offset: %lld, res: %lld\n", fd,
			 offsets[fd], res);

		if (res < 0) {
			pr_err("error while seeking back fd %u: %lld", fd, res);
			return res;
		} else if (res != offsets[fd]) {
			pr_err("could not seek fd %u back to old position (%lld), "
			       "current position (%lld)",
			       fd, offsets[fd], res);
			return 1;
		}
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

	pr_debug("duplicating files structure for current thread\n");
	files_copy = dup_fd(current_files, NR_OPEN_MAX, &error);
	if (!files_copy)
		goto out;

	/* Locking is not necessary because files_snapshot was just created, this
	 * is the only reference */
	max_fds = rcu_dereference_raw(files_copy->fdt)->max_fds;

	pr_debug("allocating memory for %u offsets\n", max_fds);
	offsets = kvmalloc_array(max_fds, sizeof(loff_t), GFP_KERNEL_ACCOUNT);
	if (!offsets) {
		error = -ENOMEM;
		goto out_release;
	}

	pr_debug("saving offsets for all fds\n");
	memset(offsets, -1, max_fds * sizeof(loff_t));
	if ((error = iterate_fd(files_copy, 0, save_file_offset, offsets)))
		goto out_release;

	files_snap->files = files_copy;
	files_snap->offsets = offsets;

	return 0;

out_release:
	put_files_struct(files_copy);
	kvfree(offsets);

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

	if (!(data->config & AFL_SNAPSHOT_FDS)) {
		return 0;
	}

	if (!files_snap->files || !files_snap->offsets) {
		error = -EINVAL;
		goto out;
	}

	pr_debug("duplicating snapshotted files structure\n");
	restored_files = dup_fd(files_snap->files, NR_OPEN_MAX, &error);
	if (!restored_files) {
		goto out;
	}

	pr_debug("seeking all files back to the original position\n");
	if ((error = iterate_fd(restored_files, 0, restore_file_offset,
				files_snap->offsets))) {
		goto out_release;
	}

	current_old_files = current_task->files;

	for_each_thread (current_task, task_iter) {
		if (task_iter->files != current_old_files) {
			continue;
		}

		pr_debug("replacing files structure for thread "
			 "in current process\n");

		if (task_iter != current_task) {
			atomic_inc(&restored_files->count);
		}

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
		pr_debug("dropping files structure snapshot\n");
		put_files_struct(files_snap->files);
		files_snap->files = NULL;
	}

	if (files_snap->offsets) {
		pr_debug("freeing offsets array\n");
		kvfree(files_snap->offsets);
		files_snap->offsets = NULL;
	}
}