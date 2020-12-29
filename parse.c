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

static Var *new_global_var(char *name, Type *type, bool emit) {
    Var *var = new_var(name, type, false);

    if (emit) {
        VarList *vl = calloc(1, sizeof(VarList));
        vl->var = var;
        vl->next = globals;
        globals = vl;
    }

    return var;
}

static Var *push_typedef_to_scope(char *type_name, Type *base_type) {
    Var *self_defined_type = calloc(1, sizeof(Var));
    self_defined_type->name     = type_name;
    self_defined_type->len      = strlen(type_name);

    VarList *sc = calloc(1, sizeof(VarList));
    sc->var = self_defined_type;
    sc->type_def = base_type;
    sc->next = var_scope;
    var_scope = sc;

    return self_defined_type;
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

static Type *find_typedef(char *type_name) {
    for (VarList *vl = var_scope; vl; vl = vl->next) {
        Var *var = vl->var;
        if (var->len == strlen(type_name) && !memcmp(type_name, var->name, var->len))
            return vl->type_def;
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

static bool is_type();
static Type *basetype(bool *is_typedef);
static Type *declarator(Type *type, char **name);
static Type *abstract_declarator(Type *type);
static Type *type_suffix(Type *type);
static Type *type_name();
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

// basetype = builtin_type | struct_decl | typedef-name
// builtin_type = "void" | "_Bool" | "char" | "short" | "int" | "long"
static Type *basetype(bool *is_typedef) {
    if (is_typedef) *is_typedef = false;

    Type *type = int_type;
    bool non_builtin_type_already_appeared = false;
    while (is_type()) {
        if (consume("typedef")) {
            *is_typedef = true;
        } else if (consume("void")) {
            type = void_type;
        } else if (consume("_Bool")) {
            type = bool_type;
        } else if (consume("char")) {
            type = char_type;
        } else if (consume("short")) {
            type = short_type;
        } else if (consume("int")) {
            type = int_type;
        } else if (consume("long")) {
            type = long_type;
        } else if (consume("struct")) {
            if (non_builtin_type_already_appeared) break;
            type = struct_decl();
            non_builtin_type_already_appeared = true;
        } else {
            if (non_builtin_type_already_appeared) break;
            char *type_name = consume_ident();
            type = find_typedef(type_name);
            non_builtin_type_already_appeared = true;
        }
    }

    return type;
}

// declarator = "*" ("(" declarator ")" | ident) type_suffix
static Type *declarator(Type *type, char **name) {
    while (consume("*"))
        type = pointer_to(type);

    if (consume("(")) {
        Type *placeholder = calloc(1, sizeof(Type));
        Type *new_type = declarator(placeholder, name);
        expect(")");
        memcpy(placeholder, type_suffix(type), sizeof(Type));
        return new_type;
    }

    *name = expect_ident();
    return type_suffix(type);
}

// abstract_declarator = "*" ("(" abstract_declarator ")")? type_suffix
static Type *abstract_declarator(Type *type) {
    while (consume("*"))
        type = pointer_to(type);

    if (consume("(")) {
        Type *placeholder = calloc(1, sizeof(Type));
        Type *new_type = abstract_declarator(placeholder);
        expect(")");
        memcpy(placeholder, type_suffix(type), sizeof(Type));
        return new_type;
    }

    return type_suffix(type);
}

// type_suffix = ("[" num "]" type_suffix)?
static Type *type_suffix(Type *type) {
    if (!consume("["))
        return type;
    int size = expect_number();
    expect("]");

    type = type_suffix(type);
    return array_of(type, size);
}

// type_name = basetype abstract_declarator type_suffix
static Type *type_name() {
    Type *type = basetype(NULL);
    type = abstract_declarator(type);
    return type_suffix(type);
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

// struct_member = basetype declarator type_suffix ";"
static Member *struct_member() {
    Type *type = basetype(NULL);
    char *name = NULL;
    type = declarator(type, &name);
    type = type_suffix(type);
    expect(";");

    Member *member = calloc(1, sizeof(Member));
    member->name = name;
    member->type = type;

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
    return (peek("void") || peek("_Bool") || peek("char") || peek("short") || peek("int") || peek("long") || peek("struct") || peek("typedef") || find_typedef(token->str));
}

static bool is_function() {
    // 本関数呼び出し時点でのトークンの読み出し位置を覚えておく
    Token *tok = token;

    // トークンを先読みして型、識別子、（であれば関数宣言であると判断する
    bool is_typedef = false;
    Type *type = basetype(&is_typedef);
    char *func_name = NULL;
    declarator(type, &func_name);
    bool is_function = func_name && consume("(");

    // トークンの読み出し位置を元に戻す
    token = tok;

    return is_function;
}

// global_var = basetype declarator type_suffix ";"
static void global_var() {
    bool is_typedef = false;
    Type *type = basetype(&is_typedef);
    char *name = NULL;
    type = declarator(type, &name);
    type = type_suffix(type);
    expect(";");

    if (is_typedef)
        push_typedef_to_scope(name, type);
    else
        new_global_var(name, type, /* emit: */true);
}

// declaration = basetype declarator type_suffix ("=" expr)? ";"
//             | basetype ";"
static Node *declaration() {
    bool is_typedef = false;
    Type *type = basetype(&is_typedef);
    if (consume(";")) {
        return new_node(ND_NOP);
    }

    char *name = NULL;
    type = declarator(type, &name);
    type = type_suffix(type);

    if (is_typedef) {
        expect(";");
        push_typedef_to_scope(name, type);
        return new_node(ND_NOP);
    }

    if (type->kind == VOID) {
        fprintf(stderr, "%s: 変数はvoidで宣言されています。\n", name);
        exit(1);
    }

    Var *var = new_local_var(name, type);
    if (consume(";")) {
        return new_node(ND_NOP);
    }

    expect("=");
    Node *lhs = new_var_node(var);
    Node *rhs = expr();
    expect(";");

    Node *node = new_binary(ND_ASSIGN, lhs, rhs);
    return new_unary(ND_EXPR_STMT, node);
}

// program = ((type func_decl) | (type ident ("[" num "]")?))*
Program *program() {
    Function head = {};
    Function *cur = &head;
    globals = NULL;

    while(!at_eof()) {
        if (is_function()) {
            Function *f = func_decl();
            if (!f) continue;

            cur->next = f;
            cur = cur->next;
        } else {
            global_var();
        }
    }

    Program *program = calloc(1, sizeof(Program));
    program->functions = head.next;
    program->global_variables = globals;

    return program;
}

static VarList *read_func_param() {
    Type *type = basetype(NULL);
    char *name = NULL;
    type = declarator(type, &name);
    type = type_suffix(type);

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

// func_decl = basetype declarator "(" params? ")" ("{" stmt* "}" | ";")
// params    = param ("," param)*
// param     = basetype declarator type_suffix
Function *func_decl() {
    Type *type = basetype(NULL);
    char *func_name = NULL;
    type = declarator(type, &func_name);

    new_global_var(func_name, func_type(type), false);

    Function *f = calloc(1, sizeof(Function));
    f->name = func_name;
    locals = NULL;

    expect("(");

    Scope *sc = enter_scope();
    f->params = read_func_params();

    if (consume(";")) {
        leave_scope(sc);
        return NULL;
    }

    // 関数本体を読む
    Node head = {};
    Node *cur = &head;
    expect("{");
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

Node *stmt() {
    Node *node = stmt2();
    add_type(node);
    return node;
}

// stmt2 = "return" expr ";"
//       | "if" "(" expr ")" stmt ("else" stmt)?
//       | "while" "(" expr ")" stmt
//       | "for" "(" (expr? ";" | declaration) expr? ";" expr? ")" stmt
//       | "{" stmt* "}"
//       | declaration
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
        Scope *sc = enter_scope();

        if (!consume(";")) {
            if (is_type()) {
                node->init = declaration();
            } else {
                node->init = new_unary(ND_EXPR_STMT, expr());
                expect(";");
            }
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

        leave_scope(sc);
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
        return declaration();
    }

    Node *node = new_unary(ND_EXPR_STMT, expr());
    expect(";");
    return node;
}

// expr = assign ("." assign)*
Node *expr() {
    Node *node = assign();

    while (consume(",")) {
        node = new_unary(ND_EXPR_STMT, node);
        node = new_binary(ND_COMMA_OP, node, assign());
    }

    return node;
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

// unary = ("+" | "-" | "*" | "&")? unary | ("++" | "--") unary | postfix
Node *unary() {
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

// postfix = primary ("[" expr "]" | "." ident | "->" ident | "++" | "--")*
Node *postfix() {
    Node *node = primary();

    for (;;) {
      if (consume("[")) {
        // x[y] is short for *(x+y)
        Node *index = new_num_node(expect_number());
        Node *expr = new_binary(ND_PTR_ADD, node, index);

        expect("]");

        node = new_unary(ND_DEREF, expr);
        continue;
      }

      if (consume(".")) {
        node = struct_ref(node);
        continue;
      }

      if (consume("->")) {
        // x->y is short for (*x).y
        node = new_unary(ND_DEREF, node);
        node = struct_ref(node);
        continue;
      }

      if (consume("++")) {
        node = new_unary(ND_POST_INC, node);
        continue;
      }

      if (consume("--")) {
        node = new_unary(ND_POST_DEC, node);
        continue;
      }

      return node;
    }
}

// primary = num
//         | str
//         | ident funcargs?
//         | "(" expr ")"
//         | "sizeof" "(" type_name ")"
//         | "sizeof" unary
//         | gnu-stmt-expr
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

            Var *var = find_var(ident);
            if (var) {
                if (var->type->kind != FUNC)
                    fprintf(stderr, "%sは関数ではありません。\n", ident);
                node->type = var->type->return_type;
            } else {
                fprintf(stderr, "%s: 関数の暗黙的な宣言です。\n", ident);
                node->type = int_type;
            }
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

        Type *type = array_of(char_type, literal->len);
        Var *var = new_global_var(new_label(), type, /* emit: */true);
        var->contents    = literal->str;
        var->content_len = literal->len;

        return new_var_node(var);
    }

    if (consume("sizeof")) {
        Token *tok = token;
        if (consume("(")) {
            if (is_type()) {
                Type *type = type_name();
                expect(")");
                return new_num_node(type->size);
            }
            token = tok;
        }

        Node *node = unary();
        add_type(node);
        return new_num_node(node->type->size);
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
