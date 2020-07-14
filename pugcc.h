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

// 抽象構文木のノードの種類
typedef enum {
    ND_ADD,    // +
    ND_SUB,    // -
    ND_MUL,    // *
    ND_DIV,    // /
    ND_ASSIGN, // =
    ND_LVAR,   // Local Variable
    ND_EQ,     // ==
    ND_NE,     // !=
    ND_LT,     // <
    ND_LE,     // <=
    ND_NUM,    // Integer
    ND_IF,     // if
    ND_WHILE,  // while
    ND_FOR,    // for
    ND_BLOCK,  // {...}
    ND_FUNCCALL, // function()
    ND_RETURN, // return
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
    Node *next;
    int val;       // kindがND_NUMの場合のみ使う
    int offset;    // ローカル変数のベースポインタからのオフセット; kindがND_LVARの場合のみ使う
    char *funcname; // kindがND_FUNCCALLの場合のみ使う
};

typedef struct LVar LVar;

// ローカル変数を表す型
struct LVar {
    LVar *next; // 次の変数かNULL
    char *name; // 変数の名前
    int len;    // 変数名の文字列長
    int offset; // RBPからのオフセット
};

// ローカル変数情報
LVar *locals;

// 入力プログラム
char *user_input;

// パース結果（暫定）
Node *code[100];

// パース関数の宣言
void program();

// トークナイズ関数の宣言
extern Token *tokenize();

// コード生成関数の宣言
extern void gen(Node *node);

extern void error(char *fmt, ...);
