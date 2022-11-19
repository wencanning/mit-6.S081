// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);
void* steal(int cid);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem2[NCPU];

char buf[20];

void
kinit()
{
  push_off();
  for(int i = 0; i < NCPU; i++) {
    snprintf(buf, sizeof(buf), "kmem-%d", i);
    initlock(&kmem2[i].lock, buf);
  }
  freerange(end, (void*)PHYSTOP);
  pop_off();
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

void kfree(void *pa) {
  push_off();
  int cid = cpuid();
  acquire(&kmem2[cid].lock);

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  struct run *r;
  memset(pa, 1, PGSIZE);
  r = (struct run *)pa;
  r->next = kmem2[cid].freelist;
  kmem2[cid].freelist = r;

  release(&kmem2[cid].lock);
  pop_off();
}

void* kalloc(void) {
  push_off();
  int cid = cpuid();
  acquire(&kmem2[cid].lock);

  struct run *r;
  r = kmem2[cid].freelist;
  if(r) {  
    kmem2[cid].freelist = r->next;
  }
  else {
    r = (struct run*)steal(cid);
  }

  release(&kmem2[cid].lock);
  pop_off();

  if(r)
    memset((char*)r, 5, sizeof PGSIZE);
  return (void*)r;
}

void *steal(int cid) {
  release(&kmem2[cid].lock);
  for(int i = 0; i < NCPU; i++) {
    if(cid == i) continue;
    acquire(&kmem2[i].lock);

    if(kmem2[i].freelist) {
      acquire(&kmem2[cid].lock);
      struct run *r = (struct run*)kmem2[i].freelist;
      kmem2[i].freelist = r->next;
      release(&kmem2[i].lock);
      return (void*)r;
    }

    release(&kmem2[i].lock);
  }
  acquire(&kmem2[cid].lock);
  return (void *)0;
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// void
// kfree(void *pa)
// {
//   struct run *r;

//   if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
//     panic("kfree");

//   // Fill with junk to catch dangling refs.
//   memset(pa, 1, PGSIZE);

//   r = (struct run*)pa;

//   acquire(&kmem.lock);
//   r->next = kmem.freelist;
//   kmem.freelist = r;
//   release(&kmem.lock);
// }

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// void *
// kalloc(void)
// {
//   struct run *r;

//   acquire(&kmem.lock);
//   r = kmem.freelist;
//   if(r)
//     kmem.freelist = r->next;
//   release(&kmem.lock);

//   if(r)
//     memset((char*)r, 5, PGSIZE); // fill with junk
//   return (void*)r;
// }
