// Physical memory allocator definitions
struct user_physical_page_ref {
  uint32 ref;
  struct spinlock lock;
};
