// Test parsing of #pragma omp declare simd
// { dg-do compile }

#pragma omp declare simd
int a;	// { dg-error "not immediately followed by function declaration or definition" }

#pragma omp declare simd
int fn1 (int a), fn2 (int a);	// { dg-error "not immediately followed by a single function declaration or definition" }

#pragma omp declare simd
int b, fn3 (int a);	// { dg-error "not immediately followed by function declaration or definition" }

#pragma omp declare simd linear (a)
int fn4 (int a), c;	// { dg-error "not immediately followed by function declaration or definition" }

#pragma omp declare simd
extern "C"		// { dg-error "not immediately followed by function declaration or definition" }
{
  int fn5 (int a);
}

#pragma omp declare simd // { dg-error "not immediately followed by function declaration or definition" }
namespace N1
{
  int fn6 (int a);
}

#pragma omp declare simd simdlen (4)
struct A
{			// { dg-error "not immediately followed by function declaration or definition" }
  int fn7 (int a);
};

#pragma omp declare simd
template <typename T>
struct B
{			// { dg-error "not immediately followed by function declaration or definition" }
  int fn8 (int a);
};

struct C
{
#pragma omp declare simd // { dg-error "not immediately followed by function declaration or definition" }
  public:		 // { dg-error "expected unqualified-id before" }
    int fn9 (int a);
};

int t;

#pragma omp declare simd
#pragma omp declare simd
#pragma omp threadprivate(t)	// { dg-error "not immediately followed by function declaration or definition" }
int fn10 (int a);

#pragma omp declare simd inbranch notinbranch
int fn11 (int);		// { dg-error "clause is incompatible with" }

#pragma omp declare simd simdlen (N)	// { dg-error "was not declared in this scope" }
template <int N>
int fn12 (int);
