#!/bin/bash
cat <<EOF | gcc -xc -c -o tmp2.o -
int ret3() { return 3; }
int ret5() { return 5; }
EOF

assert() {
  expected="$1"
  input="$2"

  ./pugcc "$input" > tmp.s
  cc -o tmp tmp.s tmp2.o
  ./tmp
  actual="$?"

  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected, but got $actual"
    exit 1
  fi
}

assert 0 "return 0;" 
assert 42 "return 42;"
assert 21 "return 5+20-4;"
assert 41 "return  12 + 34 - 5 ;"
assert 47 "return 5+6*7;"
assert 15 "return 5*(9-6);"
assert 4 "return (3+5)/2;"
assert 10 "return -10+20;"
assert 10 "return - -10;"
assert 10 "return - - +10;"

assert 0 "return 0==1;"
assert 1 "return 42==42;"
assert 1 "return 0!=1;"
assert 0 "return 42!=42;"

assert 1 "return 0<1;"
assert 0 "return 1<1;"
assert 0 "return 2<1;"
assert 1 "return 0<=1;"
assert 1 "return 1<=1;"
assert 0 "return 2<=1;"

assert 1 "return 1>0;"
assert 0 "return 1>1;"
assert 0 "return 1>1;"
assert 1 "return 1>=0;"
assert 1 "return 1>=1;"
assert 0 "return 1>=2;"

assert 3 "foo=3; return foo;"
assert 4 "foo=3; bar=5; return (foo+bar)/2;"
assert 5 "return 5; return 8;"

assert 3 "if (0) return 2; else return 3;"
assert 3 "if (1-1) return 2; else return 3;"
assert 2 "if (1) return 2; return 3;"
assert 2 "if (2-1) return 2; return 3;"

assert 10 "i = 0; while (i < 10) i = i + 1; return i;"
assert 0  "i = 0; while (0) i = 1; return i;"

assert 9  "ret = 0; for (i = 0; i < 10; i = i + 1) ret = i; return ret;"
assert 0  "ret = 0; for (i = 0; i > 1;  i = i + 1) ret = i; return ret;"
assert 10 "ret = 0; for (;ret < 10;) ret = ret + 1; return ret;"
assert 3  "for (;;) return 3; return 5;"

assert 3 "{1;2;return 3;}"
assert 3 "{1;{2;}return 3;}"
assert 55 "i=0;j=0;while(i<=10){ j=i+j;i=i+1;}return j;"

assert 3 "return ret3();"
assert 5 "return ret5();"
echo OK
