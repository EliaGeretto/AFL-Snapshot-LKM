#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/version.h>
#include "debug.h"
#include "ftrace_util.h"

struct hook {
	char *symbol_name;
	struct ftrace_ops fops;
	struct list_head l;
};

LIST_HEAD(hooks);

int try_hook(char *func_name, void *handler)
{
	struct hook *hook;
	int ret = 0;

	SAYF("Hooking function %s\n", func_name);
	hook = kzalloc(sizeof(struct hook), GFP_KERNEL);
	if (!hook) {
		ret = -ENOMEM;
		goto err;
	}

	hook->symbol_name = func_name;
	hook->fops.flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_IPMODIFY |
			   FTRACE_OPS_FL_RECURSION;
	hook->fops.func = handler;
	INIT_LIST_HEAD(&hook->l);

	ret = ftrace_set_filter(&hook->fops, func_name, strlen(func_name), 0);
	if (ret)
		goto err_release;

	ret = register_ftrace_function(&hook->fops);
	if (ret)
		goto err_release;

	list_add(&hook->l, &hooks);

	SAYF("Function hooked: %s\n", func_name);

	return 0;

err_release:
	kfree(hook);

err:
	return ret;
}

void unhook(const char *func_name)
{
	struct hook *hook = NULL;
	struct hook *n = NULL;
	int res;

	list_for_each_entry_safe (hook, n, &hooks, l) {
		if (!strcmp(hook->symbol_name, func_name)) {
			res = unregister_ftrace_function(&hook->fops);
			if (res)
				WARNF("unregister_ftrace_function failed");
			list_del(&hook->l);
			kfree(hook);
			break;
		}
	}
}

void unhook_all(void)
{
	struct hook *hook = NULL;
	struct hook *n = NULL;
	int res;

	list_for_each_entry_safe (hook, n, &hooks, l) {
		res = unregister_ftrace_function(&hook->fops);
		if (res)
			WARNF("unregister_ftrace_function failed");
		list_del(&hook->l);
		kfree(hook);
	}
}
