// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
  // check if fault was a write
  if(((err & FEC_WR) != 0) && ((uvpt[PGNUM(addr)] & (PTE_COW | PTE_P)) != 0)){
    // good
  }else{
    panic("pgfault error in COW handler");
  }


	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
  // request new page at PFTEMP
	if ((r = sys_page_alloc(0, (void*)PFTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_alloc: %e", r);
  // move mem to temp page
  memcpy(PFTEMP, (void*)PTE_ADDR(addr), PGSIZE);
  // insert new mapping and free 
	if ((r = sys_page_map(0, (void*)PFTEMP, 0, (void*)PTE_ADDR(addr), PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e", r);
	if ((r = sys_page_unmap(0, (void*)PFTEMP)) < 0)
		panic("sys_page_unmap: %e", r);

}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
  // check if page is writable or COW
  if(((uvpt[pn] & PTE_W) != 0) || ((uvpt[pn] & PTE_COW) != 0)){
    // from yeongjin -- map child first, then parent's
    // src then dest arg order.... envid = child's, 0 = cur (parent)
    if ((r = sys_page_map(0, (void*)(pn*PGSIZE), envid, (void*)(pn*PGSIZE), PTE_P|PTE_U|PTE_COW)) < 0)
      panic("sys_page_map: %e", r);
    // map parent to self
    if ((r = sys_page_map(0, (void*)(pn*PGSIZE), 0, (void*)(pn*PGSIZE), PTE_P|PTE_U|PTE_COW)) < 0)
      panic("sys_page_map: %e", r);
  }else{
    // create read only mapping
    if ((r = sys_page_map(0, (void*)(pn*PGSIZE), envid, (void*)(pn*PGSIZE), PTE_P|PTE_U)) < 0)
      panic("sys_page_map: %e", r);
  }
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
  // copy code from user/dumbfork() and modify it so it's right
	envid_t envid;
	uint32_t addr;
	int r;

  // install pgfault handler
  set_pgfault_handler(&pgfault);

	// Allocate a new child environment.
	// The kernel will initialize it with a copy of our register state,
	// so that the child will appear to have called sys_exofork() too -
	// except that in the child, this "fake" call to sys_exofork()
	// will return 0 instead of the envid of the child.
	envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %e", envid);
	if (envid == 0) {
		// We're the child.
		// The copied value of the global variable 'thisenv'
		// is no longer valid (it refers to the parent!).
		// Fix it and return 0.
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// We're the parent.
	// copy everything writable / COWable below UTOP
	for (addr = UTEXT; addr < UTOP; addr += PGSIZE){
    // exception -- don't copy UXSTACK
    if(addr == (UXSTACKTOP - PGSIZE))
      continue;

    // only copy pages that exist
    if(((uvpd[PDX(addr)] & PTE_P) != 0) && ((uvpt[PGNUM(addr)] & PTE_P) != 0)){
      duppage(envid, addr / PGSIZE);
    }
  }

  // add a page for UXSTACK
  if((r = sys_page_alloc(envid, (void*)(UXSTACKTOP - PGSIZE), PTE_W | PTE_U | PTE_P)) < 0)
    panic("sys_page_alloc: %e", r);

  // set pgfault upacll to handle cow
  sys_env_set_pgfault_upcall(envid, thisenv->env_pgfault_upcall);

	// Also copy the stack we are currently running on.
	duppage(envid, ((uint32_t)ROUNDDOWN(&addr, PGSIZE)) / PGSIZE);

	// Start the child environment running
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
