#!/bin/bash -x
TMP=tmp-self

mkdir -p $TMP

expand() {
  file=$1

  # ライブラリ関数宣言挿入
  tee $TMP/$1 <<EOF > /dev/null
typedef struct FILE FILE;
extern FILE *stdout;
extern FILE *stderr;
void *malloc(long size);
void *calloc(long nmemb, long size);
int *__errno_location();
char *strerror(int errnum);
FILE *fopen(char *pathname, char *mode);
long fread(void *ptr, long size, long nmemb, FILE *stream);
int feof(FILE *stream);
static void assert() {}
int strcmp(char *s1, char *s2);
typedef int size_t;
EOF

  # ヘッダファイル挿入
  grep -v '^#' pugcc.h >> $TMP/$1

  # コンパイル対象のソースファイル本文挿入
  grep -v '^#' $1 >> $TMP/$1

  sed -i 's/\bbool\b/_Bool/g' $TMP/$1
  sed -i 's/\berrno\b/*__errno_location()/g' $TMP/$1
  sed -i 's/\btrue\b/1/g; s/\bfalse\b/0/g;' $TMP/$1
  sed -i 's/\bNULL\b/0/g' $TMP/$1
  sed -i 's/, \.\.\.//g' $TMP/$1

  # 狭義のコンパイル実行
  ./pugcc $TMP/$1 > $TMP/${1%.c}.s
  # オブジェクトファイル生成
  gcc -c -o $TMP/${1%.c}.o $TMP/${1%.c}.s
}

cp *.c $TMP
for i in $TMP/*.c; do
  gcc -I. -c -o ${i%.c}.o $i
done

expand main.c
expand type.c

gcc -static -o pugcc-gen2 $TMP/*.o
