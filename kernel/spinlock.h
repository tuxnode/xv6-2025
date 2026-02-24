// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
#ifdef LAB_LOCK
  int nts;
  int n;
#endif
};

#ifdef LAB_LOCK
// Reader-writer lock.
struct rwspinlock {
  // Replace this with your implementation.
  struct spinlock l;
  uint readers; // 读者数量
  uint is_write;  // 是否处于写入状态
  uint pending_writers;  // 排队的写入数量
};
#endif
