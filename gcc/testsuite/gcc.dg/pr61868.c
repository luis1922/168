/* { dg-do compile } */
/* { dg-options "-flto -frandom-seed=0x12345" }  */
extern int foo (int);
int main ()
{
  foo (100);
  return 0;
}
/* { dg-final { scan-assembler "\.gnu\.lto.*.12345" } } */
