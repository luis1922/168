/* { dg-do run { xfail *-*-* } } */
/* { dg-options "-fmpx" } */
/* { dg-skip-if "" { *-*-* } { "-flto" } { "" } } */

#define XFAIL

#include "mpx-check.h"
#include "stdarg.h"

int buf[100];
int buf1[10];

int
rd (int *pppp, int n, ...)
{
  va_list argp;
  int *p;
  int i;
  int res;

  va_start (argp, n);
  for (; n > 0; n--)
    va_arg (argp, int *);
  p = va_arg (argp, int *);
  i = va_arg (argp, int);

  res = p[i];
  printf ("%d\n", res);

  return res;
}

int mpx_test (int argc, const char **argv)
{
  rd (buf1, 4, buf1, buf1, buf1, buf1, buf, -1, buf1);
  return 0;
}
