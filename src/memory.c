#include "hook.h"
#include "debug.h"
#include "linux/gfp.h"
#include "linux/list.h"
#include "linux/mm.h"
#include "linux/types.h"
#include "task_data.h"
#include "snapshot.h"
#include "vdso/limits.h"

static DEFINE_PER_CPU(struct task_struct *, last_task) = NULL;
static DEFINE_PER_CPU(struct task_data *, last_task_data) = NULL;

static struct task_data *get_task_data_with_cache(struct task_struct *task)
{
	struct task_struct **cached_task = &get_cpu_var(last_task);
	struct task_data **cached_data = &get_cpu_var(last_task_data);

	struct task_data *data = NULL;

	if (*cached_task == task) {
		data = *cached_data;
	} else {
		data = get_task_data(task);

		*cached_task = task;
		*cached_data = data;
	}

	put_cpu_var(last_task);
	put_cpu_var(last_task_data);

	return data;
}

static void invalidate_task_data_cache(const struct task_struct *task)
{
	struct task_struct **cached_task;
	int i;

	for_each_possible_cpu (i) {
		cached_task = &per_cpu(last_task, i);
		if (*cached_task == task) {
			*cached_task = NULL;
			per_cpu(last_task_data, i) = NULL;
		}
	}
}

static pte_t *walk_page_table(unsigned long addr)
{
  pgd_t *pgd;
  p4d_t *p4d;
  pud_t *pud;
  pmd_t *pmd;
  pte_t *ptep = NULL;

  struct mm_struct *mm = current->mm;

  pgd = pgd_offset(mm, addr);
  if (pgd_none(*pgd) || pgd_bad(*pgd)) {

    // DBG_PRINT("Invalid pgd.");
    goto out;

  }

  p4d = p4d_offset(pgd, addr);
  if (p4d_none(*p4d) || p4d_bad(*p4d)) {

    // DBG_PRINT("Invalid p4d.");
    goto out;

  }

  pud = pud_offset(p4d, addr);
  if (pud_none(*pud) || pud_bad(*pud)) {

    // DBG_PRINT("Invalid pud.");
    goto out;

  }

  pmd = pmd_offset(pud, addr);
  if (pmd_none(*pmd) || pmd_bad(*pmd)) {

    // DBG_PRINT("Invalid pmd.");
    goto out;

  }

  ptep = pte_offset_map(pmd, addr);
  if (!ptep) {

    // DBG_PRINT("[NEW] Invalid pte.");
    goto out;

  }

out:
  return ptep;

}

// TODO lock thee lists

void exclude_vmrange(unsigned long start, unsigned long end) {

  struct task_data *data = ensure_task_data(current);

  struct vmrange_node *n = kmalloc(sizeof(struct vmrange_node), GFP_KERNEL);
  if (!n) {
	  FATAL("vmrange_node allocation failed");
	  return;
  }

  n->start = start;
  n->end = end;
  n->next = data->blocklist;
  data->blocklist = n;

}

void include_vmrange(unsigned long start, unsigned long end) {

  struct task_data *data = ensure_task_data(current);

  struct vmrange_node *n = kmalloc(sizeof(struct vmrange_node), GFP_KERNEL);
  if (!n) {
	  FATAL("vmrange_node allocation failed");
	  return;
  }

  n->start = start;
  n->end = end;
  n->next = data->allowlist;
  data->allowlist = n;

}

static int intersect_blocklist(unsigned long start, unsigned long end)
{
  struct task_data *data = ensure_task_data(current);

  struct vmrange_node *n = data->blocklist;
  while (n) {

    if (end > n->start && start < n->end) return 1;
    n = n->next;

  }

  return 0;

}

static int intersect_allowlist(unsigned long start, unsigned long end)
{
  struct task_data *data = ensure_task_data(current);

  struct vmrange_node *n = data->allowlist;
  while (n) {

    if (end > n->start && start < n->end) return 1;
    n = n->next;

  }

  return 0;

}

static int add_snapshot_vma(struct task_data *data, unsigned long start,
			    unsigned long end)
{
	struct snapshot_vma *ss_vma;

	DBG_PRINT("adding snapshot_vma, start: 0x%08lx end: 0x%08lx\n", start,
		  end);

	ss_vma = kmalloc(sizeof(struct snapshot_vma), GFP_KERNEL);
	if (!ss_vma) {
		FATAL("snapshot_vma allocation failed!");
		return -ENOMEM;
	}

	ss_vma->vm_start = start;
	ss_vma->vm_end = end;
	INIT_LIST_HEAD(&ss_vma->ss_vma_list);

	list_add_tail(&ss_vma->ss_vma_list, &data->ss.ss_vma_list);

	return 0;
}

#ifdef DEBUG
void dump_memory_snapshot(struct task_data *data)
{
	struct snapshot_page *sp;
	int i;

	if (!data)
		return;

	DBG_PRINT("dumping dirty pages from task_data %p:", data);
	hash_for_each (data->ss.ss_pages, i, sp, next) {
		if (sp->dirty)
			DBG_PRINT("  %d: 0x%016lx\n", i, sp->page_base);
	}
}
#endif

static struct snapshot_page *get_snapshot_page(struct task_data *data,
					       unsigned long page_base)
{
  struct snapshot_page *sp;

  hash_for_each_possible(data->ss.ss_pages, sp, next, page_base) {

    if (sp->page_base == page_base) return sp;

  }

  return NULL;

}

static struct snapshot_page *add_snapshot_page(struct task_data *data,
					       unsigned long page_base)
{
  struct snapshot_page *sp;

  sp = get_snapshot_page(data, page_base);
  if (sp == NULL) {

    sp = kmalloc(sizeof(struct snapshot_page), GFP_KERNEL);
    if (!sp) {
	    FATAL("could not allocate snapshot_page");
	    return NULL;
    }

    sp->page_base = page_base;
    sp->page_data = NULL;
    hash_add(data->ss.ss_pages, &sp->next, sp->page_base);
    INIT_LIST_HEAD(&sp->dirty_list);

  }

  sp->page_prot = 0;
  sp->has_been_copied = false;
  sp->dirty = false;
  sp->in_dirty_list = false;

  return sp;

}

static void make_snapshot_page(struct task_data *data, struct mm_struct *mm,
			       unsigned long addr)
{
  pte_t *               pte;
  struct snapshot_page *sp;
  struct page *         page;

  pte = walk_page_table(addr);
  if (!pte) goto out;

  page = pte_page(*pte);

  DBG_PRINT(
      "making snapshot: 0x%08lx PTE: 0x%08lx Page: 0x%08lx "
      "PageAnon: %d\n",
      addr, pte->pte, (unsigned long)page, page ? PageAnon(page) : 0);

  sp = add_snapshot_page(data, addr);

  if (pte_none(*pte)) {

    /* empty pte */
    sp->has_had_pte = false;
    set_snapshot_page_none_pte(sp);

  } else {

    sp->has_had_pte = true;
    if (pte_write(*pte)) {

      /* Private rw page */
      DBG_PRINT("private writable addr: 0x%08lx\n", addr);
      ptep_set_wrprotect(mm, addr, pte);
      set_snapshot_page_private(sp);

      /* flush tlb to make the pte change effective */
      k_flush_tlb_mm_range(mm, addr & PAGE_MASK, (addr & PAGE_MASK) + PAGE_SIZE,
                           PAGE_SHIFT, false);
      DBG_PRINT("writable now: %d\n", pte_write(*pte));

    } else {

      /* COW ro page */
      DBG_PRINT("cow writable addr: 0x%08lx\n", addr);
      set_snapshot_page_cow(sp);

    }

  }

  pte_unmap(pte);

out:
  return;

}

// TODO: This seems broken?
// If I have a page that is right below the page of the stack, then it will count as a stack page.
inline bool is_stack(struct vm_area_struct *vma) {

  return vma->vm_start <= vma->vm_mm->start_stack &&
         vma->vm_end >= vma->vm_mm->start_stack;

}

int take_memory_snapshot(struct task_data *data)
{
	struct vm_area_struct *pvma = NULL;
	unsigned long addr = 0;
	int res = 0;

#ifdef DEBUG
	struct vmrange_node *n;

	for (n = data->allowlist; n; n = n->next)
		DBG_PRINT("Allowlist: 0x%08lx - 0x%08lx\n", n->start, n->end);

	for (n = data->blocklist; n; n = n->next)
		DBG_PRINT("Blocklist: 0x%08lx - 0x%08lx\n", n->start, n->end);
#endif

	invalidate_task_data_cache(data->tsk);

	for (pvma = current->mm->mmap; pvma; pvma = pvma->vm_next) {
		// Temporarily store all the vmas
		if (data->config & AFL_SNAPSHOT_MMAP) {
			res = add_snapshot_vma(data, pvma->vm_start,
					       pvma->vm_end);
			if (res)
				return res;
		}

		// We only care about writable pages. Shared memory pages are
		// skipped. If NOSTACK is specified, skip if this is the stack.
		// Otherwise look into the allowlist.
		if (!(((pvma->vm_flags & VM_WRITE) &&
		       !(pvma->vm_flags & VM_SHARED) &&
		       !((data->config & AFL_SNAPSHOT_NOSTACK) &&
			 is_stack(pvma))) ||
		      intersect_allowlist(pvma->vm_start, pvma->vm_end)))
			continue;

		DBG_PRINT("Make snapshot start: 0x%08lx end: 0x%08lx\n",
			  pvma->vm_start, pvma->vm_end);

		for (addr = pvma->vm_start; addr < pvma->vm_end;
		     addr += PAGE_SIZE) {
			if (intersect_blocklist(addr, addr + PAGE_SIZE))
				continue;

			if (((data->config & AFL_SNAPSHOT_BLOCK) ||
			     ((data->config & AFL_SNAPSHOT_NOSTACK) &&
			      is_stack(pvma))) &&
			    !intersect_allowlist(addr, addr + PAGE_SIZE))
				continue;

			make_snapshot_page(data, pvma->vm_mm, addr);
		}
	}

	return 0;
}

static int munmap_new_vmas(struct task_data *data)
{
	struct vm_area_struct *vma_iter = data->tsk->mm->mmap;
	struct vm_area_struct *next_vma_iter = NULL;
	struct snapshot_vma *ss_vma_iter = list_first_entry(
		&data->ss.ss_vma_list, struct snapshot_vma, ss_vma_list);

	unsigned long cursor = 0;
	unsigned long next_cursor = 0;

	unsigned long next_vma_pos = 0;
	unsigned long next_ss_vma_pos = 0;

	bool in_ss_vmas = false;
	bool in_vmas = false;

	int res = 0;

	DBG_PRINT("unmapping new vmas:\n");

	while (vma_iter ||
	       !list_entry_is_head(ss_vma_iter, &data->ss.ss_vma_list,
				   ss_vma_list)) {
		// Calculate next valid positions for vma lists.
		if (vma_iter) {
			// `vm_munmap` may free the `vm_area_struct`, so save `vm_next` here.
			next_vma_iter = vma_iter->vm_next;
			next_vma_pos =
				in_vmas ? vma_iter->vm_end : vma_iter->vm_start;
		} else {
			next_vma_iter = NULL;
			next_vma_pos = ULONG_MAX;
		}

		if (!list_entry_is_head(ss_vma_iter, &data->ss.ss_vma_list,
					ss_vma_list)) {
			next_ss_vma_pos = in_ss_vmas ? ss_vma_iter->vm_end :
							     ss_vma_iter->vm_start;
		} else {
			next_ss_vma_pos = ULONG_MAX;
		}

		next_cursor = min(next_vma_pos, next_ss_vma_pos);

		// `in_vmas` and `in_ss_vmas` hold for the interval [cursor,
		// next_cursor).
		if (next_cursor != cursor && in_vmas && !in_ss_vmas) {
			DBG_PRINT("  unmapping (0x%08lx, 0x%08lx)\n", cursor,
				  next_cursor);
			res = vm_munmap(cursor, next_cursor - cursor);
			if (res) {
				FATAL("munmap failed, start: 0x%08lx, end: 0x%08lx\n",
				      cursor, next_cursor);
				return res;
			}
		} else if (next_cursor != cursor && !in_vmas && in_ss_vmas) {
			FATAL("missing memory, start: 0x%08lx, end: 0x%08lx\n",
			      cursor, next_cursor);
		}

		if (next_cursor == next_vma_pos) {
			in_vmas = !in_vmas;
			if (!in_vmas)
				vma_iter = next_vma_iter;
		}

		if (next_cursor == next_ss_vma_pos) {
			in_ss_vmas = !in_ss_vmas;
			if (!in_ss_vmas)
				ss_vma_iter = list_next_entry(ss_vma_iter,
							      ss_vma_list);
		}

		cursor = next_cursor;
	}

	return 0;
}

static void do_recover_page(struct snapshot_page *sp)
{
  DBG_PRINT(
      "found reserved page: 0x%08lx page_base: 0x%08lx page_prot: "
      "0x%08lx\n",
      (unsigned long)sp->page_data, (unsigned long)sp->page_base,
      sp->page_prot);
  if (copy_to_user((void __user *)sp->page_base, sp->page_data, PAGE_SIZE) != 0)
    DBG_PRINT("incomplete copy_to_user\n");
  sp->dirty = false;

}

static void do_recover_none_pte(struct snapshot_page *sp)
{
  struct mm_struct *mm = current->mm;

  DBG_PRINT("found none_pte refreshed page_base: 0x%08lx page_prot: 0x%08lx\n",
            sp->page_base, sp->page_prot);

  k_zap_page_range(mm->mmap, sp->page_base, PAGE_SIZE);

}

int recover_memory_snapshot(struct task_data *data)
{
	struct snapshot_page *sp;
	int i;

	struct mm_struct *mm = data->tsk->mm;
	pte_t *pte;

	int res = 0;

	if (data->config & AFL_SNAPSHOT_MMAP) {
		res = munmap_new_vmas(data);
		if (res)
			return res;
	}

	hash_for_each (data->ss.ss_pages, i, sp, next) {
		if (sp->dirty && sp->has_been_copied) {
			// it has been captured by page fault

			do_recover_page(sp); // copy old content
			sp->has_had_pte = true;

			pte = walk_page_table(sp->page_base);
			if (!pte)
				continue;

			/* Private rw page */
			DBG_PRINT("private writable addr: 0x%08lx\n",
				  sp->page_base);
			ptep_set_wrprotect(mm, sp->page_base, pte);
			set_snapshot_page_private(sp);

			/* flush tlb to make the pte change effective */
			k_flush_tlb_mm_range(mm, sp->page_base,
					     sp->page_base + PAGE_SIZE,
					     PAGE_SHIFT, false);
			DBG_PRINT("writable now: %d\n", pte_write(*pte));

			pte_unmap(pte);

		} else if (is_snapshot_page_private(sp)) {
			// private page that has not been captured
			// still write protected

		} else if (is_snapshot_page_none_pte(sp) && sp->has_had_pte) {
			do_recover_none_pte(sp);

			set_snapshot_page_none_pte(sp);
			sp->has_had_pte = false;
		}

		if (sp->in_dirty_list) {
			DBG_PRINT("page was in dirty list: 0x%016lx\n",
				  sp->page_base);
			sp->in_dirty_list = false;
			list_del(&sp->dirty_list);
		}
	}

	if (!list_empty(&data->ss.dirty_pages)) {
		WARNF("dirty list is not empty");
	}

	return 0;
}

static void clean_snapshot_vmas(struct task_data *data)
{
	struct snapshot_vma *ss_vma, *next;

	DBG_PRINT("freeing snapshot vmas:\n");

	list_for_each_entry_safe (ss_vma, next, &data->ss.ss_vma_list,
				  ss_vma_list) {
		DBG_PRINT("  start: 0x%08lx end: 0x%08lx\n", ss_vma->vm_start,
			  ss_vma->vm_end);
		list_del(&ss_vma->ss_vma_list);
		kfree(ss_vma);
	}
}

void clean_memory_snapshot(struct task_data *data)
{
	struct snapshot_page *sp;
	struct hlist_node *tmp;
	int i;

	invalidate_task_data_cache(data->tsk);

	if (data->config & AFL_SNAPSHOT_MMAP)
		clean_snapshot_vmas(data);

	hash_for_each_safe (data->ss.ss_pages, i, tmp, sp, next) {
		if (sp->page_data)
			kfree(sp->page_data);
		hash_del(&sp->next);
		kfree(sp);
	}
}

static vm_fault_t do_wp_page_stub(struct vm_fault *vmf)
{
	return 0;
}

void do_wp_page_hook(unsigned long ip, unsigned long parent_ip,
		     struct ftrace_ops *op, ftrace_regs_ptr regs)
{
	struct pt_regs *pregs = ftrace_get_regs(regs);
	struct vm_fault *vmf = NULL;

	struct task_data *data = NULL;
	struct snapshot_page *ss_page = NULL;

	struct mm_struct *mm = NULL;
	struct page *old_page = NULL;
	pte_t entry;
	void *vfrom = NULL;
	unsigned long page_base_addr;

	vmf = (struct vm_fault *)regs_get_kernel_argument(pregs, 0);
	mm = vmf->vma->vm_mm;

	data = get_task_data_with_cache(mm->owner);
	if (!data || !have_snapshot(data))
		return;

	page_base_addr = vmf->address & PAGE_MASK;

	DBG_PRINT("searching snapshot_page for 0x%016lx in task_data: %p\n",
		  page_base_addr, data);
	ss_page = get_snapshot_page(data, vmf->address & PAGE_MASK);
	if (!ss_page)
		return;

	if (ss_page->dirty)
		return;
	ss_page->dirty = true;

	DBG_PRINT("hooking page fault for 0x%016lx\n", page_base_addr);

	if (ss_page->in_dirty_list) {
		WARNF("0x%016lx: Adding page to dirty list, but it's already there??? (dirty: %d, copied: %d)\n",
		      ss_page->page_base, ss_page->dirty,
		      ss_page->has_been_copied);
	} else {
		ss_page->in_dirty_list = true;
		list_add_tail(&ss_page->dirty_list, &data->ss.dirty_pages);
	}

	/* copy the page if necessary.
	 * the page becomes COW page again. we do not need to take care of it.
	 */
	if (!ss_page->has_been_copied) {
		DBG_PRINT("copying page 0x%016lx\n", page_base_addr);

		/* reserved old page data */
		if (!ss_page->page_data) {
			ss_page->page_data = kmalloc(PAGE_SIZE, GFP_ATOMIC);
			if (!ss_page->page_data) {
				FATAL("could not allocate memory for page_data");
				return;
			}
		}

		old_page = pfn_to_page(pte_pfn(vmf->orig_pte));
		vfrom = kmap_atomic(old_page);
		memcpy(ss_page->page_data, vfrom, PAGE_SIZE);
		kunmap_atomic(vfrom);

		ss_page->has_been_copied = true;
	}

	/* if this was originally a COW page, let the original page fault handler
	 * handle it.
	 */
	if (!is_snapshot_page_private(ss_page))
		return;

	DBG_PRINT(
		"handling page fault! process: %s addr: 0x%08lx ptep: 0x%08lx pte: 0x%08lx\n",
		current->comm, vmf->address, (unsigned long)vmf->pte,
		vmf->orig_pte.pte);

	/* change the page prot back to ro from rw */
	entry = pte_mkwrite(vmf->orig_pte);
	set_pte_at(mm, vmf->address, vmf->pte, entry);

	k_flush_tlb_mm_range(mm, page_base_addr, page_base_addr + PAGE_SIZE,
			     PAGE_SHIFT, false);

	pte_unmap_unlock(vmf->pte, vmf->ptl);

	// skip original function
	pregs->ip = (unsigned long)&do_wp_page_stub;
}

// actually hooking page_add_new_anon_rmap, but we really only care about calls
// from do_anonymous_page
void page_add_new_anon_rmap_hook(unsigned long ip, unsigned long parent_ip,
				 struct ftrace_ops *op, ftrace_regs_ptr regs)
{
	struct pt_regs *pregs = ftrace_get_regs(regs);
	struct vm_area_struct *vma;
	unsigned long address;

	struct mm_struct *mm;
	struct task_data *data = NULL;
	struct snapshot_page *ss_page = NULL;
	unsigned long page_base_addr;

	vma = (struct vm_area_struct *)regs_get_kernel_argument(pregs, 1);
	mm = vma->vm_mm;

	address = regs_get_kernel_argument(pregs, 2);
	page_base_addr = address & PAGE_MASK;

	data = get_task_data_with_cache(mm->owner);
	if (!data || !have_snapshot(data))
		return;

	DBG_PRINT("searching snapshot_page for 0x%016lx in task_data: %p\n",
		  page_base_addr, data);
	ss_page = get_snapshot_page(data, page_base_addr);
	if (!ss_page)
		/* not a snapshot'ed page */
		return;

	DBG_PRINT("do_anonymous_page 0x%08lx\n", address);
	// dump_stack();

	// HAVE PTE NOW
	ss_page->has_had_pte = true;
	if (is_snapshot_page_none_pte(ss_page)) {
		if (ss_page->in_dirty_list) {
			WARNF("0x%016lx: Adding page to dirty list, but it's already there??? (dirty: %d, copied: %d)\n",
			      ss_page->page_base, ss_page->dirty,
			      ss_page->has_been_copied);
		} else {
			ss_page->in_dirty_list = true;
			list_add_tail(&ss_page->dirty_list,
				      &data->ss.dirty_pages);
		}
	}

	return;
}

// void finish_fault_hook(unsigned long ip, unsigned long parent_ip,
//                    struct ftrace_ops *op, ftrace_regs_ptr regs)
// {
//   struct pt_regs* pregs = ftrace_get_regs(regs);
//   struct vm_fault *vmf = (struct vm_fault*)pregs->di;
//   struct vm_area_struct *vma;
//   struct mm_struct *     mm;
//   struct task_data *     data;
//   struct snapshot_page * ss_page;
//   unsigned long          address;

//   vma = vmf->vma;
//   address = vmf->address;

//   struct task_struct* ltask = get_cpu_var(last_task);
//   if (ltask == mm->owner) { // XXX: mm is not initialized!

//     // fast path
//     data = get_cpu_var(last_data);
//     put_cpu_var(last_task);
//     put_cpu_var(last_data);
//   } else {

//     // query the radix tree
//     data = get_task_data(mm->owner);
//     get_cpu_var(last_task) = mm->owner;
//     get_cpu_var(last_data) = data;
//     put_cpu_var(last_task);
//     put_cpu_var(last_task);
//     put_cpu_var(last_data);

//   }

//   if (data && have_snapshot(data)) {

//     ss_page = get_snapshot_page(data, address & PAGE_MASK);

//   } else {

//     return;

//   }

//   if (!ss_page) {

//     /* not a snapshot'ed page */
//     return;

//   }

//   DBG_PRINT("finish_fault 0x%08lx", address);
//   dump_stack();

//   // HAVE PTE NOW
//   ss_page->has_had_pte = true;
//   if (is_snapshot_page_none_pte(ss_page)) {
//     if (ss_page->in_dirty_list) {
//       WARNF("0x%016lx: Adding page to dirty list, but it's already there???", ss_page->page_base);
//     } else {
//       ss_page->in_dirty_list = true;
//       list_add_tail(&ss_page->dirty_list, &data->ss.dirty_pages);
//     }
//   }

//   return;
// }
