// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define STEAL_PAGE_NUM 10

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  uint nfree; // 记录拥有的空闲物理页数量
} kmem[NCPU];

/*
  这里采取的思路是先将所有空闲链表挂在初始化的那个cpu上(利于cpu0)
  当其他cpu核心发现freelist为空的时候，就去init的那个cpu上"偷取"物理页
  采用批量偷取的方法
*/
void
kinit()
{
  for(int i = 0; i < NCPU; ++i){
    initlock(&kmem[i].lock, "kmem");
    kmem[i].freelist = 0;
    kmem[i].nfree = 0;
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;

  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  push_off();
  uint64 core_id = cpuid();

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // 释放物理页到当前cpu的链表中
  acquire(&kmem[core_id].lock);
  r->next = kmem[core_id].freelist;
  kmem[core_id].freelist = r;
  kmem[core_id].nfree += 1;
  release(&kmem[core_id].lock);

  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  uint64 core_id = cpuid();

  acquire(&kmem[core_id].lock);

  r = kmem[core_id].freelist;

  if(r){
    kmem[core_id].freelist = r->next;
    kmem[core_id].nfree -= 1;
  }

  release(&kmem[core_id].lock);
  if(!r){
    // 从其他核心获取PAGE
    for(int i = 0; i < NCPU; ++i){
      if(i == core_id) continue;

      if(kmem[i].nfree <= 0) continue;

      acquire(&kmem[i].lock);
      if(kmem[i].nfree > 0){

        struct run *head = kmem[i].freelist; // 找到返回的指针
        struct run *p = head;
        int count = 1;
        int num = (STEAL_PAGE_NUM > kmem[i].nfree) ? kmem[i].nfree : STEAL_PAGE_NUM;

        // 遍历找到需要更新的指针
        while(p->next && count < num){
          p = p->next;
          count += 1;
        }

        kmem[i].freelist = p->next;
        kmem[i].nfree -= count;
        release(&kmem[i].lock);

        p->next = 0;  // 截断取走的指针
        r = head;
        if(count > 1){
          struct run *stolen = head->next;
          acquire(&kmem[core_id].lock);
          // 挂上偷取的物理页
          p->next = kmem[core_id].freelist;
          kmem[core_id].freelist = stolen;
          kmem[core_id].nfree += count - 1;
          release(&kmem[core_id].lock);
        }
        break;
      }
      release(&kmem[i].lock);
    }
  }
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
