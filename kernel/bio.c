// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct buf buf[NBUF];
  struct buf bucket[NBUCKET];
  
  struct spinlock lock;
  struct spinlock buclock[NBUCKET];

  struct buf *freelist;
  int rest;
} bcache;


void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  
  bcache.freelist = 0;
  bcache.rest = NBUF;
  for(int i = 0; i < NBUCKET; i++) {
    bcache.bucket[i].prev = &bcache.bucket[i];
    bcache.bucket[i].next = &bcache.bucket[i];
    initlock(&bcache.buclock[i], "bcache.bucket");
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "bcache.buffer");
    b->next = bcache.freelist;
    bcache.freelist = b;
  }
}

void 
removenode(struct buf *a, struct buf *b) {
  b->next->prev = b->prev;
  b->prev->next = b->next;
  b->next = a->next;
  b->prev = a;
  
  b->next->prev = b;
  a->next = b;
}

void update_block(uint dev, uint blockno, struct buf* ad) {
  ad->dev = dev;
  ad->blockno = blockno;
  ad->valid = 0;
  ad->refcnt = 1;
}

static struct buf *
bmatch(uint dev, uint blockno, uint bid) 
{
  struct buf * b;
  for(b = bcache.bucket[bid].next; b != &bcache.bucket[bid]; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      return b;
    }
  }
  return (struct buf *)0;
}

static struct buf * 
blru(int id) 
{
  struct buf *b, *res = 0;
  uint mitiks = 0x3f3f3f3f;
  for(b = bcache.bucket[id].next; b != &bcache.bucket[id]; b = b->next) {
    if(b->refcnt == 0 && b->ticks < mitiks) {
      res = b;
      mitiks = b->ticks; 
    }
  }
  return res;
}

static struct buf * 
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint bid = BID(blockno);
  acquire(&bcache.buclock[bid]);

  if((b = bmatch(dev, blockno, bid)) != 0) {
    b->refcnt++;
    release(&bcache.buclock[bid]);
    acquiresleep(&b->lock);
    return b;
  }

  if((b = blru(bid)) != 0) {
    update_block(dev, blockno, b);
    release(&bcache.buclock[bid]);
    acquiresleep(&b->lock);
    return b;
  }

  release(&bcache.buclock[bid]);

  acquire(&bcache.lock);
  acquire(&bcache.buclock[bid]);

  if((b = bmatch(dev, blockno, bid)) != 0) {
    b->refcnt++;
    release(&bcache.buclock[bid]);
    release(&bcache.lock);
    acquiresleep(&b->lock);
    return b;
  }

  if(bcache.rest) {
    bcache.rest--;
    b = bcache.freelist;
    bcache.freelist = b->next; 

    update_block(dev, blockno, b);
    b->next = bcache.bucket[bid].next;
    b->prev = &bcache.bucket[bid];
    bcache.bucket[bid].next->prev = b;
    bcache.bucket[bid].next = b;

    release(&bcache.buclock[bid]);
    release(&bcache.lock);
    acquiresleep(&b->lock);
    return b;
  }
 
  for(int i = 1; i < NBUCKET; i++) {
    int new = (i + bid) % NBUCKET;
    acquire(&bcache.buclock[new]);

    if((b = blru(new)) != 0) {
      update_block(dev, blockno, b);
      removenode(&bcache.bucket[bid], b);

      release(&bcache.buclock[new]);
      release(&bcache.buclock[bid]);
      release(&bcache.lock);

      acquiresleep(&b->lock);
      return b;
    }

    release(&bcache.buclock[new]);
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint uid = BID(b->blockno);
  acquire(&bcache.buclock[uid]);

  b->refcnt--;
  b->ticks = ticks;   

  release(&bcache.buclock[uid]);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


