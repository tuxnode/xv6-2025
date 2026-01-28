#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char *fmt, char *data);

int
main(int argc, char *argv[])
{
  if(argc == 1){
    printf("Example 1:\n");
    int a[2] = { 61810, 2025 };
    memdump("ii", (char*) a);
    
    printf("Example 2:\n");
    memdump("S", "a string");
    
    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *) &s);

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;
    
    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");
    
    printf("Example 4:\n");
    memdump("pihcS", (char*) &example);
    
    printf("Example 5:\n");
    memdump("sccccc", (char*) &example);
  } else if(argc == 2){
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while(n < sizeof(data)){
      int nn = read(0, data + n, sizeof(data) - n);
      if(nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

void
memdump(char *fmt, char *data)
{
  // Your code here.
  char *p = fmt;
  char *d = data; // 使用临时指针操作数据

  while (*p) {
    char f = *p;
    
    if (f == 'i') {
      // 32位整数
      int val = *(int *)d;
      printf("%d\n", val);
      d += 4;
    } 
    else if (f == 'p') {
      // 64位十六进制
      uint64 val = *(uint64 *)d;
      printf("%x\n", (uint)val);
      d += 8;
    } 
    else if (f == 'h') {
      // 16位整数
      short val = *(short *)d;
      printf("%d\n", val);
      d += 2;
    } 
    else if (f == 'c') {
      printf("%c\n", *d);
      d += 1;
    } 
    else if (f == 's') {
      // 指向字符串的指针
      char *str_ptr = *(char **)d;
      printf("%s\n", str_ptr);
      d += 8;
    } 
    else if (f == 'S') {
      // 剩余数据本身就是字符串
      printf("%s\n", d);
      return;
    }
    // 移动到下一个数据块
    p++; 
  } 
}
