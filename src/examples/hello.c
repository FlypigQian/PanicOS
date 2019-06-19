#include <stdio.h>
#include <stdlib.h>
#include <user/syscall.h>


int
main (int argc, char **argv)
{
  ASSERT (argc == 2)
  int n = atoi (argv[1]);
  printf ("Enter %d\n", n);

  if (n == 0)
    {
      return 0;
    }

  char child_cmd[128];
  snprintf (child_cmd, sizeof child_cmd, "hello %d", n - 1);
  pid_t id = exec (child_cmd);
  wait (id);

  return 0;
}
