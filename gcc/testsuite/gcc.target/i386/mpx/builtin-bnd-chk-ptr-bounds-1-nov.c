/* { dg-do run } */
/* { dg-options "-fmpx" } */
/* { dg-skip-if "" { *-*-* } { "-flto" } { "" } } */

#include "mpx-check.h"

int buf[100];

int mpx_test (int argc, const char **argv)
{
  __bnd_chk_ptr_bounds (buf, sizeof (int) * 100);
  return 0;
}
