#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <assert.h>

typedef enum {
  TK_RESERVED, // 記号
  TK_IDENT,    // 識別子
  TK_NUM,      // 整数トークン
  TK_STR,      // 文字列リテラルトークン
  TK_EOF,      // 入力の終わりを表すトークン
} TokenKind;

typedef struct Token Token;

struct Token {
  TokenKind kind; // トークンの型
  Token *next;    // 次の入力トークン
  int val;        // kindがTK_NUMの場合、その整数値
  char *str;      // トークン文字列
  int len;        // トークンの長さ
};

// 現在着目しているトークン
Token *token;

typedef struct Type Type;
typedef struct Member Member;
typedef struct GlobalVarInitializer GlobalVarInitializer;

// 型を表す型
struct Type {
  enum { VOID, BOOL, CHAR, SHORT, INT, LONG, ENUM, PTR, ARRAY, STRUCT, FUNC } kind;
  int size;
  int align;
  int array_size;
  struct Type *pointer_to;
  Member *members;
  Type *base;
  Type *return_type;
  bool is_incomplete;
};

// 構造体のメンバを表す型
struct Member {
  char *name;
  Type *type;
  int offset;
  Member *next;
  Token *token; // for error message
};

// void型の宣言
extern Type *void_type;
// bool型の宣言
extern Type *bool_type;
// char型の宣言
extern Type *char_type;
// short型の宣言
extern Type *short_type;
// int型の宣言
extern Type *int_type;
// long型の宣言
extern Type *long_type;
// enum型の宣言
extern Type *enum_type;
// 構造体型の宣言
extern Type *struct_type();
// 関数型の宣言
extern Type *func_type(Type *return_type);
// 指定型の配列型を得る関数の宣言
extern Type *array_of(Type *type, int len);
// Xのポインタ型を得る関数の宣言
extern Type *pointer_to(Type *base_type);

typedef struct Var Var;

// 変数を表す型
struct Var {
  char *name;     // 変数の名前
  int   len;      // 変数名の文字列長
  Type *type;     // 変数の型
  int   offset;   // RBPからのオフセット; ローカル変数の場合にのみ使用する
  char *contents;
  int content_len;
  bool  is_local;
  int enum_val;

  // for global variable
  bool is_static;
  GlobalVarInitializer *initializer;
};

typedef struct VarList VarList;
struct VarList {
  Var *var;
  VarList *next;
  Type *type_def;
};

typedef struct VarScope VarScope;
struct VarScope {
  char *name;
  int len;
  Var *var;
  Type *type_def;

  VarScope *next;
};

typedef struct TagScope TagScope;
struct TagScope {
  char *name;
  int len;
  Type *type;

  TagScope *next;
};

typedef struct {
  VarScope *var_scope;
  TagScope *tag_scope;
} Scope;

struct GlobalVarInitializer {
  GlobalVarInitializer *next;

  // 定数による初期化の場合に使用
  int size;
  long val;

  // 別のグローバル変数へのポインタで初期化する場合に使用
  char *another_var_name;

  long addend;
};

// 抽象構文木のノードの種類
typedef enum {
  ND_ADD,           // num + num
  ND_SUB,           // num - num
  ND_PTR_ADD,       // ptr + num | num + ptr
  ND_PTR_SUB,       // ptr - num | num - ptr
  ND_PTR_DIFF,      // ptr - ptr
  ND_MUL,           // *
  ND_DIV,           // /
  ND_ASSIGN,        // =
  ND_TERNARY,       // ?:
  ND_COMMA_OP,      // ,
  ND_PRE_INC,       // ++x
  ND_PRE_DEC,       // --x
  ND_POST_INC,      // x++
  ND_POST_DEC,      // x--
  ND_ADD_EQ,        // +=
  ND_PTR_ADD_EQ,    // +=
  ND_SUB_EQ,        // -=
  ND_PTR_SUB_EQ,    // -=
  ND_MUL_EQ,        // *=
  ND_DIV_EQ,        // /=
  ND_SHL_EQ,        // <<=
  ND_SHR_EQ,        // >>=
  ND_BITAND_EQ,     // &=
  ND_BITOR_EQ,      // |=
  ND_BITXOR_EQ,     // ^=
  ND_VAR,           // Local/Global Variable
  ND_EQ,            // ==
  ND_NE,            // !=
  ND_LT,            // <
  ND_LE,            // <=
  ND_NUM,           // Integer
  ND_CAST,          // Type cast
  ND_IF,            // if
  ND_WHILE,         // while
  ND_FOR,           // for
  ND_DO,            // do
  ND_SWITCH,        // switch
  ND_CASE,          // case
  ND_BLOCK,         // {...}
  ND_BREAK,         // break
  ND_CONTINUE,      // continue
  ND_GOTO,          // goto
  ND_LABEL,         // labled statement
  ND_FUNCCALL,      // function()
  ND_EXPR_STMT,     // expression statement
  ND_GNU_STMT_EXPR, // GNU statement expression
  ND_MEMBER,        // . (struct member access)
  ND_ADDR,          // &x
  ND_DEREF,         // *x
  ND_NOT,           // !
  ND_BITNOT,        // ~
  ND_BITAND,        // &
  ND_BITOR,         // |
  ND_BITXOR,        // ^
  ND_SHL,           // <<
  ND_SHR,           // >>
  ND_LOGAND,        // &&
  ND_LOGOR,         // ||
  ND_RETURN,        // return
  ND_NOP,           // Empty statement
} NodeKind;

typedef struct Node Node;

// 抽象構文木のノードの型
struct Node {
  Token *token;  // for error message
  NodeKind kind; // ノードの型
  Node *lhs;     // 左辺(light-hand side)
  Node *rhs;     // 右辺(right-hand side)
  Node *cond;    // if,while,for文の条件式
  Node *then;    // if,while,for文の条件式が真の場合に実行される式
  Node *els;     // if,while,for文の条件式が偽の場合に実行される式
  Node *init;
  Node *inc;
  Node *body;
  Member *member; // 構造体のメンバへのアクセス
  Node *args;
  Node *next;
  long val;      // kindがND_NUMの場合のみ使う
  Var *var;
  Node *case_next;
  Node *default_case;
  int case_label;
  int case_end_label;
  Type *type;    // 変数の型; kindがND_VARの場合のみ使う
  int offset;    // ローカル変数のベースポインタからのオフセット; kindがND_VARの場合のみ使う
  char *funcname; // kindがND_FUNCCALLの場合のみ使う
  char *label_name;
};

// ノードがポインタ型であるか判定する関数の宣言
extern bool is_pointer(Node *node);
// ノードが配列型であるか判定する関数の宣言
extern bool is_array(Node *node);
// ノードに型を付与する関数の宣言
extern void add_type(Node *Node);

typedef struct Function Function;

// 関数を表す型
struct Function {
  char     *name;
  Node     *body;
  VarList  *params;
  Function *next;
  int stack_size;
  bool is_static;
};

typedef struct Program Program;

// プログラムを表す型
struct Program {
  Function *functions;
  VarList *global_variables;
};

// 入力ファイル名
char *filename;

// 入力プログラム
char *user_input;

// パース関数の宣言
Program *program();

// トークナイズ関数の宣言
extern Token *tokenize();
extern Token *new_token(TokenKind kind, Token *cur, char *str, int len);
extern void error(char *fmt, ...);
extern void error_at(char *loc, char *fmt, ...);
extern void error_tok(Token *tok, char *fmt, ...);
extern void warn_tok(Token *tok, char *fmt, ...);
extern Token *consume(char *op);
extern Token *consume_ident();
extern Token *peek(char *s);
extern Token *consume_ident_and_return_consumed_token();
extern void expect(char *op);
extern int expect_number();
extern char *expect_ident();
extern bool peek_end();
extern bool consume_end();
extern void expect_end();
extern bool at_eof();
extern void error_at(char *loc, char *fmt, ...);

// コード生成関数の宣言
extern void codegen(Program *program);

extern char *strndup(const char *s, size_t n);
extern void error(char *fmt, ...);

// 型関連の関数の宣言
int align_to(int n, int align);
