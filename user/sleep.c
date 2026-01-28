#include "kernel/types.h"
#include "user/user.h"

int 
main(int argc, char const *argv[]) 
{
  if (argc != 2) {
    fprintf(2, "Usage: sleep SECONDS\n");
    exit(1);
  }
  int time = atoi(argv[1]);
  pause(time);
  exit(0); 
}
