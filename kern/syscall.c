/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
  user_mem_assert(curenv, s, len, PTE_U | PTE_P);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	if (e == curenv)
		cprintf("[%08x] exiting gracefully\n", curenv->env_id);
	else
		cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
  struct Env* new_env;
  int error = env_alloc(&new_env, curenv->env_id);
  if(error < 0)
    return error;

  // set up runnable status and registers
  new_env->env_status = ENV_NOT_RUNNABLE;
  new_env->env_tf = curenv->env_tf;

  // set child return (ty yeongjin)
  new_env->env_tf.tf_regs.reg_eax = 0;

  return new_env->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
  if(status == ENV_RUNNABLE || status == ENV_NOT_RUNNABLE){
    //good env status
    struct Env* target_env;
    int error = envid2env(envid, &target_env, 1);
    if(error != 0)
      return error;   // no perms or doesn't exist

    // good env
    target_env->env_status = status;
    return 0;
  }

  // bad env status
  return -E_INVAL;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
  // grab current env
  struct Env* target_env;
  int error = envid2env(envid, &target_env, 1);
  if(error != 0)
    return error;   // bad perms or does not exist

  // set upcall
  target_env->env_pgfault_upcall = func;

  // success
  return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

  // LAB 4: Your code here.
  // verify va is safe
  if((uint32_t)va >= UTOP || (uint32_t)va % PGSIZE != 0)
    return -E_INVAL;

  // check perms
  //ty yeongjin :)
  if((perm & 0xfff) & (~PTE_SYSCALL))
    return -E_INVAL;

  // grab current env
  struct Env* target_env;
  int error = envid2env(envid, &target_env, 1);
  if(error != 0)
    return -E_BAD_ENV;   // bad perms or does not exist
  
  // grab phys page
  struct PageInfo* pp;
  pp = page_alloc(ALLOC_ZERO);
  //pp = page_alloc(0);
  if(pp == NULL)
    return -E_NO_MEM;

  // try to insert
  error = page_insert(target_env->env_pgdir, pp, va, perm);
  if(error != 0){
    // free phys page b/c not in use
    page_free(pp);
    return -E_NO_MEM; // no mem for page table
  }

  // successful insert
  return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
  //cprintf("SPM: srcid = %d srcva = %08x dstid = %d dstva = %08x perm = %08x\n", srcenvid, (uint32_t) srcva, dstenvid, (uint32_t) dstva, perm);
  // check vas
  if((uint32_t)srcva >= UTOP || (uint32_t)srcva % PGSIZE != 0)
    return -E_INVAL;
  if((uint32_t)dstva >= UTOP || (uint32_t)dstva % PGSIZE != 0)
    return -E_INVAL;

  // check perms (ty yeongjin)
  if((perm & 0xfff) & (~PTE_SYSCALL))
    return -E_INVAL;

  // get environments and check them
  struct Env* srcenv;
  struct Env* dstenv;
  int error = envid2env(srcenvid, &srcenv, 1);
  if(error != 0)
    return error;   // bad perms or does not exist
  error = envid2env(dstenvid, &dstenv, 1);
  if(error != 0)
    return error;   // bad perms or does not exist

  // get src page
  pte_t* srcpte;
  struct PageInfo* pp = page_lookup(srcenv->env_pgdir, srcva, &srcpte);
  if(pp == NULL)
    return -E_INVAL; //srcva not mapped in srcenv
  //TODO: FIX ME
  if(!(*srcpte & PTE_W) && (perm & PTE_W))
    return -E_INVAL; // attempting to insert a write link to a R/O page

  // insert into dest table
  error = page_insert(dstenv->env_pgdir, pp, dstva, perm);
  if(error != 0)
    return error; // no mem to alloc page table

  // successful map
  return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
  // verify va is safe
  if((uint32_t)va >= UTOP || (uint32_t)va % PGSIZE != 0)
    return -E_INVAL;

  // grab current env
  struct Env* target_env;
  int error = envid2env(envid, &target_env, 1);
  if(error != 0)
    return error;   // bad perms or does not exist
  
  page_remove(target_env->env_pgdir, va);

  // success
  return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
  // grab target environment
  struct Env* target_env;
  int error = envid2env(envid, &target_env, 0);
  if(error != 0)
    return -E_BAD_ENV;

  // check if they're receiving
  if(target_env->env_ipc_recving != 1)
    return -E_IPC_NOT_RECV;

  // validate srcva
  if((uint32_t)srcva >= UTOP){
    // send a value, no page
    // perform ipc
    target_env->env_ipc_value = value;
    target_env->env_ipc_from = curenv->env_id;
    target_env->env_status = ENV_RUNNABLE;
    target_env->env_ipc_recving= 0;
  }else{
    // send a page
    // srscva not page alligned
    if(((uint32_t)srcva % PGSIZE) != 0)
      return -E_INVAL;

    // is recv wanting a page?
    if((uint32_t)(target_env->env_ipc_dstva) >= UTOP){
      // receiver just wants a value
      // perform ipc
      target_env->env_ipc_value = value;
      target_env->env_ipc_from = curenv->env_id;
      target_env->env_status = ENV_RUNNABLE;
      target_env->env_ipc_recving= 0;
      return 0;
    }

    // check perms
    // ...from sys_page_alloc:
    // perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
    //         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
    // PTE_P | PTE_U | PTE_W | PTE_AVAIL are all flags that are allowed to be set
    // thus if we negate it, we get all bits that aren't allowed to be set.
    // If we and with the bits not allowed to be set, then we can check if bad perms are set
    if((perm & (PTE_U | PTE_P)) == 0 || (perm & ~(PTE_P | PTE_U | PTE_W | PTE_AVAIL)) != 0)
      return -E_INVAL;

    // grab page
    struct PageInfo* pp = NULL;
    pte_t* pte;
    pp = page_lookup(curenv->env_pgdir, srcva, &pte);
    // srcva not mapped in caller's address space
    if(pp == NULL)
      return -E_INVAL;

    // check perms part 2 --
    // (perm & PTE_W), but srcva is read-only i
    if((perm & PTE_W) && (*pte & PTE_W) == 0)
      return -E_INVAL;

    // finally, copy page
    error = page_insert(target_env->env_pgdir, pp, target_env->env_ipc_dstva, perm);
    if(error != 0)
      return -E_NO_MEM;

    // perform ipc
    target_env->env_ipc_value = value;
    target_env->env_ipc_from = curenv->env_id;
    target_env->env_status = ENV_RUNNABLE;
    target_env->env_ipc_recving= 0;
  }

  return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
  // check to make sure dstva is valid
  // dstav not page alligned
  if((((uint32_t)dstva % PGSIZE) != 0) && ((uint32_t)dstva < UTOP))
    return -E_INVAL;

  // mark as wanting to receive at dstva, then block
	curenv->env_ipc_recving = 1;
  curenv->env_ipc_dstva = dstva;
  curenv->env_status = ENV_NOT_RUNNABLE;
  //ty yeongjin
  curenv->env_tf.tf_regs.reg_eax = 0;
  sched_yield();

  // after being woken up
	return 0;
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.

	switch (syscallno) {
  case SYS_cputs:
    sys_cputs((const char*) a1, (size_t) a2);
    return 0;
  case SYS_cgetc:
    return sys_cgetc();
  case SYS_getenvid:
    return sys_getenvid();
  case SYS_env_destroy:
    return sys_env_destroy((envid_t) a1);
  case SYS_yield:
    sys_yield();
    return 0;
  case SYS_exofork:
    return sys_exofork();
  case SYS_env_set_status:
    return sys_env_set_status((envid_t) a1, a2);
  case SYS_page_alloc:
    return sys_page_alloc((envid_t) a1, (void*) a2, a3);
  case SYS_page_map:
    return sys_page_map((envid_t) a1, (void*) a2, (envid_t) a3, (void*) a4, a5);
  case SYS_page_unmap:
    return sys_page_unmap((envid_t) a1, (void*) a2);
  case SYS_env_set_pgfault_upcall:
    return sys_env_set_pgfault_upcall((envid_t) a1, (void*) a2);
  case SYS_ipc_try_send:
    return sys_ipc_try_send((envid_t) a1, (uint32_t) a2, (void*) a3, (unsigned) a4);
  case SYS_ipc_recv:
    return sys_ipc_recv((void*) a1);
	default:
		return -E_INVAL;
	}
}

