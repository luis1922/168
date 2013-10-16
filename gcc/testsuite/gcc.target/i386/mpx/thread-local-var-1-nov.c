/* { dg-do run } */
/* { dg-options "-fcheck-pointers -mmpx" } */
/* { dg-skip-if "" { *-*-* } { "-flto" } { "" } } */

#include "mpx-check.h"
#include "pthread.h"

__thread int prebuf[100];
__thread int buf[100];
__thread int postbuf[100];

int rd (int *p, int i)
{
  int res = p[i];
  printf("%d\n", res);
  return res;
}

void *thred_func (void *ptr)
{
  rd (buf, 0);
  rd (buf, 99);
}

int mpx_test (int argc, const char **argv)
{
  pthread_t thread;
  pthread_create (&thread, NULL, thred_func, 0);
  pthread_join (thread, NULL);
  return 0;
}
