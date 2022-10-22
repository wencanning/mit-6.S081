#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  pte_t *pte;

  uint64 buf_va, mask_va;
  int len;
  if(argaddr(0, &buf_va) < 0) {
    return -1;
  }
  if(argint(1, &len) < 0) {
    return -1;
  }
  if(argaddr(2, &mask_va) < 0) {
    return -1;
  }

  int mask = 0;

  if(len > 32) 
    return -1;

  buf_va = PGROUNDDOWN(buf_va);

  for(int i = 0; i < len; i++) {
    uint64 a = buf_va + i * PGSIZE;
    if((pte = walk(myproc()->pagetable, a, 0)) == 0)
      return -1;    
    if(*pte & PTE_A) {
      mask |= (1 << i); 
      *pte ^= PTE_A;
    }
  }

  if(copyout(myproc()->pagetable, (uint64)mask_va, (char*)(&mask), 4) < 0) {
    return -1;
  }
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_trace(void)
{
  int mask;

  if(argint(0, &mask) < 0)
    return -1;
  myproc()->mask = mask;
  return 0;
}

uint64 
sys_sysinfo(void) 
{ 
  uint64 addr;
  struct sysinfo *p_info;
  struct proc *p = myproc();

  if(argaddr(0, &addr) < 0) 
    return -1;
  p_info = (struct sysinfo*) addr; 
  
  uint64 freemem = kfreenum();
  uint64 nproc   = procnum();

  if(copyout(p->pagetable, (uint64)&p_info->freemem, (char*)&freemem, sizeof(freemem)) < 0) {
    return -1;
  }
  if(copyout(p->pagetable, (uint64)&p_info->nproc, (char*)&nproc, sizeof(nproc)) < 0) {
    return -1;
  }
  return 0;
}