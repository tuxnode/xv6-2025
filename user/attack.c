#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define CAPACITY 4096 * 10

int is_secret_char(char c) {
  return (c >= 'a' && c <= 'z') || 
         (c >= 'A' && c <= 'Z') || 
         (c >= '0' && c <= '9');
}

int
main(int argc, char *argv[])
{
  // Your code here.
  char *p = sbrk(CAPACITY);

  if (p == (char *)-1) {
    printf("Allocation failed\n");
    exit(1);
  }
  for (int i = 0; i < CAPACITY; ++i) {
    if (is_secret_char(p[i])) {
      int len = 0;
      // 检查后续
      while (CAPACITY > (i + len) && is_secret_char(p[i + len])) {
        len++;
      }
      if (len > 5) {
        for (int j = 0; j < len; ++j) {
          printf("%c", p[i + j]);
        }
        printf("\n");

        exit(0);
      }
      i += len;
    }
  }
  exit(0);
}
