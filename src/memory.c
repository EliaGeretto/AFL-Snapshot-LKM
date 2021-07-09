#include "hook.h"
#include "debug.h"
#include "linux/mm.h"
#include "linux/types.h"
#include "task_data.h"
#include "snapshot.h"

static DEFINE_PER_CPU(struct task_struct *, last_task);
static DEFINE_PER_CPU(struct task_data *, last_task_data);

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

static void invalidate_task_data_cache(void)
{
	struct task_struct **cached_task = &get_cpu_var(last_task);
	struct task_data **cached_data = &get_cpu_var(last_task_data);

	*cached_task = NULL;
	*cached_data = NULL;

	put_cpu_var(last_task);
	put_cpu_var(last_task_data);
}

pmd_t *get_page_pmd(unsigned long addr) {

  pgd_t *pgd;
  p4d_t *p4d;
  pud_t *pud;
  pmd_t *pmd = NULL;

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
    pmd = NULL;
    goto out;

  }

out:
  return pmd;

}

pte_t *walk_page_table(unsigned long addr) {

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

int intersect_blocklist(unsigned long start, unsigned long end) {

  struct task_data *data = ensure_task_data(current);

  struct vmrange_node *n = data->blocklist;
  while (n) {

    if (end > n->start && start < n->end) return 1;
    n = n->next;

  }

  return 0;

}

int intersect_allowlist(unsigned long start, unsigned long end) {

  struct task_data *data = ensure_task_data(current);

  struct vmrange_node *n = data->allowlist;
  while (n) {

    if (end > n->start && start < n->end) return 1;
    n = n->next;

  }

  return 0;

}

void add_snapshot_vma(struct task_data *data, unsigned long start,
                      unsigned long end) {

  struct snapshot_vma *ss_vma;
  struct snapshot_vma *p;

  DBG_PRINT("Adding snapshot vmas start: 0x%08lx end: 0x%08lx\n", start, end);

  ss_vma = kmalloc(sizeof(struct snapshot_vma), GFP_ATOMIC);
  if (!ss_vma) {
    FATAL("snapshot_vma allocation failed!");
    return;
  }

  ss_vma->vm_start = start;
  ss_vma->vm_end = end;

  if (data->ss.ss_mmap == NULL) {

    data->ss.ss_mmap = ss_vma;

  } else {

    p = data->ss.ss_mmap;
    while (p->vm_next != NULL)
      p = p->vm_next;

    p->vm_next = ss_vma;

  }

  ss_vma->vm_next = NULL;

}

struct snapshot_page *get_snapshot_page(struct task_data *data,
                                        unsigned long     page_base) {

  struct snapshot_page *sp;

  hash_for_each_possible(data->ss.ss_page, sp, next, page_base) {

    if (sp->page_base == page_base) return sp;

  }

  return NULL;

}

struct snapshot_page *add_snapshot_page(struct task_data *data,
                                        unsigned long     page_base) {

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
    hash_add(data->ss.ss_page, &sp->next, sp->page_base);

  }

  sp->page_prot = 0;
  sp->has_been_copied = false;
  sp->dirty = false;
  sp->in_dirty_list = false;

  return sp;

}

void make_snapshot_page(struct task_data *data, struct mm_struct *mm,
                        unsigned long addr) {

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

void take_memory_snapshot(struct task_data *data)
{
	struct vm_area_struct *pvma = NULL;
	unsigned long addr = 0;

#ifdef DEBUG
	struct vmrange_node *n;

	for (n = data->allowlist; n; n = n->next)
		DBG_PRINT("Allowlist: 0x%08lx - 0x%08lx\n", n->start, n->end);

	for (n = data->blocklist; n; n = n->next)
		DBG_PRINT("Blocklist: 0x%08lx - 0x%08lx\n", n->start, n->end);
#endif

	invalidate_task_data_cache();

	for (pvma = current->mm->mmap; pvma; pvma = pvma->vm_next) {
		// Temporarily store all the vmas
		if (data->config & AFL_SNAPSHOT_MMAP)
			add_snapshot_vma(data, pvma->vm_start, pvma->vm_end);

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
}

void munmap_new_vmas(struct task_data *data) {

  struct vm_area_struct *vma = data->tsk->mm->mmap;
  struct snapshot_vma *  ss_vma = data->ss.ss_mmap;

  unsigned long old_start = ss_vma->vm_start;
  unsigned long old_end = ss_vma->vm_end;
  unsigned long cur_start = vma->vm_start;
  unsigned long cur_end = vma->vm_end;

  /* we believe that normally, the original mappings of the father process
   * will not be munmapped by the child process when fuzzing.
   *
   * load library on-the-fly?
   */
  do {

    if (cur_start < old_start) {

      if (old_start >= cur_end) {

        DBG_PRINT("new: from 0x%08lx to 0x%08lx\n", cur_start, cur_end);
        vm_munmap(cur_start, cur_end - cur_start);
        vma = vma->vm_next;
        if (!vma) break;
        cur_start = vma->vm_start;
        cur_end = vma->vm_end;

      } else {

        DBG_PRINT("new: from 0x%08lx to 0x%08lx\n", cur_start, old_start);
        vm_munmap(cur_start, old_start - cur_start);
        cur_start = old_start;

      }

    } else {

      if (cur_end < old_end) {

        vma = vma->vm_next;
        if (!vma) break;
        cur_start = vma->vm_start;
        cur_end = vma->vm_end;

        old_start = cur_end;

      } else if (cur_end == old_end) {

        vma = vma->vm_next;
        if (!vma) break;
        cur_start = vma->vm_start;
        cur_end = vma->vm_end;

        ss_vma = ss_vma->vm_next;
        if (!ss_vma) break;
        old_start = ss_vma->vm_start;
        old_end = ss_vma->vm_end;

      } else if (cur_end > old_end) {

        cur_start = old_end;

        ss_vma = ss_vma->vm_next;
        if (!ss_vma) break;
        old_start = ss_vma->vm_start;
        old_end = ss_vma->vm_end;

      }

    }

  } while (true);

  if (vma) {

    DBG_PRINT("new: from 0x%08lx to 0x%08lx\n", cur_start, cur_end);
    vm_munmap(cur_start, cur_end - cur_start);
    while (vma->vm_next != NULL) {

      vma = vma->vm_next;
      DBG_PRINT("new: from 0x%08lx to 0x%08lx\n", vma->vm_start, vma->vm_end);
      vm_munmap(vma->vm_start, vma->vm_end - vma->vm_start);

    }

  }

}

void do_recover_page(struct snapshot_page *sp) {

  DBG_PRINT(
      "found reserved page: 0x%08lx page_base: 0x%08lx page_prot: "
      "0x%08lx\n",
      (unsigned long)sp->page_data, (unsigned long)sp->page_base,
      sp->page_prot);
  if (copy_to_user((void __user *)sp->page_base, sp->page_data, PAGE_SIZE) != 0)
    DBG_PRINT("incomplete copy_to_user\n");
  sp->dirty = false;

}

void do_recover_none_pte(struct snapshot_page *sp) {

  struct mm_struct *mm = current->mm;

  DBG_PRINT("found none_pte refreshed page_base: 0x%08lx page_prot: 0x%08lx\n",
            sp->page_base, sp->page_prot);

  k_zap_page_range(mm->mmap, sp->page_base, PAGE_SIZE);

}

void recover_memory_snapshot(struct task_data *data) {

  struct snapshot_page *sp, *prev_sp = NULL;
  struct mm_struct *    mm = data->tsk->mm;
  pte_t *               pte, entry;
  int                   i;

  int count = 0;

  if (data->config & AFL_SNAPSHOT_MMAP) munmap_new_vmas(data);
  // Instead of iterating over all pages in the snapshot and then restoring the dirty ones,
  // we can save a lot of computing time by keeping a list of only dirty pages.
  // Since we know exactly when pages match the conditions below, we can just insert them into the dirty list then.
  // This had a massive boost on performance for me, >50%. (Might be more or less depending on a few factors).
  //
  // original loop below
  hash_for_each(data->ss.ss_page, i, sp, next) {
  struct list_head* ptr;
  // for (ptr = data->ss.dirty_pages.next; ptr != &data->ss.dirty_pages; ptr = ptr->next){
    count++;
    // sp = list_entry(ptr, struct snapshot_page, dirty_list);
    if (sp->dirty &&
        sp->has_been_copied) {  // it has been captured by page fault

      do_recover_page(sp);  // copy old content
      sp->has_had_pte = true;

      pte = walk_page_table(sp->page_base);
      if (pte) {

        /* Private rw page */
        DBG_PRINT("private writable addr: 0x%08lx\n", sp->page_base);
        ptep_set_wrprotect(mm, sp->page_base, pte);
        set_snapshot_page_private(sp);

        /* flush tlb to make the pte change effective */
        k_flush_tlb_mm_range(mm, sp->page_base, sp->page_base + PAGE_SIZE,
                             PAGE_SHIFT, false);
        DBG_PRINT("writable now: %d\n", pte_write(*pte));

        pte_unmap(pte);

      }

    } else if (is_snapshot_page_private(sp)) {

      // private page that has not been captured
      // still write protected

    } else if (is_snapshot_page_none_pte(sp) && sp->has_had_pte) {

      do_recover_none_pte(sp);

      set_snapshot_page_none_pte(sp);
      sp->has_had_pte = false;

    }
    if (!sp->in_dirty_list) {
      // WARNF("0x%016lx: sp->in_dirty_list = false, but we just encountered it in dirty list!?", sp->page_base);
    }
    sp->in_dirty_list = false;
    // if (ptr->next == ptr || ptr->prev == ptr) {
    //   WARNF("0x%016lx: DETECTED CYCLE IN DIRTY LIST: ptr: %px, ptr->next: %px", sp->page_base, &ptr, ptr->next);
    //   break;
    // }
  }

  DBG_PRINT("HAD %d dirty pages!\n", count);

  // haha this is really dumb
  // surely this will not come back to bite me later, right??
  INIT_LIST_HEAD(&data->ss.dirty_pages);
}

void clean_snapshot_vmas(struct task_data *data) {

  struct snapshot_vma *p = data->ss.ss_mmap;
  struct snapshot_vma *q;

  DBG_PRINT("freeing snapshot vmas\n");

  while (p != NULL) {

    DBG_PRINT("start: 0x%08lx end: 0x%08lx\n", p->vm_start, p->vm_end);
    q = p;
    p = p->vm_next;
    kfree(q);

  }

}

void clean_memory_snapshot(struct task_data *data)
{
	struct task_struct *cached_task;

	struct snapshot_page *sp;
	struct hlist_node *tmp;
	int i;

	cached_task = get_cpu_var(last_task);
	if (cached_task == current) {
		invalidate_task_data_cache();
	}
	put_cpu_var(last_task);

	if (data->config & AFL_SNAPSHOT_MMAP)
		clean_snapshot_vmas(data);

	hash_for_each_safe (data->ss.ss_page, i, tmp, sp, next) {
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

	ss_page = get_snapshot_page(data, vmf->address & PAGE_MASK);
	if (!ss_page)
		return;

	if (ss_page->dirty)
		return;
	ss_page->dirty = true;

	page_base_addr = vmf->address & PAGE_MASK;

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

  struct vm_area_struct *vma;
  struct mm_struct *     mm;
  struct task_data *     data;
  struct snapshot_page * ss_page;
  unsigned long          address;

  struct pt_regs* pregs = ftrace_get_regs(regs);

  vma = (struct vm_area_struct *)pregs->si;
  address = pregs->dx;
  mm = vma->vm_mm;
  ss_page = NULL;

  data = get_task_data_with_cache(mm->owner);

  if (data && have_snapshot(data)) {

    ss_page = get_snapshot_page(data, address & PAGE_MASK);

  } else {

    return;

  }

  if (!ss_page) {

    /* not a snapshot'ed page */
    return;

  }

  DBG_PRINT("do_anonymous_page 0x%08lx\n", address);
  // dump_stack();

  // HAVE PTE NOW
  ss_page->has_had_pte = true;
  if (is_snapshot_page_none_pte(ss_page)) {
    if (ss_page->in_dirty_list) {
      WARNF("0x%016lx: Adding page to dirty list, but it's already there??? (dirty: %d, copied: %d)\n", ss_page->page_base, ss_page->dirty, ss_page->has_been_copied);
    } else {
      ss_page->in_dirty_list = true;
      list_add_tail(&ss_page->dirty_list, &data->ss.dirty_pages);
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
