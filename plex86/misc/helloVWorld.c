#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

  int
main()
{
  pid_t myPid, parentPid, childPid;

  printf("\nUser program, installed as /linuxrc on initial ramdisk image.\n");

  myPid     = getpid();
  printf("my pid is %u.\n", (unsigned) myPid);

  parentPid = getppid();
  printf("parent process pid is %u.\n", (unsigned) parentPid);

  printf("forking child process...\n");
  childPid = fork();
  if ( childPid<0 ) {
    fprintf(stderr, "Ouch, fork() failed!\n");
    exit(1);
    }
  if ( childPid == 0 ) {
    // This is the child process.
    printf("Child process: pid from getpid() is %u.\n", (unsigned) getpid());
    exit(0);
    }
  printf("Parent process: child pid from fork was %u.\n", (unsigned) childPid);
  waitpid(childPid, NULL, 0);
  printf("\nHello virtual world!\n");

  // Sleep for a long time...  Since this is the init process, don't
  // die because init likes to live through the system uptime.
  sleep(10000000);
  return(0);
}
