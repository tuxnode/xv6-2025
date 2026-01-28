#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

char *separators = " -\r\t\n./,";

void
process_file(int fd)
{
  char buf[64];
  int i = 0; // 有效数字计数器
  char c;

  while (read(fd, &c, 1) > 0) {
    if (strchr(separators, c)) {
      // 遇到分隔符，如果缓冲区有数字，处理它
      if (i > 0) {
        buf[i] = '\0';
        // atoi函数读取到字母的时候就会停止
        int num = atoi(buf);
        if (num % 5 == 0 || num % 6 == 0) {
          printf("%d\n", num);
        }
        i = 0; 
      }
    } 
    else if (c >= '0' && c <= '9') {
      // 遇到数字，检查大小并存入缓冲区
      if (i < sizeof(buf) - 1) {
        buf[i++] = c;
      }
    } 
    else {
      i = 0;
    }
  }

  // 重要：循环结束后，处理文件末尾没有分隔符的情况
  if (i > 0 && i <= sizeof(buf) - 1) {
    buf[i] = '\0';
    int num = atoi(buf);
    if (num % 5 == 0 || num % 6 == 0) {
      printf("%d\n", num);
    }
  }
}

int 
main(int argc, char *argv[]) // 去掉 const，保持 xv6 风格
{ 
  if (argc < 2) {
    fprintf(2, "Usage: sixfive FILE...\n");
    exit(1);
  }

  // i 从 1 开始，argv[0] 是程序名
  for (int i = 1; i < argc; ++i) {
    int fd = open(argv[i], 0);
    if (fd < 0) {
      fprintf(2, "sixfive: cannot open %s\n", argv[i]);
      continue;
    }
    process_file(fd);
    close(fd);
  }

  exit(0);
}
