#include "pugcc.h"

static VarList *locals  = NULL;
static VarList *globals = NULL;

static VarList *var_scope = NULL;
static TagList *tag_scope = NULL;

static Node *current_switch;

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

Node *new_node(NodeKind kind, Token *token) {
  Node *node  = calloc(1, sizeof(Node));
  node->kind  = kind;
  node->token = token;
  return node;
}

Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *token) {
  Node *node = new_node(kind, token);
  node->lhs  = lhs;
  node->rhs  = rhs;
  return node;
}

Node *new_unary(NodeKind kind, Node *expr, Token *token) {
  Node *node = new_node(kind, token);
  node->lhs = expr;
  return node;
}

Node *new_num_node(int val, Token *token) {
  Node *node = new_node(ND_NUM, token);
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

static Var *push_enum_field_to_scope(char *field_name, Type *type, int val) {
  Var *field = calloc(1, sizeof(Var));
  field->name     = field_name;
  field->len      = strlen(field_name);
  field->type     = type;
  field->enum_val = val;

  VarList *sc = calloc(1, sizeof(VarList));
  sc->var = field;
  sc->next = var_scope;
  var_scope = sc;

  return field;
}

// 変数を名前で検索する。見つからなかった場合はNULLを返す。
static Var *find_var(Token *tok) {
  for (VarList *vl = var_scope; vl; vl = vl->next) {
    Var *var = vl->var;
    if (var->len == tok->len && !memcmp(tok->str, var->name, tok->len)) {
      return var;
    }
  }
  return NULL;
}

static Type *find_typedef(Token *tok) {
  for (VarList *vl = var_scope; vl; vl = vl->next) {
    Var *var = vl->var;
    if (var->len == tok->len && !memcmp(tok->str, var->name, tok->len))
      return vl->type_def;
  }
  return NULL;
}

static TagList *find_tag_by(Token *tok) {
  for (TagList *tag = tag_scope; tag; tag = tag->next)
    if (tag->len == tok->len && !memcmp(tok->str, tag->name, tok->len))
      return tag;
  return NULL;
}

static Node *new_var_node(Var *var, Token *token) {
  Node *node  = new_node(ND_VAR, token);
  node->var   = var;
  node->token = token;
  return node;
}

static char *new_label() {
  static int cnt = 0;
  char buf[20];
  sprintf(buf, ".L.data.%d", cnt++);
  return strndup(buf, 20);
}

typedef enum {
  TYPEDEF = 1 << 0,
  STATIC  = 1 << 1,
} StorageClass;

static bool is_type();
static Type *basetype(StorageClass *sclass);
static Type *declarator(Type *type, char **name);
static Type *abstract_declarator(Type *type);
static Type *type_suffix(Type *type);
static Type *type_name();
static Type *struct_decl();
static Member *struct_member();
static Type *enum_specifier();
Function *func_decl();
Node *stmt();
Node *stmt2();
Node *expr();
long constant_expr();
Node *assign();
Node *conditional();
Node *logand();
Node *logor();
Node *bitand();
Node *bitor();
Node *bitxor();
Node *equality();
Node *relational();
Node *shift();
Node *add();
Node *mul();
Node *cast();
Node *unary();
Node *gnu_stmt_expr(Token *tok);
Node *postfix();
Node *primary();
Node *funcargs();

// basetype = builtin_type | struct_decl | typedef-name
// builtin_type = "void" | "_Bool" | "char" | "short" | "int" | "long"
static Type *basetype(StorageClass *sclass) {
  if (sclass) *sclass = 0;

  Type *type = int_type;
  bool non_builtin_type_already_appeared = false;
  while (is_type()) {
    if (peek("typedef") || peek("static")) {
      if (!sclass)
        error_tok(token, "記憶クラス指定子は許可されていません");

      if (consume("typedef")) {
        *sclass |= TYPEDEF;
      } else if (consume("static")) {
        *sclass |= STATIC;
      }

      if (*sclass & (*sclass - 1))
        error_tok(token, "typedefとstaticは同時に指定できません");
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
    } else if (peek("struct")) {
      if (non_builtin_type_already_appeared) break;
      type = struct_decl();
      non_builtin_type_already_appeared = true;
    } else if (peek("enum")) {
      if (non_builtin_type_already_appeared) break;
      type = enum_specifier();
      non_builtin_type_already_appeared = true;
    } else {
      if (non_builtin_type_already_appeared) break;
      Token *type_name = consume_ident();

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

// type_suffix = ("[" constant_expr "]" type_suffix)?
static Type *type_suffix(Type *type) {
  if (!consume("["))
    return type;
  int size = 0;
  bool is_incomplete = true;
  if (!consume("]")) {
    size = constant_expr();
    is_incomplete = false;
    expect("]");
  }

  Token *tok = token;
  type = type_suffix(type);

  if (type->is_incomplete)
    error_tok(tok, "配列の要素数が指定されていません");

  type = array_of(type, size);
  type->is_incomplete = is_incomplete;

  return type;
}

// type_name = basetype abstract_declarator type_suffix
static Type *type_name() {
  StorageClass sclass;
  Type *type = basetype(&sclass);
  type = abstract_declarator(type);
  return type_suffix(type);
}

static void push_tag_to_scope(Token *tok, Type *type) {
  TagList *tag = calloc(1, sizeof(TagList));
  tag->next = tag_scope;
  tag->name = strndup(tok->str, tok->len);
  tag->len  = tok->len;
  tag->type = type;
  tag_scope = tag;
}

// struct_decl = "struct" ident
//             | "struct" ident? "{" struct_member "}"
static Type *struct_decl() {
  expect("struct");

  // 構造体名を読む
  Token *tag_name = consume_ident();
  if (tag_name && !peek("{")) {
    TagList *tag = find_tag_by(tag_name);

    if (!tag) {
      Type *type = struct_type();
      push_tag_to_scope(tag_name, type);
      return type;
    }

    if (tag->type->kind != STRUCT)
      error_at(tag->name, "構造体のタグではありません");

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
    if (member->type->is_incomplete)
      error_tok(member->token, "構造体のメンバーの型が不完全です");

    offset = align_to(offset, member->type->align);
    member->offset = offset;
    offset += member->type->size;

    if (type->align < member->type->align)
      type->align = member->type->align;
  }
  type->size = align_to(offset, type->align);

  if (tag_name) {
    push_tag_to_scope(tag_name, type);
  }

  return type;
}

// struct_member = basetype declarator type_suffix ";"
static Member *struct_member() {
  StorageClass sclass;
  Type *type = basetype(&sclass);
  Token *tok = token;
  char *name = NULL;
  type = declarator(type, &name);
  type = type_suffix(type);
  expect(";");

  Member *member = calloc(1, sizeof(Member));
  member->name = name;
  member->type = type;
  member->token = tok;

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

  Token *tok = token; // for error message

  Member *member = find_member(lhs->type, expect_ident());

  Node *node = new_unary(ND_MEMBER, lhs, tok);
  node->member = member;

  return node;
}

// enum_list = enum_elem ("," enum_elem)* ","?
// enum_elem = ident ("=" constant_expr)?
static Type *enum_specifier() {
  expect("enum");
  Type *type = enum_type;

  Token *tag_name = consume_ident();
  if (tag_name && !peek("{")) {
    TagList *scope = find_tag_by(tag_name);
    if (!scope)
      error_tok(tag_name, "未定義のenumです");
    if (scope->type->kind != ENUM)
      error_tok(tag_name, "enumのタグではありません");
    return scope->type;
  }

  expect("{");

  int count = 0;
  for (;;) {
    char *name = expect_ident();
    if (consume("="))
      count = constant_expr();

    push_enum_field_to_scope(name, type, count++);

    if (consume_end()) break;

    expect(",");
  }

  if (tag_name) {
    push_tag_to_scope(tag_name, type);
  }

  return type;
}

static bool is_type() {
  return (peek("void") || peek("_Bool") || peek("char") || peek("short") || peek("int") || peek("long") || peek("enum") || peek("struct") || peek("typedef") || peek("static") || find_typedef(token));
}

static bool is_function() {
  bool is_function = false;

  // 本関数呼び出し時点でのトークンの読み出し位置を覚えておく
  Token *tok = token;

  // トークンを先読みして型、識別子、（であれば関数宣言であると判断する
  StorageClass sclass;
  Type *type = basetype(&sclass);

  if (!consume(";")) {
    char *func_name = NULL;
    declarator(type, &func_name);
    is_function = func_name && consume("(");
  }

  // トークンの読み出し位置を元に戻す
  token = tok;

  return is_function;
}

// global_var = basetype declarator type_suffix ";"
static void global_var() {
  StorageClass sclass;
  Type *type = basetype(&sclass);
  if (consume(";"))
    return;

  char *name = NULL;
  Token *tok = token;
  type = declarator(type, &name);
  type = type_suffix(type);
  expect(";");

  if (type->is_incomplete)
    error_tok(tok, "不完全な型です");

  if (sclass == TYPEDEF) {
    push_typedef_to_scope(name, type);
  } else {
    if (type->is_incomplete)
      error_tok(tok, "不完全な型です");

    new_global_var(name, type, /* emit: */true);
  }
}

typedef struct Designator Designator;
struct Designator {
  Designator *next;
  int index;      // for array
  Member *member; // for struct
};

static Node *refs_elem_of(Var *var, Designator *desg, Token *tok) {
  if (!desg) return new_var_node(var, tok);

  Node *node = refs_elem_of(var, desg->next, tok);

  if (desg->member) {
    node = new_unary(ND_MEMBER, node, tok);
    node->member = desg->member;
    return node;
  }

  node = new_binary(ND_PTR_ADD, node, new_num_node(desg->index, tok), tok);
  return new_unary(ND_DEREF, node, tok);
}

static Node *assign_expr(Var *var, Designator *desg, Node *rhs) {
  Node *lhs = refs_elem_of(var, desg, rhs->token);
  Node *node = new_binary(ND_ASSIGN, lhs, rhs, rhs->token);
  return new_unary(ND_EXPR_STMT, node, rhs->token);
}

static Node *initialize_with_zero(Node *cur, Var *var, Type *type, Designator *desg) {
  if (type->kind == ARRAY) {
    Var *array = var;

    for (int i = 0; i < type->array_size; i++) {
      Designator next_elem_desg = { desg, i++ };
      cur = initialize_with_zero(cur, array, type->base, &next_elem_desg);
    }
    return cur;
  }

  cur->next = assign_expr(var, desg, new_num_node(0, token));
  return cur->next;
}

// initializer = assign_expr
//             | "{" (initializer_list ("," initializer_list)* ",")? "}"
static Node *initializer_list(Node *cur, Var *var, Type *type, Designator *desg) {
  if (type->kind == ARRAY && type->base->kind == CHAR && token->kind == TK_STR) {
    Token *tok = token;
    token = token->next;

    if (type->is_incomplete) {
      // 配列長が未指定の場合には値(文字列長)分のサイズとみなす
      type->size       = tok->len;
      type->array_size = tok->len;

      type->is_incomplete = false;
    }

    int len = (type->array_size < tok->len) ? type->array_size : tok->len;

    for (int i = 0; i < len; i++) {
      Designator next_elem_desg = { desg, i };
      Node *rhs = new_num_node(tok->str[i], tok);
      cur->next = assign_expr(var, &next_elem_desg, rhs);
      cur = cur->next;
    }

    for (int i = len; i < type->array_size; i++) {
      Designator next_elem_desg = { desg, i };
      cur = initialize_with_zero(cur, var, type->base, &next_elem_desg);
    }

    return cur;
  }

  if (type->kind == ARRAY) {
    Var *array = var;

    expect("{");

    int i = 0;
    if (!peek("}")) {
      do {
        Designator next_elem_desg = { desg, i++ };
        cur = initializer_list(cur, array, type->base, &next_elem_desg);
      } while (!peek_end() && consume(","));
    }

    expect_end();

    while (i < type->array_size) {
      Designator next_elem_desg = { desg, i++ };
      cur = initialize_with_zero(cur, array, type->base, &next_elem_desg);
    }

    if (type->is_incomplete) {
      // 配列長が未指定の場合には配列長＝要素数とする
      type->size       = type->base->size * i;
      type->array_size = i;

      type->is_incomplete = false;
    }

    return cur;
  }

  if (type->kind == STRUCT) {
    Member *member = type->members;

    expect("{");

    if (!peek("}")) {
      do {
        Designator next_elem_desg = { desg, 0, member };
        cur = initializer_list(cur, var, member->type, &next_elem_desg);
        member = member->next;
      } while (!peek_end() && consume(","));
    }

    expect_end();

    for (; member; member = member->next) {
      Designator next_elem_desg = { desg, 0, member };
      cur = initialize_with_zero(cur, var, member->type, &next_elem_desg);
    }
    return cur;
  }

  cur->next = assign_expr(var, desg, assign());
  return cur->next;
}

static Node *initializer(Var *var, Token *tok) {
  Node head = {};

  initializer_list(&head, var, var->type, NULL);

  Node *node = new_node(ND_BLOCK, tok);
  node->body = head.next;

  return node;
}

// declaration = basetype declarator type_suffix ("=" expr)? ";"
//             | basetype ";"
static Node *declaration() {
  Token *tok = token; // for error message

  StorageClass sclass;
  Type *type = basetype(&sclass);
  if ((tok = consume(";"))) {
    if (type->is_incomplete)
      error_tok(tok, "不完全な型です");

    return new_node(ND_NOP, tok);
  }

  tok = token;
  char *name = NULL;
  type = declarator(type, &name);
  type = type_suffix(type);

  if (sclass == TYPEDEF) {
    expect(";");
    push_typedef_to_scope(name, type);
    return new_node(ND_NOP, tok);
  }

  if (type->kind == VOID) {
    fprintf(stderr, "%s: 変数はvoidで宣言されています。\n", name);
    exit(1);
  }

  Var *var = new_local_var(name, type);

  if (consume(";")) {
    if (type->is_incomplete)
      error_tok(tok, "不完全な型です");

    return new_node(ND_NOP, tok);
  }

  expect("=");
  Node *node = initializer(var, tok);
  expect(";");

  return node;
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

  /*
   * 関数の引数において"T型の配列"は"Tへのポインタ"に変換される。
   * ex) *argv => **argv
   */
  if (type->kind == ARRAY)
    type = pointer_to(type->base);

  VarList *vl = calloc(1, sizeof(VarList));
  vl->var = new_local_var(name, type);

  return vl;
}

static VarList *read_func_params() {
  if (consume(")"))
    return NULL;

  Token *tok = token;
  if (consume("void") && consume(")"))
    return NULL;
  token = tok;

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
// params    = param ("," param)* | "void"
// param     = basetype declarator type_suffix
Function *func_decl() {
  StorageClass sclass;
  Type *type = basetype(&sclass);
  char *func_name = NULL;
  type = declarator(type, &func_name);

  new_global_var(func_name, func_type(type), false);

  Function *f = calloc(1, sizeof(Function));
  f->name = func_name;
  f->is_static = (sclass == STATIC);
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

// stmt2 = "return" expr? ";"
//       | "if" "(" expr ")" stmt ("else" stmt)?
//       | "switch" "(" expr ")" stmt
//       | "case" constant_expr ":" stmt
//       | "default" ":" stmt
//       | "while" "(" expr ")" stmt
//       | "for" "(" (expr? ";" | declaration) expr? ";" expr? ")" stmt
//       | "do" stmt "while" "(" expr ")" ";"
//       | "{" stmt* "}"
//       | "break" ";"
//       | "continue" ";"
//       | "goto" ident ";"
//       | ident ":" stmt
//       | ";"
//       | declaration
//       | expr ";"
Node *stmt2() {
  Token *tok; // for error message

  if ((tok = consume("return"))) {
    if (consume(";"))
      return new_node(ND_RETURN, tok);

    Node *node = new_unary(ND_RETURN, expr(), tok);
    add_type(node);
    expect(";");
    return node;
  }

  if ((tok = consume("if"))) {
    Node *node = new_node(ND_IF, tok);
    expect("(");
    node->cond = expr();
    expect(")");
    node->then = stmt();
    if (consume("else"))
      node->els = stmt();
    add_type(node);
    return node;
  }

  if ((tok = consume("switch"))) {
    Node *node = new_node(ND_SWITCH, tok);
    expect("(");
    node->cond = expr();
    expect(")");

    Node *sw = current_switch;
    current_switch = node;
    node->then = stmt();
    current_switch = sw;
    return node;
  }

  if ((tok = consume("case"))) {
    int val = constant_expr();
    expect(":");

    Node *node = new_unary(ND_CASE, stmt(), tok);
    node->val = val;
    node->case_next = current_switch->case_next;
    current_switch->case_next = node;
    return node;
  }

  if ((tok = consume("default"))) {
    expect(":");

    Node *node = new_unary(ND_CASE, stmt(), tok);
    current_switch->default_case = node;
    return node;
  }

  if ((tok = consume("while"))) {
    Node *node = new_node(ND_WHILE, tok);
    expect("(");
    node->cond = expr();
    expect(")");
    node->then = stmt();
    add_type(node);
    return node;
  }

  if ((tok = consume("for"))) {
    Node *node = new_node(ND_FOR, tok);
    expect("(");
    Scope *sc = enter_scope();

    if (!consume(";")) {
      if (is_type()) {
        node->init = declaration();
      } else {
        tok = token;
        node->init = new_unary(ND_EXPR_STMT, expr(), tok);
        expect(";");
      }
    }
    if (!consume(";")) {
      node->cond = expr();
      expect(";");
    }
    if (!consume(")")) {
      tok = token;
      node->inc = new_unary(ND_EXPR_STMT, expr(), tok);
      expect(")");
    }
    node->then = stmt();
    add_type(node);

    leave_scope(sc);
    return node;
  }

  if ((tok = consume("do"))) {
    Node *node = new_node(ND_DO, tok);
    node->then = stmt();
    expect("while");
    expect("(");
    node->cond = expr();
    expect(")");
    expect(";");
    return node;
  }

  if ((tok = consume("{"))) {
    Node head = {};
    Node *cur = &head;

    Scope *sc = enter_scope();
    while (!consume("}")) {
      cur->next = stmt();
      cur = cur->next;
    }
    leave_scope(sc);

    Node *node = new_node(ND_BLOCK, tok);
    node->body = head.next;
    add_type(node);
    return node;
  }

  if ((tok = consume("break"))) {
    expect(";");
    return new_node(ND_BREAK, tok);
  }

  if ((tok = consume("continue"))) {
    expect(";");
    return new_node(ND_CONTINUE, tok);
  }

  if ((tok = consume("goto"))) {
    Node *node = new_node(ND_GOTO, tok);
    node->label_name = expect_ident();
    expect(";");
    return node;
  }

  if ((tok = consume_ident())) {
    if (consume(":")) {
      Node *node = new_unary(ND_LABEL, stmt(), tok);
      node->label_name = strndup(tok->str, tok->len);
      return node;
    }
    token = tok;
  }

  if ((tok = consume(";"))) {
    return new_node(ND_NOP, tok);
  }

  if (is_type()) {
    return declaration();
  }

  tok = token;
  Node *node = new_unary(ND_EXPR_STMT, expr(), tok);
  expect(";");
  return node;
}

// expr = assign ("." assign)*
Node *expr() {
  Node *node = assign();

  Token *tok; // for error message
  while (consume(",")) {
    node = new_unary(ND_EXPR_STMT, node, node->token);
    node = new_binary(ND_COMMA_OP, node, assign(), tok);
  }

  return node;
}

static long eval(Node *node) {
  switch (node->kind) {
    case ND_ADD:
      return eval(node->lhs) + eval(node->rhs);
    case ND_SUB:
      return eval(node->lhs) - eval(node->rhs);
    case ND_MUL:
      return eval(node->lhs) * eval(node->rhs);
    case ND_DIV:
      return eval(node->lhs) / eval(node->rhs);
    case ND_BITAND:
      return eval(node->lhs) & eval(node->rhs);
    case ND_BITOR:
      return eval(node->lhs) | eval(node->rhs);
    case ND_BITXOR:
      return eval(node->lhs) | eval(node->rhs);
    case ND_SHL:
      return eval(node->lhs) << eval(node->rhs);
    case ND_SHR:
      return eval(node->lhs) >> eval(node->rhs);
    case ND_EQ:
      return eval(node->lhs) == eval(node->rhs);
    case ND_NE:
      return eval(node->lhs) != eval(node->rhs);
    case ND_LT:
      return eval(node->lhs) < eval(node->rhs);
    case ND_LE:
      return eval(node->lhs) <= eval(node->rhs);
    case ND_TERNARY:
      return eval(node->cond) ? eval(node->then) : eval(node->els);
    case ND_COMMA_OP:
      return eval(node->rhs);
    case ND_NOT:
      return !eval(node->lhs);
    case ND_BITNOT:
      return ~eval(node->lhs);
    case ND_LOGAND:
      return eval(node->lhs) && eval(node->rhs);
    case ND_LOGOR:
      return eval(node->lhs) || eval(node->rhs);
    case ND_NUM:
      return node->val;
  }
}

long constant_expr() {
  return eval(conditional());
}

// assign    = conditional (assign-op assign)?
// assign-op = "=" | "+=" | "-=" | "*=" | "/=" | "<<=" | ">>=" | "&=" | "|=" | "^="
Node *assign() {
  Node *node = conditional();

  Token *tok;
  if ((tok = consume("=")))
    return new_binary(ND_ASSIGN, node, assign(), tok);

  if ((tok = consume("*=")))
    return new_binary(ND_MUL_EQ, node, assign(), tok);

  if ((tok = consume("/=")))
    return new_binary(ND_DIV_EQ, node, assign(), tok);

  if ((tok = consume("<<=")))
    return new_binary(ND_SHL_EQ, node, assign(), tok);

  if ((tok = consume(">>=")))
    return new_binary(ND_SHR_EQ, node, assign(), tok);

  if ((tok = consume("&=")))
    return new_binary(ND_BITAND_EQ, node, assign(), tok);

  if ((tok = consume("|=")))
    return new_binary(ND_BITOR_EQ, node, assign(), tok);

  if ((tok = consume("^=")))
    return new_binary(ND_BITXOR_EQ, node, assign(), tok);

  if ((tok = consume("+="))) {
    add_type(node);
    if (node->type->base)
      return new_binary(ND_PTR_ADD_EQ, node, assign(), tok);
    else
      return new_binary(ND_ADD_EQ, node, assign(), tok);
  }

  if ((tok = consume("-="))) {
    add_type(node);
    if (node->type->base)
      return new_binary(ND_PTR_SUB_EQ, node, assign(), tok);
    else
      return new_binary(ND_SUB_EQ, node, assign(), tok);
  }

  return node;
}

// conditional = logor ("?" expr ":" condtional)?
Node *conditional() {
  Node *node = logor();

  Token *tok = consume("?");
  if (!tok)
    return node;

  Node *ternary = new_node(ND_TERNARY, tok);
  ternary->cond = node;
  ternary->then = expr();
  expect(":");
  ternary->els  = conditional();

  return ternary;
}

// logor = logand ("||" logand)*
Node *logor() {
  Node *node = logand();
  Token *tok;
  while ((tok = consume("||")))
    node = new_binary(ND_LOGOR, node, logand(), tok);

  return node;
}

// logand = bitor ("&&" bitor)*
Node *logand() {
  Node *node = bitor();
  Token *tok;
  while ((tok = consume("&&")))
    node = new_binary(ND_LOGAND, node, bitor(), tok);

  return node;
}

// bitor = bitxor ("|" bitxor)*
Node *bitor() {
  Node *node = bitxor();
  Token *tok;
  while ((tok = consume("|")))
    node = new_binary(ND_BITOR, node, bitxor(), tok);

  return node;
}

// bitxor = bitand ("^" bitand)*
Node *bitxor() {
  Node *node = bitand();
  Token *tok;
  while ((tok = consume("^")))
    node = new_binary(ND_BITXOR, node, bitxor(), tok);

  return node;
}

// bitand = equality ("&" equality)*
Node *bitand() {
  Node *node = equality();
  Token *tok;
  while ((tok = consume("&")))
    node = new_binary(ND_BITAND, node, equality(), tok);

  return node;
}

// equality = relational ("==" relational | "!=" relational)*
Node *equality() {
  Node *node = relational();
  Token *tok;
  for (;;) {
    if ((tok = consume("==")))
      node = new_binary(ND_EQ, node, relational(), tok);
    else if ((tok = consume("!=")))
      node = new_binary(ND_NE, node, relational(), tok);
    else
      return node;
  }
}

// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
Node *relational() {
  Node *node = shift();
  Token *tok;
  for (;;) {
    if ((tok = consume("<")))
      node = new_binary(ND_LT, node, shift(), tok);
    else if ((tok = consume("<=")))
      node = new_binary(ND_LE, node, shift(), tok);
    else if ((tok = consume(">")))
      node = new_binary(ND_LT, shift(), node, tok);
    else if ((tok = consume(">=")))
      node = new_binary(ND_LE, shift(), node, tok);
    else
      return node;
  }
}

// shift = add ("<<" add | ">>" add)*
Node *shift() {
  Node *node = add();
  Token *tok;
  for (;;) {
    if ((tok = consume("<<")))
      node = new_binary(ND_SHL, node, add(), tok);
    else if ((tok = consume(">>")))
      node = new_binary(ND_SHR, node, add(), tok);
    else
      return node;
  }
}

// add = mul ("+" mul | "-" mul)*
Node *add() {
  Node *node = mul();
  Token *tok;
  for (;;) {
    if ((tok = consume("+"))) {
      Node *lhd = node;
      Node *rhd = mul();
      if (is_pointer(lhd) || is_pointer(rhd) || is_array(lhd) || is_array(rhd)) {
        node = new_binary(ND_PTR_ADD, lhd, rhd, tok);
      } else {
        node = new_binary(ND_ADD,     lhd, rhd, tok);
      }
    } else if ((tok = consume("-"))) {
      Node *lhd = node;
      Node *rhd = mul();
      if (is_pointer(lhd) && is_pointer(rhd) || is_array(lhd) || is_array(rhd)) {
        node = new_binary(ND_PTR_DIFF, lhd, rhd, tok);
      } else if (is_pointer(lhd) || is_pointer(rhd)) {
        node = new_binary(ND_PTR_SUB,  lhd, rhd, tok);
      } else {
        node = new_binary(ND_SUB,      lhd, rhd, tok);
      }
    } else {
      return node;
    }
  }
}

// mul = cast ("*" cast | "/" cast)*
Node *mul() {
  Node *node = cast();
  Token *tok;
  for (;;) {
    if ((tok = consume("*")))
      node = new_binary(ND_MUL, node, cast(), tok);
    else if ((tok = consume("/")))
      node = new_binary(ND_DIV, node, cast(), tok);
    else
      return node;
  }
}

// cast = "(" type_name ")" cast | unary
Node *cast() {
  Token *tok = token;
  if (consume("(")) {
    if (is_type()) {
      Type *type = type_name();
      expect(")");
      Node *node = new_unary(ND_CAST, cast(), tok);
      add_type(node->lhs);
      node->type = type;
      return node;
    }
    token = tok;
  }

  return unary();
}
// unary = ("+" | "-" | "*" | "&" | "!" | "~")? cast | ("++" | "--") cast | postfix
Node *unary() {
  Token *tok;
  if ((tok = consume("+")))
    return cast();
  if ((tok = consume("-")))
    return new_binary(ND_SUB, new_num_node(0, tok), cast(), tok);
  if ((tok = consume("*")))
    return new_unary(ND_DEREF, cast(), tok);
  if ((tok = consume("&")))
    return new_unary(ND_ADDR, cast(), tok);
  if ((tok = consume("!")))
    return new_unary(ND_NOT, cast(), tok);
  if ((tok = consume("~")))
    return new_unary(ND_BITNOT, cast(), tok);
  if ((tok = consume("++")))
    return new_unary(ND_PRE_INC, cast(), tok);
  if ((tok = consume("--")))
    return new_unary(ND_PRE_DEC, cast(), tok);
  return postfix();
}

// gnu-stmt-expr = "(" "{" stmt stmt* "}" ")"
Node *gnu_stmt_expr(Token *tok) {
  Scope *sc = enter_scope();

  Node *node = new_node(ND_GNU_STMT_EXPR, tok);
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
  Token *tok;

  Node *node = primary();

  for (;;) {
    if ((tok = consume("["))) {
      // x[y] is short for *(x+y)
      Node *exp = new_binary(ND_PTR_ADD, node, expr(), tok);

      expect("]");

      node = new_unary(ND_DEREF, exp, tok);
      continue;
    }

    if ((tok = consume("."))) {
      node = struct_ref(node);
      continue;
    }

    if ((tok = consume("->"))) {
      // x->y is short for (*x).y
      node = new_unary(ND_DEREF, node, tok);
      node = struct_ref(node);
      continue;
    }

    if ((tok = consume("++"))) {
      node = new_unary(ND_POST_INC, node, tok);
      continue;
    }

    if ((tok = consume("--"))) {
      node = new_unary(ND_POST_DEC, node, tok);
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
  Token *tok;

  // 次のトークンが"("なら、"(" expr ")"のはず
  if ((tok = consume("("))) {
    if (consume("{"))
      return gnu_stmt_expr(tok);

    Node *node = expr();
    expect(")");
    return node;
  }

  if ((tok = consume_ident())) {
    char *ident = strndup(tok->str, tok->len);

    if (consume("(")) {
      // 関数呼び出し
      Node *node     = new_node(ND_FUNCCALL, tok);
      node->funcname = ident;
      node->args     = funcargs();
      add_type(node);

      Var *var = find_var(tok);
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
      Node *node = new_node(ND_VAR, tok);

      // 変数 or 列挙定数
      Var *var = find_var(tok);
      if (var) {
        if (var->type->kind == ENUM) {
          node = new_num_node(var->enum_val, tok);
        } else {
          node->type   = var->type;
          node->offset = var->offset;
          node->var    = var;
        }
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

    return new_var_node(var, literal);
  }

  if ((tok = consume("sizeof"))) {
    if (consume("(")) {
      if (is_type()) {
        Type *type = type_name();
        if (type->is_incomplete)
          error_tok(tok, "不完全な型です");

        expect(")");
        return new_num_node(type->size, tok);
      }
      token = tok->next;
    }

    Node *node = unary();
    add_type(node);

    if (node->type->is_incomplete)
      error_tok(tok, "不完全な型です");

    return new_num_node(node->type->size, tok);
  }

  if (token->kind != TK_NUM)
    error_tok(token, "式ではありません");

  // そうでなければ数値のはず
  return new_num_node(expect_number(), token);
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
