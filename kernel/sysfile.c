//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

static struct VMA*
VMAalloc()
{
  struct proc *p = myproc();
  for(int i = 0; i < NVMA; i++) {
    if(p->vmas[i].f == 0) {
      return &p->vmas[i];
    }
  }
  panic("VMAalloc");
}

struct VMA* 
getVMA(uint64 addr) 
{
  struct proc *p = myproc();
  for(int i = 0; i < NVMA; i++) {
    if(p->vmas[i].addr <= addr && addr < p->vmas[i].addr + p->vmas[i].len) {
      return &p->vmas[i];
    }
  }
  return (struct VMA*)-1; 
}

int
vmap(struct VMA* vma, uint64 addr) 
{
  struct proc *p = myproc();
  uint64 pa = (uint64)kalloc();
  uint64 perm = PTE_U;
  if(vma->prot & PROT_READ) perm |= PTE_R;
  if(vma->prot & PROT_WRITE) perm |= PTE_W;
  if(vma->prot & PROT_EXEC) perm |= PTE_X;
  memset((void*)pa, 0, PGSIZE);
  pte_t *pte;
  if((pte = walk(p->pagetable, addr, 1)) == 0)
    return -1;
  if(*pte & PTE_V)
    panic("vmap: remap");
  *pte = PA2PTE(pa) | perm | PTE_V;
  // printf("maped %p\n", PGROUNDDOWN(addr));

  struct file *f = vma->f;
  struct inode *in = f->ip;

  uint64 off = addr - vma->addr + vma->off;
  begin_op();
  ilock(in);
  for(int i = 0; i < PGSIZE / BSIZE; i++) {
    int n = readi(in, 0, pa + i * BSIZE, off + i * BSIZE, BSIZE);
    if(n == -1) {
      panic("vmap: readi");
    }
  }
  iunlock(in);
  end_op();
  return 0;
}

void 
vclear(struct VMA *vma) 
{
  memset(vma, 0, sizeof(struct VMA));
}

uint64
sys_mmap(void)
{
  uint64 addr;
  uint len, prot, flag, off, fd;
  struct file * f;
  struct proc * p = myproc();
  struct VMA * vma = VMAalloc();
  if(argaddr(0, &addr) == -1 || argint(1, (int*)&len) == -1 || argint(2, (int*)&prot) == -1 || 
        argint(3, (int*)&flag) == -1 || argint(4, (int*)&fd) == -1 || argint(5, (int*)&off) == -1) {
    return -1;
  }
  f = p->ofile[fd];
  if(flag & MAP_SHARED) {
    if(!f->readable && (prot & PROT_READ)) return -1;
    if(!f->writable && (prot & PROT_WRITE)) return -1;
  }
  if(off % PGSIZE != 0) return -1;
  if(addr == 0) {
    if(off == 0) {
      vma->f = filedup(f);
      vma->len = (len + PGSIZE - 1) / PGSIZE * PGSIZE; 
      vma->addr = p->vmaaddr - vma->len;
      p->vmaaddr = vma->addr; 
      vma->flag = flag;
      vma->prot = prot;
      vma->off = off;
      // printf("create [%p, %p), \n", vma->addr, vma->addr+vma->len);
      return vma->addr;
    }else {
      return -1;
    }
  }else {
    return -1;
  }
  return -1;
}

uint64
sys_munmap(void)
{
  uint64 addr;
  uint len;
  if(argaddr(0, &addr) == -1 || argint(1, (int*)&len) == -1) {
    return -1;
  }
  return munmap(getVMA(addr), addr, len);
}

int 
munmap(struct VMA *vma, uint64 addr, uint len) 
{
  struct proc *p = myproc();
  struct file *f = vma->f;
  if(vma == (struct VMA*)-1) return -1;
  if(addr < vma->addr) return -1;
  if(addr + len > vma->addr + vma->len) return -1;

  if(vma->flag & MAP_SHARED) {
    uint off, r;
    for(uint64 i = PGROUNDDOWN(addr); i < addr + len; i += PGSIZE) {
      off = i - vma->addr + vma->off;
      pte_t *pte = walk(p->pagetable, i, 0);
      // printf("want to write %p\n", i);
      if(pte == 0 || !(*pte & PTE_V) || !(*pte & PTE_D)) {
        // printf("%p not maped\n", i);
        continue;
      }
      begin_op();
      ilock(f->ip);
      r = writei(f->ip, 1, i, off, PGSIZE);
      iunlock(f->ip);
      end_op();
      if(r != PGSIZE) {
        // printf("r = %d\n", r);
        // panic("r != PGSIZE");
        return -1;
      }
    }
  }
  uint64 npages = 0;
  for(uint64 i = PGROUNDDOWN(addr); i < addr + len; i += PGSIZE) {
    npages++;
    pte_t *pte = walk(p->pagetable, i, 0);
    if(pte != 0 && (*pte & PTE_V)) {
      // printf("unmaped: %p\n", i);
      uvmunmap(p->pagetable, i, 1, 1);
    }
  }
  //修改vma中的区间和分段的大小, 并释放相应的page
  if(vma->addr == addr && vma->len == len) {
    vclear(vma);
    fileclose(f);
  }else if(vma->addr == addr) {
    vma->addr += npages * PGSIZE;
    vma->len  -= npages * PGSIZE;
    vma->off  += npages * PGSIZE;
  }else if(addr + len == vma->addr + vma->len){
    vma->len  -= npages * PGSIZE;
  }else {
    panic("sys_munmap: wrong range");
  }
  return 0; 
}