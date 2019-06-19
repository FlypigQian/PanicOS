#include <stdio.h>
#include <stdlib.h>
#include <user/syscall.h>


int
main (int argc, char **argv)
{
  for (int i = 0; i < 100; ++i)
    exec ("dd");
  return 0;
}
