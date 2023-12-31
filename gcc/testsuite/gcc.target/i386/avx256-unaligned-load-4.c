/* { dg-do compile } */
/* { dg-options "-O3 -dp -mavx -mno-avx256-split-unaligned-load -mno-avx256-split-unaligned-store -fno-common" } */

#define N 1024

float a[N+3], b[N];

void
avx_test (void)
{
  int i;

  for (i = 0; i < N; i++)
    b[i] = a[i+3] * 2;
}

/* { dg-final { scan-assembler "avx_loadups256" } } */
/* { dg-final { scan-assembler-not "sse_loadups" } } */
/* { dg-final { scan-assembler-not "vinsertf128" } } */
