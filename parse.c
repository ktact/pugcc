#include "pugcc.h"

static Var *locals;
static Var *globals;

Node *new_node(NodeKind kind) {
    Node *node = calloc(1, sizeof(Node));
    node->kind = kind;
    return node;
}

Node *new_binary(NodeKind kind, Node *lhs, Node *rhs) {
    Node *node = new_node(kind);
    node->lhs  = lhs;
    node->rhs  = rhs;
    add_type(node);
    return node;
}

Node *new_unary(NodeKind kind, Node *expr) {
    Node *node = new_node(kind);
    node->lhs = expr;
    add_type(node);
    return node;
}

Node *new_num_node(int val) {
    Node *node = new_node(ND_NUM);
    node->val  = val;
    add_type(node);
    return node;
}

static Var *new_var(char *name, Type *type, bool is_local) {
    Var *var = calloc(1, sizeof(Var));
    var->name     = name;
    var->len      = strlen(name);
    var->type     = type;
    var->is_local = is_local;

    return var;
}

static Var *new_local_var(char *name, Type *type) {
    Var *var = new_var(name, type, true);

    int offset = type->size + (type->size % 16);

    if (locals) {
        Var *last_var = locals;
        while (last_var->next)
            last_var = last_var->next;

        var->offset = last_var->offset + offset;
        last_var->next = var;
    } else {
        locals = var;
        var->offset = offset;
    }

   return var;
}

static Var *new_global_var(char *name, Type *type) {
    Var *var = new_var(name, type, false);

    if (globals) {
        Var *last_var = globals;
        while (last_var->next)
            last_var = last_var->next;

        last_var->next = var;
    } else {
        globals = var;
    }

    return var;
}

// 変数を名前で検索する。見つからなかった場合はNULLを返す。
static Var *find_var(char *name) {
    for (Var *var = locals; var; var = var->next)
        if (var->len == strlen(name) && !memcmp(name, var->name, var->len))
            return var;
    for (Var *var = globals; var; var = var->next)
        if (var->len == strlen(name) && !memcmp(name, var->name, var->len))
            return var;
    return NULL;
}

static Node *new_var_node(Var *var) {
    Node *node = new_node(ND_VAR);
    node->var = var;
    add_type(node);
    return node;
}

static char *new_label() {
    static int cnt = 0;
    char buf[20];
    sprintf(buf, ".L.data.%d", cnt++);
    return strndup(buf, 20);
}

Function *func_decl();
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
char *consume_ident();
void expect(char *op);
int expect_number();
char *expect_ident();
Type *expect_type();
bool at_eof();
void error_at(char *loc, char *fmt, ...);

// type = ("int" | "char") "*"?
static Type *read_type() {
    Type *type = NULL;
    if (consume("int")) {
        type = int_type;
    } else if (consume("char")) {
        type = char_type;
    } else {
        error_at(token->str, "型ではありません");
    }

    while (consume("*"))
        type = pointer_to(type);
    return type;
}

static bool is_function() {
    // 本関数呼び出し時点でのトークンの読み出し位置を覚えておく
    Token *tok = token;

    // トークンを先読みして型、識別子、（であれば関数宣言であると判断する
    read_type();
    bool is_function = consume_ident() && consume("(");

    // トークンの読み出し位置を元に戻す
    token = tok;

    return is_function;
}

static void read_global_var_decl() {
    Type *type = read_type();
    char *var_name = consume_ident();
    if (consume("[")) {
        int array_size = expect_number();
        type = array_of(type, array_size);
        expect("]");
    }
    expect(";");
    Var *var = new_global_var(var_name, type);
}

// program = ((type func_decl) | (type ident ("[" num "]")?))*
Program *program() {
    Function head = {};
    Function *cur = &head;
    globals = NULL;

    while(!at_eof()) {
        if (is_function()) {
            cur->next = func_decl();
            cur = cur->next;
        } else {
            read_global_var_decl();
        }
    }

    Program *program = calloc(1, sizeof(Program));
    program->functions = head.next;
    program->global_variables = globals;

    return program;
}

static Var *read_func_params() {
    if (consume(")"))
        return NULL;

    Type *type = read_type();

    Var *head = new_local_var(expect_ident(), type);
    Var *cur  = head;
    while (consume(",")) {
        type = read_type();

        cur->next = new_local_var(expect_ident(), type);
        cur = cur->next;
    }
    expect(")");

    return head;
}

// func_decl = type ident "(" params? ")" "{" stmt* "}"
Function *func_decl() {
    Type *type = read_type();
    char *func_name = consume_ident();
    if (func_name) {
        Function *f = calloc(1, sizeof(Function));
        f->name = func_name;
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

        int stack_size = 0;
        for (Var *var = locals; var; var = var->next)
            stack_size = var->offset;

        f->stack_size = stack_size;

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

    Type *type = NULL;
    if (consume("int")) {
        type = int_type;
    } else if (consume("char")) {
        type = char_type;
    } else {
        Node *node = expr();
        expect(";");
        return node;
   }

    while (consume("*"))
        type = pointer_to(type);

    char *var_name = expect_ident();
    if (consume("[")) {
        int array_size = expect_number();
        type = array_of(type, array_size);
        expect("]");
    }
    Var *var = new_local_var(var_name, type);
    expect(";");
    return new_node(ND_NOP);
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
            if (is_pointer(lhd) || is_pointer(rhd) || is_array(lhd) || is_array(rhd)) {
                node = new_binary(ND_PTR_ADD, lhd, rhd);
            } else {
                node = new_binary(ND_ADD,     lhd, rhd);
            }
        } else if (consume("-")) {
            Node *lhd = node;
            Node *rhd = mul();
            if (is_pointer(lhd) && is_pointer(rhd) || is_array(lhd) || is_array(rhd)) {
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
        switch (node->type->kind) {
        case INT:
            return new_num_node(4);
        case CHAR:
            return new_num_node(1);
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

// primary = num | str | ident funcargs? | ident ("[" num "]")? | "(" expr ")"
Node *primary() {
    // 次のトークンが"("なら、"(" expr ")"のはず
    if (consume("(")) {
        Node *node = expr();
        expect(")");
        return node;
    }

    char *ident = consume_ident();
    if (ident) {
        if (consume("(")) {
            // 関数呼び出し
            Node *node     = new_node(ND_FUNCCALL);
            node->funcname = ident;
            node->args     = funcargs();
            add_type(node);
            return node;
        } else if (consume("[")) {
            // 配列参照

            /*
             * x[n]を*(x+n)に読み替える
             * 例) a[3]を*(a+3)に読み替える
             */
            Var *array = find_var(ident);
            if (!array) {
                error_at(ident, "未定義の変数を参照しています");
            }

            Node *ptr_to_array   = new_node(ND_VAR);
            ptr_to_array->type   = array->type;
            ptr_to_array->offset = array->offset;
            ptr_to_array->var    = array;
            add_type(ptr_to_array);

            Node *index = new_num_node(expect_number());

            expect("]");

            return new_unary(ND_DEREF, new_binary(ND_PTR_ADD, ptr_to_array, index));
        } else {
            // 変数参照
            Node *node = new_node(ND_VAR);

            Var *var = find_var(ident);
            if (var) {
                node->type   = var->type;
                node->offset = var->offset;
                node->var    = var;
            } else {
                error_at(ident, "未定義の変数を参照しています");
            }
            add_type(node);
            return node;
        }
    }

    if (token->kind == TK_STR) {
        Token *literal = token;
        token = token->next;

        Type *type = array_of(char_type, token->len);
        Var *var = new_global_var(new_label(), type);
        var->contents    = literal->str;
        var->content_len = literal->len;

        if (consume("[")) {
            // 配列参照
            Node *ptr_to_array   = new_node(ND_VAR);
            ptr_to_array->type   = var->type;
            ptr_to_array->offset = var->offset;
            ptr_to_array->var    = var;
            add_type(ptr_to_array);

            Node *index = new_num_node(expect_number());

            expect("]");

            return new_unary(ND_DEREF, new_binary(ND_PTR_ADD, ptr_to_array, index));
        }

        return new_var_node(var);
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
    tok->str  = strndup(str, len);
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

    // locが含まれている行の開始地点と終了地点を取得
    char *line = loc;
    while (user_input < line && line[-1] != '\n')
        line--;

    char *end = loc;
    while (*end != '\n')
        end++;

    // 見つかった行が全体の何行目なのか調べる
    int line_num = 1;
    for (char *p = user_input; p < line; p++)
        if (*p == '\n')
            line_num++;

    // 見つかった行を、ファイル名と行番号と一緒に表示
    int indent = fprintf(stderr, "%s:%d: ", filename, line_num);
    fprintf(stderr, "%*.s\n", (int)(end - line), line);

    // エラー箇所を"^"で指し示して、エラーメッセージを表示
    int pos = loc - line + indent;
    fprintf(stderr, "%*s", pos, "");
    fprintf(stderr, "^ ");
    fprintf(stderr, fmt, ap);
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

// 次のトークンが識別子であればトークンを1つ読み進めそのトークンの文字列を返す。
// それ以外の場合にはNULLを返す。
char *consume_ident() {
    if (token->kind != TK_IDENT)
        return NULL;
    Token *t = token;
    token = token->next;
    return strndup(t->str, t->len);
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

// 次のトークンが識別子の場合、トークンを1つ読み進めてそのトークンの文字列をを返す。
// それ以外の場合にはエラーを報告する。
char *expect_ident() {
    if (token->kind != TK_IDENT)
        error_at(token->str, "識別子ではありません");
    Token *t = token;
    token = token->next;
    return strndup(t->str, t->len);
}

// 次のトークンが型の場合、トークンを1つ読み進めてその型を返す。
// それ以外の場合にはエラーを報告する。
Type *expect_type() {
    if (token->kind != TK_RESERVED ||
        (memcmp(token->str, "int",  token->len) && memcmp(token->str, "char", token->len)))
        error_at(token->str, "型ではありません");

    Token *t = token;
    token = token->next;
    if (memcmp(t->str, "int", t->len)) {
        return int_type;
    } else if (memcmp(t->str, "char", t->len)) {
        return char_type;
    }
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
    char *keywords[] = { "return", "if", "else", "while", "for", "int", "char", "sizeof" };
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

        // 行コメントをスキップ
        if (strncmp(p, "//", 2) == 0) {
            p += 2;
            while (*p != '\n')
                p++;
            continue;
        }

        // ブロックコメントをスキップ
        if (strncmp(p, "/*", 2) == 0) {
            char *q = strstr(p + 2, "*/");
            if (!q)
                error_at(p, "コメントが閉じられていません");
            p = q + 2;
            continue;
        }

        // 文字列リテラル
        if (*p == '"') {
            char *q = p++;
            while (*p && *p != '"')
                p++;
            if (!*p)
                error_at(q,"文字列リテラルの終端がありません");

            p++; // 読み出し位置を、文字列リテラルの終端の"の次の文字にセットする

            cur = new_token(TK_STR, cur, q + 1, p - q - 2);
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
