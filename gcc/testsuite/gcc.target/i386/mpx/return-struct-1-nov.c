/* { dg-do run } */
/* { dg-options "-fmpx" } */
/* { dg-skip-if "" { *-*-* } { "-flto" } { "" } } */

#include "mpx-check.h"

struct s1
{
  int *p;
} s1;

int buf[100];

struct s1 __attribute__((noinline)) get ()
{
  struct s1 s;
  s.p = buf;
  return s;
}

int __attribute__((noinline)) rd (struct s1 s, int i)
{
  int res = s.p[i];
  printf ("%d\n", res);
  return res;
}

int mpx_test (int argc, const char **argv)
{
  struct s1 s = get ();

  rd (s, 0);
  rd (s, 99);

  return 0;
}
