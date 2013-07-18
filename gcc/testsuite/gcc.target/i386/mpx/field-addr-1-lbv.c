/* { dg-do run { xfail *-*-* } } */
/* { dg-options "-fmpx" } */
/* { dg-skip-if "" { *-*-* } { "-flto" } { "" } } */

#define XFAIL

#include "mpx-check.h"

struct S {
  int a;
  int b[100];
  int c;
} S;

int foo (int *i, int k)
{
  printf ("%d\n", i[k]);
  return i[k];
}

int mpx_test (int argc, const char **argv)
{
  struct S s;

  foo(&s.a, -1);

  return 0;
}
