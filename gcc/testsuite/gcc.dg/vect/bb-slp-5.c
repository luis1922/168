/* { dg-require-effective-target vect_int } */

#include <stdarg.h>
#include "tree-vect.h"

#define N 16 

unsigned short out[N];
unsigned short in[N] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

__attribute__ ((noinline)) int
main1 ()
{
  int i;
  unsigned short *pin = &in[0];
  unsigned short *pout = &out[0];
  
  *pout++ = *pin++;
  *pout++ = *pin++;
  *pout++ = *pin++;
  *pout++ = *pin++;
  *pout++ = *pin++;
  *pout++ = *pin++;
  *pout++ = *pin++;
  *pout++ = *pin++;

  /* Check results.  */
  if (out[0] != in[0]
      || out[1] != in[1]
      || out[2] != in[2]
      || out[3] != in[3]
      || out[4] != in[4]
      || out[5] != in[5]
      || out[6] != in[6]
      || out[7] != in[7])
    abort();

  return 0;
}

int main (void)
{
  check_vect ();

  main1 ();

  return 0;
}

/* { dg-final { scan-tree-dump-times "Vectorized basic-block" 1 "slp" } } */
/* { dg-final { cleanup-tree-dump "slp" } } */
  
