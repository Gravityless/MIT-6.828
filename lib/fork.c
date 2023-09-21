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
	if ((err & FEC_WR) == 0 || (uvpt[PGNUM(addr)] & PTE_COW) == 0)
        panic("pgfault: check faluting access failed");
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
    // 在获取当前进程的id时千万不要使用thisenv，而应该用sys_getenvid()系统调用。
    // 若使用thisenv，则当子进程开始运行时，其栈页面处于COW状态；
    // 进入子进程之后的第一件事儿就是读取将寄存器eax的值作为int中断指令的“返回值”，
    // 然后从接口函数sys_exofork返回，而return操作势必会用到进程栈！
    // 所以，处理器马上就会进入页错误处理程序，而此时thisenv还没有来得及修正！
    envid_t envid = sys_getenvid();    // do not use thisenv!
    if ((r = sys_page_alloc(envid, PFTEMP, PTE_P|PTE_U|PTE_W)) < 0)
        panic("sys_page_alloc: %e", r);
    
    memmove(PFTEMP,ROUNDDOWN(addr, PGSIZE), PGSIZE);
    
    if ((r = sys_page_map(envid, PFTEMP, envid, ROUNDDOWN(addr, PGSIZE), PTE_P|PTE_U|PTE_W)) < 0)
        panic("sys_page_map: %e", r);
    if ((r = sys_page_unmap(envid, PFTEMP)) < 0)
        panic("sys_page_unmap: %e", r);

	// panic("pgfault not implemented");
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
	// panic("duppage not implemented");
    void* va = (void*)(pn << PGSHIFT);
    int perm = uvpt[pn] & 0xFFF;
    if ((perm & PTE_W) || (perm & PTE_COW)) {
        perm |= PTE_COW; // 增加 PTE_COW
        perm &= ~PTE_W;  // 减去 PTE_W
    }
    perm &= PTE_SYSCALL;

    // 为什么要先映射子进程的页面为PTE_COW，然后再映射父进程的页面，顺序是否可以交换？
    // 答案是不能，关键在于栈所在的页。若先映射父进程的栈页面为PTE_COW，
    // 那么在执行第二个sys_page_map时，由于函数调用必定会对进程的栈产生写操作；
    // 而父进程的栈此时已经被标记为PTE_COW，所以会导致页错误；
    // 进程的页错误处理程序会重新申请一个物理页，并让原先的栈所在的虚拟页指向这个物理页，
    // 且父进程对这个物理页是有写权限的；随后，问题出现了：
    // 当回到引起页错误的第二个sys_page_map继续执行时，
    // 为子进程建立的映射页会使它的栈指向该刚刚申请的物理页且权限为PTE_COW，
    // 与此同时父进程却可以写入该页面，这就与写时复制的规则相违背了！
    if ((r = sys_page_map(thisenv->env_id, va, envid, va, perm)) < 0)
        panic("sys_page_map: %e", r);

    // 为什么即使父进程的页面已经为PTE_COW的情况下也还要再对其做一次映射？
    // 原理和前面类似，依然是考虑栈页面。若父进程的栈页面是PTE_COW的，
    // 在对子进程建立映射时会发生和前面相同的情况，之后必须把已经被改为
    // 父进程可写的新的栈页面再次映射为PTE_COW。
    if ((r = sys_page_map(thisenv->env_id, va, thisenv->env_id, va, perm)) < 0)
        panic("sys_page_map: %e", r);
    return r;
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
	// panic("fork not implemented");
	set_pgfault_handler(pgfault);
    envid_t envid = sys_exofork();
    if (envid < 0)
        panic("sys_exofork: %e", envid);
    if (envid == 0) {
        // 在子进程
        thisenv = &envs[ENVX(sys_getenvid())];
        return 0;
    }
    // 在父进程
    for (uintptr_t addr = UTEXT; addr < USTACKTOP; addr += PGSIZE)
        if (uvpd[PDX(addr)] & PTE_P && uvpt[PGNUM(addr)] & PTE_P)
            duppage(envid, PGNUM(addr));
    int r;
    // 给子进程分配 exception stack page
    if ((r = sys_page_alloc(envid, (void*)(UXSTACKTOP - PGSIZE), PTE_P | PTE_U | PTE_W)) < 0)
        panic("sys_page_alloc: %e", r);
    extern void _pgfault_upcall(void);
    if ((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
        panic("sys_env_set_pgfault_upcall: %e", r);
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
