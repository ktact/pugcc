#!/bin/bash
cat <<EOF | gcc -xc -c -o tmp2.o -
#include <stdlib.h>

int ret3() { return 3; }
int ret5() { return 5; }
int add(int x, int y) { return x + y; }
int sub(int x, int y) { return x - y; }
int add6(int a, int b, int c, int d, int e, int f) {
  return a + b + c + d + e + f;
}
int *alloc4(int v1, int v2, int v3, int v4) {
    int *p = (int *)malloc(4 * sizeof(int));
    p[0] = v1;
    p[1] = v2;
    p[2] = v3;
    p[3] = v4;
    return p;
}
EOF

assert() {
  expected="$1"
  input="$2"

  ./pugcc "$input" > tmp.s
  cc -no-pie -g -o tmp tmp.s tmp2.o
  ./tmp
  actual="$?"

  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected, but got $actual"
    exit 1
  fi
}

assert  0 "int main() { return 0; }"
assert 42 "int main() { return 42; }"
assert 21 "int main() { return 5+20-4; }"
assert 41 "int main() { return  12 + 34 - 5 ; }"
assert 47 "int main() { return 5+6*7; }"
assert 15 "int main() { return 5*(9-6); }"
assert  4 "int main() { return (3+5)/2; }"
assert 10 "int main() { return -10+20; }"
assert 10 "int main() { return - -10; }"
assert 10 "int main() { return - - +10; }"

assert  0 "int main() { return 0==1;   }"
assert  1 "int main() { return 42==42; }"
assert  1 "int main() { return 0!=1;   }"
assert  0 "int main() { return 42!=42; }"

assert  1 "int main() { return 0<1;  }"
assert  0 "int main() { return 1<1;  }"
assert  0 "int main() { return 2<1;  }"
assert  1 "int main() { return 0<=1; }"
assert  1 "int main() { return 1<=1; }"
assert  0 "int main() { return 2<=1; }"

assert  1 "int main() { return 1>0;  }"
assert  0 "int main() { return 1>1;  }"
assert  0 "int main() { return 1>1;  }"
assert  1 "int main() { return 1>=0; }"
assert  1 "int main() { return 1>=1; }"
assert  0 "int main() { return 1>=2; }"

assert  3 "int main() { int foo; foo=3; return foo; }"
assert  4 "int main() { int foo; int bar; foo=3; bar=5; return (foo+bar)/2; }"
assert  5 "int main() { return 5; return 8; }"

assert  3 "int main() { if (0) return 2; else return 3; }"
assert  3 "int main() { if (1-1) return 2; else return 3; }"
assert  2 "int main() { if (1) return 2; return 3; }"
assert  2 "int main() { if (2-1) return 2; return 3; }"

assert 10 "int main() { int i; i = 0; while (i < 10) i = i + 1; return i; }"
assert  0 "int main() { int i; i = 0; while (0) i = 1; return i; }"

assert  9 "int main() { int ret; ret = 0; int i; for (i = 0; i < 10; i = i + 1) ret = i; return ret; }"
assert  0 "int main() { int ret; ret = 0; int i; for (i = 0; i > 1;  i = i + 1) ret = i; return ret; }"
assert 10 "int main() { int ret; ret = 0;        for (;ret < 10;) ret = ret + 1; return ret; }"
assert  3 "int main() { for (;;) return 3; return 5; }"

assert  3 "int main() {1;2;return 3;}"
assert  3 "int main() {1;{2;}return 3;}"
assert 55 "int main() {int i;int j;i=0;j=0;while(i<=10){ j=i+j;i=i+1;}return j;}"

assert  3 "int main() { return ret3();}"
assert  5 "int main() { return ret5();}"

assert  3 "int main() { int x;int y;x=1;y=2;return add(x,y);}"
assert  1 "int main() { int x;int y;x=2;y=1;return sub(x,y);}"
assert 21 "int main() { return add6(1,2,3,4,5,6);}"

assert 32 "int main() { return ret32();} int ret32() { return 32;}"
assert  7 "int main() { return add2(3,4); } int add2(int x,int y) { return x+y; }"
assert  1 "int main() { return sub2(4,3); } int sub2(int x,int y) { return x-y; }"
assert 55 "int main() { return fib(9); } int fib(int x) { if (x<=1) return 1; return fib(x-1) + fib(x-2); }"

assert  3 "int main() { int x; x=3; return *&x; }"
assert  3 "int main() { int x; x=3; int y; y=&x; int z; z=&y; return **z; }"
assert  5 "int main() { int x; x=3; int y; y=5; return *(&x-16); }"
assert  3 "int main() { int x; x=3; int y; y=5; return *(&y+16); }"
assert  3 "int main() { int x; x=3; int y; y=5; int z; z=&y+16; return *z; }"

assert  3 "int main() { int x; int *y; y=&x; *y=3; return x; }"

assert  2 "int main() { int *p; p=alloc4(1, 2, 4, 8); int *q; q=p+1; return *q; }"
assert  4 "int main() { int *p; p=alloc4(1, 2, 4, 8); int *q; q=p+2; return *q; }"
assert  8 "int main() { int *p; p=alloc4(1, 2, 4, 8); int *q; q=p+3; return *q; }"
assert  8 "int main() { int *p; p=alloc4(1, 2, 4, 8); int *q; p=p+3; return *p; }"
assert  4 "int main() { int *p; p=alloc4(1, 2, 4, 8); int *q; p=p+3; q=p-1; return *q; }"
assert  2 "int main() { int *p; p=alloc4(1, 2, 4, 8); int *q; p=p+3; q=p-2; return *q; }"
assert  1 "int main() { int *p; p=alloc4(1, 2, 4, 8); int *q; p=p+3; q=p-3; return *q; }"
assert  3 "int main() { int *p; p=alloc4(1, 2, 4, 8); int *q; q=p;   p=p+3; return p-q; }"

assert  4 "int main() { int x;  return sizeof(x); }"
assert  8 "int main() { int *x; return sizeof(x); }"
assert  4 "int main() { int x;  return sizeof(x+3); }"
assert  4 "int main() { int x;  return sizeof(x-2); }"
assert  8 "int main() { int *x; return sizeof(*x); }"
assert  8 "int main() { int x;  return sizeof(&x); }"
assert  4 "int main() { return sizeof(1); }"
assert  4 "int main() { return sizeof(sizeof(1)); }"

assert 40 "int main() { int a[10]; return sizeof(a); }"
assert  3 "int main() { int a[2]; *a=1; *(a+1)=2; int *p; p=a; return *p+*(p+1); }"

assert  3 "int main() { int  x;    x=3;           return  x;     }"
assert  3 "int main() { int x[2]; *x=3;           return *x;     }"
assert  3 "int main() { int x[1]; int y[1]; *y=3; return *y;     }"
assert  3 "int main() { int x[1]; *x=3;           return *x;     }"
assert  3 "int main() { int *x;   *x=3;           return *x;     }"

assert  3 "int main() { int x[3]; *x=3;           return *x;     }"
assert  4 "int main() { int x[3]; *(x+1)=4;       return *(x+1); }"
assert  5 "int main() { int x[3]; *(x+2)=5;       return *(x+2); }"

assert  3 "int main() { int x[3]; x[0]=3;         return x[0];   }"
assert  4 "int main() { int x[3]; x[1]=4;         return x[1];   }"
assert  5 "int main() { int x[3]; x[2]=5;         return x[2];   }"

assert  0 "int x[4]; int main() { return x[0]; }"
assert  0 "int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[0]; }"
assert  1 "int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[1]; }"
assert  2 "int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[2]; }"
assert  3 "int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[3]; }"

assert  1 "int main() { char x; x=1; return x; }"
assert  1 "int main() { char x; x=1; char y; y=2; return x; }"
assert  2 "int main() { char x; x=1; char y; y=2; return y; }"
assert  1 "int main() { char x; return sizeof(x); }"
assert 40 "int main() { char x[10]; return sizeof(x); }"
assert  3 "int main() { char x[3]; x[0]=-1; x[1]=2; int y; y=4; return x[0]+y; }"
assert  1 "int main() { return subtract_char(7, 3, 3); } int subtract_char(char a, char b, char c) { return a-b-c; }"

assert 97 'int main() { return "abc"[0]; }'
assert 98 'int main() { return "abc"[1]; }'
assert 99 'int main() { return "abc"[2]; }'
assert  0 'int main() { return "abc"[3]; }'
assert  4 'int main() { return sizeof("abc"); }'

echo OK
