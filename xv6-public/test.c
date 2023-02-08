#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
   int pid;
   int ppid;
   pid = getpid();
   ppid = getppid();
   printf(1, "My	process ID: %d\n", pid);
   printf(1, "Parent	process ID: %d\n", ppid);
   exit();
}
