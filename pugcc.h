#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TK_RESERVED, // 記号
    TK_IDENT,    // 識別子
    TK_NUM,      // 整数トークン
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

// 型を表す型
struct Type {
    enum { INT, CHAR, PTR, ARRAY } kind;
    int size;
    int array_size;
    struct Type *pointer_to;
};

// int型の宣言
extern Type *int_type;
// char型の宣言
extern Type *char_type;
// intの配列型を得る関数の宣言
extern Type *array_of_int(int len);
// Xのポインタ型を得る関数の宣言
extern Type *pointer_to(Type *base_type);

typedef struct Var Var;

// 変数を表す型
struct Var {
    Var  *next;     // 次の変数かNULL
    char *name;     // 変数の名前
    int   len;      // 変数名の文字列長
    Type *type;     // 変数の型
    int   offset;   // RBPからのオフセット; ローカル変数の場合にのみ使用する
    bool  is_local;
};

// 抽象構文木のノードの種類
typedef enum {
    ND_ADD,      // num + num
    ND_SUB,      // num - num
    ND_PTR_ADD,  // ptr + num | num + ptr
    ND_PTR_SUB,  // ptr - num | num - ptr
    ND_PTR_DIFF, // ptr - ptr
    ND_MUL,      // *
    ND_DIV,      // /
    ND_ASSIGN,   // =
    ND_VAR,      // Local/Global Variable
    ND_EQ,       // ==
    ND_NE,       // !=
    ND_LT,       // <
    ND_LE,       // <=
    ND_NUM,      // Integer
    ND_IF,       // if
    ND_WHILE,    // while
    ND_FOR,      // for
    ND_BLOCK,    // {...}
    ND_FUNCCALL, // function()
    ND_ADDR,     // &x
    ND_DEREF,    // *x
    ND_RETURN,   // return
    ND_NOP,      // Empty statement
} NodeKind;

typedef struct Node Node;

// 抽象構文木のノードの型
struct Node {
    NodeKind kind; // ノードの型
    Node *lhs;     // 左辺(light-hand side)
    Node *rhs;     // 右辺(right-hand side)
    Node *cond;    // if,while,for文の条件式
    Node *then;    // if,while,for文の条件式が真の場合に実行される式
    Node *els;     // if,while,for文の条件式が偽の場合に実行される式
    Node *init;
    Node *inc;
    Node *body;
    Node *args;
    Node *next;
    int val;       // kindがND_NUMの場合のみ使う
    Var *var;
    Type *type;    // 変数の型; kindがND_VARの場合のみ使う
    int offset;    // ローカル変数のベースポインタからのオフセット; kindがND_VARの場合のみ使う
    char *funcname; // kindがND_FUNCCALLの場合のみ使う
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
    Var      *params;
    Function *next;
    int stack_size;
};

typedef struct Program Program;

// プログラムを表す型
struct Program {
    Function *functions;
    Var *global_variables;
};

// 入力プログラム
char *user_input;

// パース関数の宣言
Program *program();

// トークナイズ関数の宣言
extern Token *tokenize();

// コード生成関数の宣言
extern void codegen(Program *program);

extern char *strndup(const char *s, size_t n);
extern void error(char *fmt, ...);
