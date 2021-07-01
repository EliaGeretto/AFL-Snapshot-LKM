#include <linux/buffer_head.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/kallsyms.h>
#include <linux/version.h>
#include <linux/miscdevice.h>

#include "task_data.h"  // mm associated data
#include "hook.h"       // function hooking
#include "snapshot.h"   // main implementation
#include "debug.h"
// #include "symbols.h"
#include "ftrace_helper.h"

#include "afl_snapshot.h"

#define DEVICE_NAME "afl_snapshot"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kallsyms & andreafioraldi");
MODULE_DESCRIPTION("Fast process snapshots for fuzzing");
MODULE_VERSION("1.0.0");

void (*k_flush_tlb_mm_range)(struct mm_struct *mm, unsigned long start,
			     unsigned long end, unsigned int stride_shift,
			     bool freed_tables);
void (*k_zap_page_range)(struct vm_area_struct *vma, unsigned long start,
			 unsigned long size);
dup_fd_t dup_fd_ptr;
put_files_struct_t put_files_struct_ptr;

long mod_dev_ioctl(struct file *filep, unsigned int cmd, unsigned long arg) {

  switch (cmd) {

    case AFL_SNAPSHOT_EXCLUDE_VMRANGE: {

      DBG_PRINT("Calling afl_snapshot_exclude_vmrange");

      struct afl_snapshot_vmrange_args args;
      if (copy_from_user(&args, (void *)arg,
                         sizeof(struct afl_snapshot_vmrange_args)))
        return -EINVAL;

      exclude_vmrange(args.start, args.end);
      return 0;

    }

    case AFL_SNAPSHOT_INCLUDE_VMRANGE: {

      DBG_PRINT("Calling afl_snapshot_include_vmrange");

      struct afl_snapshot_vmrange_args args;
      if (copy_from_user(&args, (void *)arg,
                         sizeof(struct afl_snapshot_vmrange_args)))
        return -EINVAL;

      include_vmrange(args.start, args.end);
      return 0;

    }

    case AFL_SNAPSHOT_IOCTL_TAKE: {

      DBG_PRINT("Calling afl_snapshot_take");

      return take_snapshot(arg);

    }

    case AFL_SNAPSHOT_IOCTL_DO: {

      DBG_PRINT("Calling afl_snapshot_do");

      return take_snapshot(AFL_SNAPSHOT_MMAP | AFL_SNAPSHOT_FDS |
                           AFL_SNAPSHOT_REGS | AFL_SNAPSHOT_EXIT);

    }

    case AFL_SNAPSHOT_IOCTL_RESTORE: {

      DBG_PRINT("Calling afl_snapshot_restore");

      recover_snapshot();
      return 0;

    }

    case AFL_SNAPSHOT_IOCTL_CLEAN: {

      DBG_PRINT("Calling afl_snapshot_clean");

      clean_snapshot();
      return 0;

    }

  }

  return -EINVAL;

}

static const struct file_operations dev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = mod_dev_ioctl,
};

static struct miscdevice misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &dev_fops,
	.mode = 0644,
};

#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
typedef int (*syscall_handler_t)(struct pt_regs *);

// The original syscall handler that we removed to override exit_group()
syscall_handler_t sys_exit_group_orig;

// TODO: non-x86 architectures syscall_table entries don't take pt_regs,
// they take normal args
// https://grok.osiris.cyber.nyu.edu/xref/linux/include/linux/syscalls.h?r=83fa805b#235
// but x86 is (of course) different, taking a pt_regs, then passing extracted
// values to the actual __do_sys*
// https://grok.osiris.cyber.nyu.edu/xref/linux/arch/x86/include/asm/syscall_wrapper.h?r=6e484764#161

asmlinkage int sys_exit_group_hook(struct pt_regs *regs)
{
	if (exit_snapshot())
		return sys_exit_group_orig(regs);

	return 0;
}
#else
typedef long (*syscall_handler_t)(int error_code);

// The original syscall handler that we removed to override exit_group()
syscall_handler_t sys_exit_group_orig;

asmlinkage long sys_exit_group_hook(int error_code)
{
	if (exit_snapshot())
		return sys_exit_group_orig(error_code);

	return 0;
}
#endif

do_exit_t do_exit_orig;

static struct ftrace_hook ftrace_hooks[] = {
	SYSCALL_HOOK("sys_exit_group", sys_exit_group_hook,
		     &sys_exit_group_orig),
	HOOK("do_exit", do_exit_hook, &do_exit_orig),
};

static int resolve_non_exported_symbols(void)
{
	k_flush_tlb_mm_range =
		(void *)kallsyms_lookup_name("flush_tlb_mm_range");
	k_zap_page_range = (void *)kallsyms_lookup_name("zap_page_range");
	dup_fd_ptr = (dup_fd_t)kallsyms_lookup_name("dup_fd");
	put_files_struct_ptr =
		(put_files_struct_t)kallsyms_lookup_name("put_files_struct");

	if (!k_flush_tlb_mm_range || !k_zap_page_range || !dup_fd_ptr ||
	    !put_files_struct_ptr) {
		return -ENOENT;
	}

	SAYF("Resolved all non-exported symbols");

	return 0;
}

// void finish_fault_hook(unsigned long ip, unsigned long parent_ip,
//		       struct ftrace_ops *op, ftrace_regs_ptr regs);

static int __init mod_init(void)
{
	int res;

	SAYF("Loading AFL++ snapshot LKM");

	res = misc_register(&misc_dev);
	if (res) {
		FATAL("Failed to register misc device");
		return res;
	}

	res = fh_install_hooks(ftrace_hooks, ARRAY_SIZE(ftrace_hooks));
	if (res) {
		FATAL("Unable to hook syscalls");
		goto err_registration;
	}

	if (!try_hook("do_wp_page", &wp_page_hook)) {
		FATAL("Unable to hook do_wp_page");
		res = -ENOENT;
		goto err_hooks;
	}

	if (!try_hook("page_add_new_anon_rmap", &do_anonymous_hook)) {
		FATAL("Unable to hook page_add_new_anon_rmap");
		res = -ENOENT;
		goto err_hooks;
	}

	// if (!try_hook("finish_fault", &finish_fault_hook)) {
	//   FATAL("Unable to hook handle_pte_fault");
	//   res = -ENOENT;
	//   goto err_hooks;
	// }

	res = resolve_non_exported_symbols();
	if (res)
		goto err_hooks;

	return 0;

err_hooks:
	unhook_all();

err_registration:
	misc_deregister(&misc_dev);

	return res;
}

static void __exit mod_exit(void)
{
	SAYF("Unloading AFL++ snapshot LKM\n");
	unhook_all();
	fh_remove_hooks(ftrace_hooks, ARRAY_SIZE(ftrace_hooks));
	misc_deregister(&misc_dev);
}

module_init(mod_init);
module_exit(mod_exit);
