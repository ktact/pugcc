#include "pugcc.h"

Node *new_node(NodeKind kind) {
    Node *node = calloc(1, sizeof(Node));
    node->kind = kind;
    return node;
}

Node *new_binary(NodeKind kind, Node *lhs, Node *rhs) {
    Node *node = new_node(kind);
    node->lhs  = lhs;
    node->rhs  = rhs;
    return node;
}

Node *new_unary(NodeKind kind, Node *expr) {
    Node *node = new_node(kind);
    node->lhs = expr;
    return node;
}
Node *new_num_node(int val) {
    Node *node = new_node(ND_NUM);
    node->val  = val;
    return node;
}

static Var *new_local_var(Token *token, Type *type) {
    Var *local_var = calloc(1, sizeof(Var));
    local_var->name = token->str;
    local_var->len  = token->len;
    local_var->type = type;

    int offset = 0;
    switch (type->type) {
    case INT:
        offset = 8;
        break;
    case PTR:
        offset = 8;
        break;
    case ARRAY:
        offset = 8 * type->array_size;
        break;
    }

    if (locals) {
        Var *last_var = locals;
        while (last_var->next)
            last_var = last_var->next;

        local_var->offset = last_var->offset + offset;
        last_var->next = local_var;
    } else {
        locals = local_var;
        local_var->offset = offset;
    }
    return local_var;
}

// 変数を名前で検索する。見つからなかった場合はNULLを返す。
Var *find_lvar(Token *tok) {
    for (Var *var = locals; var; var = var->next)
        if (var->len == tok->len && !memcmp(tok->str, var->name, var->len))
            return var;
    return NULL;
}

Function *funcdef();
Node *stmt();
Node *expr();
Node *assign();
Node *equality();
Node *relational();
Node *add();
Node *mul();
Node *unary();
Node *primary();
Node *funcargs();
bool consume(char *op);
Token *consume_ident();
void expect(char *op);
int expect_number();
Token *expect_ident();
bool at_eof();
void error_at(char *loc, char *fmt, ...);
// program = funcdef*
Function *program() {
    Function head = {};
    Function *cur = &head;

    while(!at_eof()) {
        cur->next = funcdef();
        cur = cur->next;
    }

    return head.next;
}

static Type *var_type() {
    expect("int");
    Type *type = int_type;
    while (consume("*"))
        type = pointer_to(type);
    return type;
}

static Var *read_func_params() {
    if (consume(")"))
        return NULL;

    Type *type = var_type();

    Var *head = new_local_var(expect_ident(), type);
    Var *cur  = head;
    while (consume(",")) {
        type = var_type();

        cur->next = new_local_var(expect_ident(), type);
        cur = cur->next;
    }
    expect(")");

    return head;
}

// funcdef = "int" ident "(" params? ")" "{" stmt* "}"
Function *funcdef() {
    expect("int");

    Token *tok = consume_ident();
    if (tok) {
        Function *f = calloc(1, sizeof(Function));
        f->name = strndup(tok->str, tok->len);
        locals = NULL;

        expect("(");
        f->params = read_func_params();
        expect("{");

        Node head = {};
        Node *cur = &head;

        while (!consume("}")) {
            cur->next = stmt();
            cur = cur->next;
        }

        f->body = head.next;

        // 関数のスタックサイズ=(関数の引数の個数+関数内のローカル変数の個数)*8
        for (Var *var = locals; var; var = var->next)
            f->stack_size += 8;
 
        return f;
    }

    return NULL;
}

// stmt = "return" expr ";"
//      | "if" "(" expr ")" stmt ("else" stmt)?
//      | "while" "(" expr ")" stmt
//      | "for" "(" expr? ";" expr? ";" expr? ")" stmt
//      | "{" stmt* "}"
//      | "int" ident ("[" num "]")?";"
//      | expr ";"
Node *stmt() {
    if (consume("return")) {
        Node *node = new_unary(ND_RETURN, expr());
        add_type(node);
        expect(";");
        return node;
    }

    if (consume("if")) {
        Node *node = new_node(ND_IF);
        expect("(");
        node->cond = expr();
        expect(")");
        node->then = stmt();
        if (consume("else"))
            node->els = stmt();
        add_type(node);
        return node;
    }

    if (consume("while")) {
        Node *node = new_node(ND_WHILE);
        expect("(");
        node->cond = expr();
        expect(")");
        node->then = stmt();
        add_type(node);
        return node;
    }

    if (consume("for")) {
        Node *node = new_node(ND_FOR);
        expect("(");
        if (!consume(";")) {
            node->init = expr();
            expect(";");
        }
        if (!consume(";")) {
            node->cond = expr();
            expect(";");
        }
        if (!consume(")")) {
            node->inc = expr();
            expect(")");
        }
        node->then = stmt();
        add_type(node);
        return node;
    }

    if (consume("{")) {
        Node head = {};
        Node *cur = &head;

        while (!consume("}")) {
            cur->next = stmt();
            cur = cur->next;
        }

        Node *node = new_node(ND_BLOCK);
        node->body = head.next;
        add_type(node);
        return node;
    }

    if (consume("int")) {
        Type *type = int_type;
        while (consume("*"))
            type = pointer_to(type);

        Token *tok = expect_ident();
        if (consume("[")) {
            type->type = ARRAY;
            type->array_size = expect_number();
            expect("]");
        }
        Var *var = new_local_var(tok, type);
        expect(";");
        return new_node(ND_NOP);
    }
    Node *node = expr();
    expect(";");
    return node;
}

// expr = assign
Node *expr() {
    return assign();
}

// assign = equality ("=" assign)?
Node *assign() {
    Node *node = equality();
    if (consume("="))
        node = new_binary(ND_ASSIGN, node, assign());
    return node;
}

// equality = relational ("==" relational | "!=" relational)*
Node *equality() {
    Node *node = relational();

    for (;;) {
        if (consume("=="))
            node = new_binary(ND_EQ, node, relational());
        else if (consume("!="))
            node = new_binary(ND_NE, node, relational());
        else
            return node;
    }
}

// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
Node *relational() {
    Node *node = add();

    for (;;) {
        if (consume("<"))
            node = new_binary(ND_LT, node, add());
        else if (consume("<="))
            node = new_binary(ND_LE, node, add());
        else if (consume(">"))
            node = new_binary(ND_LT, add(), node);
        else if (consume(">="))
            node = new_binary(ND_LE, add(), node);
        else
            return node;
    }
}

// add = mul ("+" mul | "-" mul)*
Node *add() {
    Node *node = mul();

    for (;;) {
        if (consume("+")) {
            Node *lhd = node;
            Node *rhd = mul();
            if (is_pointer(lhd) || is_pointer(rhd)) {
                node = new_binary(ND_PTR_ADD, lhd, rhd);
            } else {
                node = new_binary(ND_ADD,     lhd, rhd);
            }
        } else if (consume("-")) {
            Node *lhd = node;
            Node *rhd = mul();
            if (is_pointer(lhd) && is_pointer(rhd)) {
                node = new_binary(ND_PTR_DIFF, lhd, rhd);
            } else if (is_pointer(lhd) || is_pointer(rhd)) {
                node = new_binary(ND_PTR_SUB,  lhd, rhd);
            } else {
                node = new_binary(ND_SUB,      lhd, rhd);
            }
        } else {
            return node;
        }
    }
}

// mul = unary ("*" unary | "/" unary)*
Node *mul() {
    Node *node = unary();

    for (;;) {
        if (consume("*"))
            node = new_binary(ND_MUL, node, unary());
        else if (consume("/"))
            node = new_binary(ND_DIV, node, unary());
        else
            return node;
    }
}

// unary = "sizeof" unary | ("+" | "-" | "*" | "&")? unary | primary
Node *unary() {
    if (consume("sizeof")) {
        Node *node = unary();
        add_type(node);
        switch (node->type->type) {
        case INT:
            return new_num_node(4);
        case PTR:
            return new_num_node(8);
        case ARRAY:
            return new_num_node(4 * node->type->array_size);
        }
    }

    if (consume("+"))
        return unary();
    if (consume("-"))
        return new_binary(ND_SUB, new_num_node(0), unary());
    if (consume("*"))
        return new_unary(ND_DEREF, unary());
    if (consume("&"))
        return new_unary(ND_ADDR, unary());
    return primary();
}

// primary = num | ident funcargs? | "(" expr ")"
Node *primary() {
    // 次のトークンが"("なら、"(" expr ")"のはず
    if (consume("(")) {
        Node *node = expr();
        expect(")");
        return node;
    }

    Token *tok = consume_ident();
    if (tok) {
        if (consume("(")) {
            // 関数呼び出し
            Node *node     = new_node(ND_FUNCCALL);
            node->funcname = strndup(tok->str, tok->len);
            node->args     = funcargs();
            return node;
        } else {
            // 変数
            Node *node = new_node(ND_LVAR);

            Var *lvar = find_lvar(tok);
            if (lvar) {
                node->type   = lvar->type;
                node->offset = lvar->offset;
            } else {
                error_at(tok->str, "未定義の変数を参照しています");
            }
            return node;
        }
    }

    // そうでなければ数値のはず
    return new_num_node(expect_number());
}

// funcargs = "(" (assign ("," assign)*)? ")"
Node *funcargs() {
    if (consume(")"))
        return NULL;

    Node *head = assign();
    Node *cur  = head;
    while (consume(",")) {
        cur->next = assign();
        cur = cur->next;
    }
    expect(")");

    return head;
}

// 新しいトークンを作成してcurに繋げる
Token *new_token(TokenKind kind, Token *cur, char *str, int len) {
    Token *tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->str  = str;
    tok->len  = len;
    cur->next = tok;
    return tok;
}

// エラーを報告するための関数
// printfと同じ引数を取る
void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

// エラー箇所を報告する
void error_at(char *loc, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    int pos = loc - user_input;
    fprintf(stderr, "%s\n", user_input);
    fprintf(stderr, "%*s", pos, ""); // pos個の空白を出力
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

// 次のトークンが期待している記号の場合にはトークンを1つ読み進めて真を返す。
// それ以外の場合には偽を返す。
bool consume(char *op) {
    if (token->kind != TK_RESERVED || strlen(op) != token->len ||
        memcmp(token->str, op, token->len))
        return false;
    token = token->next;
    return true;
}

// 次のトークンが識別子であればトークンを1つ読み進める。
// それ以外の場合にはNULLを返す。
Token *consume_ident() {
    if (token->kind != TK_IDENT)
        return NULL;
    Token *t = token;
    token = token->next;
    return t;
}

// 次のトークンが期待している記号の場合にはトークンを1つ読み進める。
// それ以外の場合にはエラーを報告する。
void expect(char *op) {
    if (token->kind != TK_RESERVED || strlen(op) != token->len ||
        memcmp(token->str, op, token->len))
        error_at(token->str, "'%s'ではありません", op);
    token = token->next;
}

// 次のトークンが数値の場合、トークンを1つ読み進めてその数値を返す。
// それ以外の場合にはエラーを報告する。
int expect_number() {
    if (token->kind != TK_NUM)
        error_at(token->str, "数ではありません");
    int val = token->val;
    token = token->next;
    return val;
}

// 次のトークンが識別子の場合、トークンを1つ読み進めてそのトークンを返す。
// それ以外の場合にはエラーを報告する。
Token *expect_ident() {
    if (token->kind != TK_IDENT)
        error_at(token->str, "識別子ではありません");
    Token *t = token;
    token = token->next;
    return t;
}

bool at_eof() {
    return token->kind == TK_EOF;
}

bool startswith(char *p, char *q) {
    return memcmp(p, q, strlen(q)) == 0;
}

static bool is_alpha(char c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

static bool is_alnum(char c) {
    return is_alpha(c) || ('0' <= c && c <= '9');
}

static int reserved_word(char *p) {
    char *keywords[] = { "return", "if", "else", "while", "for", "int", "sizeof" };
    for (int i = 0; i < sizeof(keywords) / sizeof(*keywords); i++) {
        int len = strlen(keywords[i]);
        if (startswith(p, keywords[i]) && !is_alnum(p[len]))
            return len;
    }
    return 0;
}

// 入力文字列pをトークナイズしてそれを返す
Token *tokenize() {
    char *p = user_input;
    Token head;
    head.next = NULL;
    Token *cur = &head;

    while (*p) {
        // 空白文字をスキップ
        if (isspace(*p)) {
            p++;
            continue;
        }

        int len = 0;
        if ((len = reserved_word(p)) > 0) {
            cur = new_token(TK_RESERVED, cur, p, len);
            p += len;
            continue;
        }

        // 識別子
        if (is_alpha(*p)) {
            char *q = p++;
            while (is_alnum(*p))
                p++;
            cur = new_token(TK_IDENT, cur, q, p - q);
            continue;
        }

        if (startswith(p, "==") || startswith(p, "!=") ||
            startswith(p, "<=") || startswith(p, ">=")) {
            cur = new_token(TK_RESERVED, cur, p, 2);
            p += 2;
            continue;
        }

        if (strchr("+-*/&(){}<>=;,[]", *p)) {
            cur = new_token(TK_RESERVED, cur, p++, 1);
            continue;
        }

        if (isdigit(*p)) {
            cur = new_token(TK_NUM, cur, p, 0);
            char *q = p;
            cur->val = strtol(p, &p, 10);
            cur->len = p - q;
            continue;
        }

        error_at(p, "不正なトークンです");
    }

    new_token(TK_EOF, cur, p, 0);
    return head.next;
}
