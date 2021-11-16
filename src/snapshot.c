#include "hook.h"
#include "debug.h"
#include "task_data.h"
#include "snapshot.h"

void do_exit_hook(long code)
{
	struct task_data *data = get_task_data(current);

	if (!data) {
		do_exit_orig(code);
		BUG();
	}

	DBG_PRINT("task_data entry found for process in %s\n", __func__);
	DBG_PRINT("this process was probably killed by a signal");

	if (had_snapshot(data)) {
		DBG_PRINT("cleaning snapshot from %s", __func__);
		clean_snapshot();
	}

	do_exit_orig(code);
	BUG();
}

static void initialize_snapshot(struct task_data *data, int config) {

  struct pt_regs *regs = task_pt_regs(current);

  data->config = config;

  set_had_snapshot(data);

  set_snapshot(data);

  // copy current regs context
  data->ss.regs = *regs;

  // copy current brk
  data->ss.oldbrk = current->mm->brk;

}

int take_snapshot(int config) {

  struct task_data *data = ensure_task_data(current);

  if (!have_snapshot(data)) {  // first execution

    initialize_snapshot(data, config);
    take_memory_snapshot(data);
    if (take_files_snapshot(data)) {
      pr_err("error while snapshotting files");
    }

#ifdef DEBUG
    dump_memory_snapshot(data);
#endif

    return 1;

  }

  return 0;

}

static void recover_state(struct task_data *data) {

  if (data->config & AFL_SNAPSHOT_REGS) {

    struct pt_regs *regs = task_pt_regs(current);

    // restore regs context
    *regs = data->ss.regs;

  }

  // restore brk
  if (current->mm->brk > data->ss.oldbrk) current->mm->brk = data->ss.oldbrk;

}

static void restore_snapshot(struct task_data *data) {

#ifdef DEBUG
  dump_memory_snapshot(data);
#endif

  recover_threads_snapshot(data);
  recover_memory_snapshot(data);
  if (recover_files_snapshot(data)) {
    pr_err("error while snapshotting files");
  }
  recover_state(data);

}

int recover_snapshot(void)
{
	struct task_data *data = get_task_data(current);
	if (!data) {
		pr_err("no snapshot found to restore");
		return 1;
	}

	restore_snapshot(data);

	return 0;
}

int exit_snapshot(void)
{
	struct task_data *data = get_task_data(current);

	if (!data)
		return 1;

	DBG_PRINT("task_data entry found for process in %s\n", __func__);

	if ((data->config & AFL_SNAPSHOT_EXIT) && have_snapshot(data)) {
		DBG_PRINT("restoring snapshot on exit\n");
		restore_snapshot(data);
		return 0;
	}

	if (had_snapshot(data)) {
		DBG_PRINT("cleaning snapshot from %s", __func__);
		clean_snapshot();
	}

	DBG_PRINT("exiting previously snapshotted process\n");

	return 1;
}

void clean_snapshot(void)
{
	struct task_data *data = get_task_data(current);

	if (!data)
		return;

#ifdef DEBUG
  dump_memory_snapshot(data);
#endif

	DBG_PRINT("cleaning snapshot\n");

	clean_memory_snapshot(data);
	clean_files_snapshot(data);
	clear_snapshot(data);

	remove_task_data(data);
}
