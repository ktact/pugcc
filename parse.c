#include "pugcc.h"

static VarList *locals  = NULL;
static VarList *globals = NULL;

static VarList *var_scope = NULL;
static TagList *tag_scope = NULL;

static Scope *enter_scope(void) {
    Scope *sc = calloc(1, sizeof(Scope));
    sc->var_scope = var_scope;
    sc->tag_scope = tag_scope;
    return sc;
}

static void leave_scope(Scope *sc) {
    var_scope = sc->var_scope;
    tag_scope = sc->tag_scope;
}

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

static Var *new_var(char *name, Type *type, bool is_local) {
    Var *var = calloc(1, sizeof(Var));
    var->name     = name;
    var->len      = strlen(name);
    var->type     = type;
    var->is_local = is_local;

    VarList *sc = calloc(1, sizeof(VarList));
    sc->var = var;
    sc->next = var_scope;
    var_scope = sc;

    return var;
}

static Var *new_local_var(char *name, Type *type) {
    Var *var = new_var(name, type, true);

    VarList *vl = calloc(1, sizeof(VarList));
    vl->var = var;
    vl->next = locals;
    var->offset = type->size;
    if (locals)
        var->offset += locals->var->offset;
    locals = vl;
    return var;
}

static Var *new_global_var(char *name, Type *type) {
    Var *var = new_var(name, type, false);

    VarList *vl = calloc(1, sizeof(VarList));
    vl->var = var;
    vl->next = globals;
    globals = vl;

    return var;
}

// 変数を名前で検索する。見つからなかった場合はNULLを返す。
static Var *find_var(char *name) {
    for (VarList *vl = var_scope; vl; vl = vl->next) {
        Var *var = vl->var;
        if (var->len == strlen(name) && !memcmp(name, var->name, var->len))
            return var;
    }
    return NULL;
}

static TagList *find_tag_by(char *name) {
    for (TagList *tag = tag_scope; tag; tag = tag->next)
        if (strlen(tag->name) == strlen(name) && !memcmp(name, tag->name, strlen(tag->name)))
            return tag;
    return NULL;
}

static Node *new_var_node(Var *var) {
    Node *node = new_node(ND_VAR);
    node->var = var;
    return node;
}

static char *new_label() {
    static int cnt = 0;
    char buf[20];
    sprintf(buf, ".L.data.%d", cnt++);
    return strndup(buf, 20);
}

static Type *struct_decl();
static Member *struct_member();
Function *func_decl();
Node *stmt();
Node *stmt2();
Node *expr();
Node *assign();
Node *equality();
Node *relational();
Node *add();
Node *mul();
Node *unary();
Node *gnu_stmt_expr();
Node *postfix();
Node *primary();
Node *funcargs();
bool consume(char *op);
Token *peek(char *s);
char *consume_ident();
void expect(char *op);
int expect_number();
char *expect_ident();
bool at_eof();
void error_at(char *loc, char *fmt, ...);

// basetype = ("char" | "int" | struct_decl) "*"*
static Type *basetype() {
    Type *type = NULL;
    if (consume("int")) {
        type = int_type;
    } else if (consume("char")) {
        type = char_type;
    } else if (peek("struct")) {
        type = struct_decl();
    } else {
        error_at(token->str, "型ではありません");
    }

    while (consume("*"))
        type = pointer_to(type);

    return type;
}

// type_suffix = ("[" num "]")*
static Type *type_suffix(Type *base) {
    if (!consume("["))
        return base;
    int array_size = expect_number();
    expect("]");

    base = type_suffix(base);
    return array_of(base, array_size);
}

static void push_tag_to_scope(char *name, Type *type) {
    TagList *tag = calloc(1, sizeof(TagList));
    tag->next = tag_scope;
    tag->name = strndup(name, strlen(name));
    tag->type = type;
    tag_scope = tag;
}

// struct_decl = "struct" ident
//             | "struct" ident? "{" struct_member "}"
static Type *struct_decl() {
    expect("struct");

    // 構造体名を読む
    char *tag_name = consume_ident();
    if (tag_name && !peek("{")) {
        TagList *tag = find_tag_by(tag_name);
        return tag->type;
    }

    expect("{");

    Member head = {};
    Member *cur = &head;

    while (!consume("}")) {
        cur->next = struct_member();
        cur = cur->next;
    }

    Type *type = calloc(1, sizeof(Type));
    type->kind = STRUCT;
    type->members = head.next;

    int offset = 0;
    for (Member *member = type->members; member; member = member->next) {
        offset = align_to(offset, member->type->align);
        member->offset = offset;
        offset += member->type->size;

        if (type->align < member->type->align)
            type->align = member->type->align;
    }
    type->size = align_to(offset, type->align);

    if (tag_name)
        push_tag_to_scope(tag_name, type);

    return type;
}

// struct_member = basetype ident ("[" num "]")* ";"
static Member *struct_member() {
    Member *member = calloc(1, sizeof(Member));
    member->type = basetype();
    member->name = expect_ident();
    member->type = type_suffix(member->type);
    expect(";");

    return member;
}

static Member *find_member(Type *type, char *name) {
    for (Member *member = type->members; member; member = member->next)
        if (!strcmp(member->name, name)) {
            return member;
        }
    return NULL;
}

static Node *struct_ref(Node *lhs) {
    add_type(lhs);

    Member *member = find_member(lhs->type, expect_ident());

    Node *node = new_unary(ND_MEMBER, lhs);
    node->member = member;

    return node;
}

static bool is_type() {
    return (peek("int") || peek("char") || peek("struct"));
}

static bool is_function() {
    // 本関数呼び出し時点でのトークンの読み出し位置を覚えておく
    Token *tok = token;

    // トークンを先読みして型、識別子、（であれば関数宣言であると判断する
    basetype();
    bool is_function = consume_ident() && consume("(");

    // トークンの読み出し位置を元に戻す
    token = tok;

    return is_function;
}

static void read_global_var_decl() {
    Type *base = basetype();
    char *var_name = consume_ident();
    Type *type = type_suffix(base);
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

static VarList *read_func_param() {
    Type *type = basetype();
    char *name = expect_ident();

    VarList *vl = calloc(1, sizeof(VarList));
    vl->var = new_local_var(name, type);

    return vl;
}

static VarList *read_func_params() {
    if (consume(")"))
        return NULL;

    VarList *head = read_func_param();
    VarList *cur = head;

    while (!consume(")")) {
        expect(",");
        cur->next = read_func_param();
        cur = cur->next;
    }

    return head;
}

// func_decl = type ident "(" params? ")" "{" stmt* "}"
Function *func_decl() {
    Type *type = basetype();
    char *func_name = consume_ident();
    if (func_name) {
        Function *f = calloc(1, sizeof(Function));
        f->name = func_name;
        locals = NULL;

        expect("(");

        Scope *sc = enter_scope();
        f->params = read_func_params();
        expect("{");

        Node head = {};
        Node *cur = &head;

        while (!consume("}")) {
            cur->next = stmt();
            cur = cur->next;
        }
        leave_scope(sc);

        f->body = head.next;

        int offset = 0;
        for (VarList *vl = locals; vl; vl = vl->next) {
            Var *var = vl->var;
            offset = align_to(offset, var->type->align);
            offset += var->type->size;
            var->offset = offset;
        }

        f->stack_size = align_to(offset, 8);

        return f;
    }

    return NULL;
}

Node *stmt() {
    Node *node = stmt2();
    add_type(node);
    return node;
}

// stmt2 = "return" expr ";"
//       | "if" "(" expr ")" stmt ("else" stmt)?
//       | "while" "(" expr ")" stmt
//       | "for" "(" expr? ";" expr? ";" expr? ")" stmt
//       | "{" stmt* "}"
//       | basetype ident ("[" num "]")* ("=" expr) ";"
//       | expr ";"
Node *stmt2() {
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
            node->init = new_unary(ND_EXPR_STMT, expr());
            expect(";");
        }
        if (!consume(";")) {
            node->cond = expr();
            expect(";");
        }
        if (!consume(")")) {
            node->inc = new_unary(ND_EXPR_STMT, expr());
            expect(")");
        }
        node->then = stmt();
        add_type(node);
        return node;
    }

    if (consume("{")) {
        Node head = {};
        Node *cur = &head;

        Scope *sc = enter_scope();
        while (!consume("}")) {
            cur->next = stmt();
            cur = cur->next;
        }
        leave_scope(sc);

        Node *node = new_node(ND_BLOCK);
        node->body = head.next;
        add_type(node);
        return node;
    }

    if (is_type()) {
        Type *base = basetype();
        if (consume(";")) {
            return new_node(ND_NOP);
        }

        char *var_name = expect_ident();
        Type *type = type_suffix(base);
        Var *var = new_local_var(var_name, type);

        if (consume(";"))
            return new_node(ND_NOP);

        expect("=");
        Node *lhs = new_var_node(var);
        Node *rhs = expr();
        expect(";");

        Node *node = new_binary(ND_ASSIGN, lhs, rhs);
        return new_unary(ND_EXPR_STMT, node);
    }

    Node *node = new_unary(ND_EXPR_STMT, expr());
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

// unary = "sizeof" unary | ("+" | "-" | "*" | "&")? unary | ("++" | "--") unary | postfix
Node *unary() {
    if (consume("sizeof")) {
        Node *node = unary();
        add_type(node);
        switch (node->type->kind) {
        case INT:
            return new_num_node(8);
        case CHAR:
            return new_num_node(1);
        case PTR:
            return new_num_node(8);
        case ARRAY:
            return new_num_node(8 * node->type->array_size);
        case STRUCT:
            return new_num_node(node->var->type->size);
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
    if (consume("++"))
        return new_unary(ND_PRE_INC, unary());
    if (consume("--"))
        return new_unary(ND_PRE_DEC, unary());
    return postfix();
}

// gnu-stmt-expr = "(" "{" stmt stmt* "}" ")"
Node *gnu_stmt_expr() {
    Scope *sc = enter_scope();

    Node *node = new_node(ND_GNU_STMT_EXPR);
    node->body = stmt();
    Node *cur = node->body;

    while (!consume("}")) {
        cur->next = stmt();
        cur = cur->next;
    }
    expect(")");

    leave_scope(sc);

    memcpy(cur, cur->lhs, sizeof(Node));
    return node;
}

// postfix = primary ("[" expr "]" | "." ident | "->" ident | "++" | "--")
Node *postfix() {
    Node *node = primary();

    if (consume("[")) {
        /*
         * x[n]を*(x+n)に読み替える
         * 例) a[3]を*(a+3)に読み替える
         */
        Node *index = new_num_node(expect_number());
        Node *expr = new_binary(ND_PTR_ADD, node, index);

        expect("]");

        node = new_unary(ND_DEREF, expr);
    }

    if (consume(".")) {
        node = struct_ref(node);
    }

    if (consume("->")) {
        // x->y is short short for (*x).y
        node = new_unary(ND_DEREF, node);
        node = struct_ref(node);
    }

    if (consume("++")) {
        node = new_unary(ND_POST_INC, node);
    }

    if (consume("--")) {
        node = new_unary(ND_POST_DEC, node);
    }

    return node;
}

// primary = num | str | ident funcargs? | "(" expr ")" | gnu-stmt-expr
Node *primary() {
    // 次のトークンが"("なら、"(" expr ")"のはず
    if (consume("(")) {
        if (consume("{"))
            return gnu_stmt_expr();

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
