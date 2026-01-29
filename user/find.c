#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"
#include "user/user.h"

char *
get_file_name(char *path)
{
  char *p;
  for (p = path + strlen(path); p >= path && *p != '/'; --p);
  return p + 1;
}

void
run_exec(char *path, int argc, char *argv[])
{
  int pid = fork();
  if (pid < 0) {
    fprintf(2, "find exec: fork failed\n");
    return;
  }

  if (pid == 0) {
    // 处理子进程
    char *cmd_argv[MAXARG];
    int i, j = 0;

    // 拷贝参数
    for (i = 4; i < argc; ++i) {
      cmd_argv[j++] = argv[i];
    }
    cmd_argv[j++] = path;
    cmd_argv[j] = 0;

    exec(cmd_argv[0], cmd_argv);
    fprintf(2, "find: exec %s failed\n", cmd_argv[0]);
    exit(1);
  } else wait(0);
}

void
find(char *path, char *key_word, int argc, char *argv[])
{
  char buf[512];
  int fd;
  struct stat st;
  struct dirent de;
  char *p;
  if ((fd = open(path, O_RDONLY)) < 0) {
    fprintf(2, "find: can not open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: can not get stat %s\n", path);
    close(fd);
    return;
  }

  switch (st.type) {
    case T_DEVICE: // 设备文件
    case T_FILE: 
      if (strcmp(key_word, get_file_name(path)) == 0) {
        if (argc > 3 && strcmp(argv[3], "-exec") == 0) {
          run_exec(path, argc, argv);
        } else printf("%s\n", path);
      }
      break;
    case T_DIR:
    // 拼接新路径
    // 检查路径是否过长
      if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
        printf("find: path is too long!\n");
        break;
      }
      strcpy(buf, path);
      p = buf + strlen(buf);
      *(p++) = '/';

      while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue;
        if (strcmp(".", de.name) == 0 || strcmp("..", de.name) == 0) continue;

        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;

        find(buf, key_word, argc, argv);
      }
      break;
  }
  close(fd);
}



int
main(int argc, char *argv[])
{
  if (argc < 3) {
    fprintf(2, "Usage: find DIR KEY_WORD\n");
    exit(1);
  }

  find(argv[1], argv[2], argc, argv);
  exit(0);
}
