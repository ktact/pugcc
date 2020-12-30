#include "pugcc.h"

// ラベルの通し番号
static int label_seq_num = 0;
// 関数呼び出しの引数を積むレジスタ
static char *_1byte_arg_regs[] = { "dil", "sil", "dl",  "cl",  "r8b", "r9b" };
static char *_2byte_arg_regs[] = { "di",  "si",  "dx",  "cx",  "r8w", "r9w" };
static char *_4byte_arg_regs[] = { "edi", "esi", "edx", "ecx", "r8d", "r9d" };
static char *_8byte_arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8",  "r9"  };

static void gen(Node *node);

static void load(Type *type) {
    printf("  pop rax\n");
    if (type->size == 1) {
        printf("  movsx rax, byte ptr [rax]\n");
    } else if (type->size == 2) {
        printf("  movsx rax, word ptr [rax]\n");
    } else if (type->size == 4) {
        printf("  movsxd rax, dword ptr [rax]\n");
    } else {
        assert(type->size == 8);
        printf("  mov rax, [rax]\n");
    }
    printf("  push rax\n");
}

static void store(Type *type) {
    printf("  pop rdi\n"); // RDI = rhs
    printf("  pop rax\n"); // RAX = lhs

    if (type->kind == BOOL) {
        // RDI = (RDI != 0) ? 1 : 0
        printf("  cmp rdi, 0\n");     // RDIと0を比較し結果をフラグレジスタにセットする
        printf("  setne dil\n");      // RDI != 0である場合にDILに1をセットする
        printf("  movzb rdi, dil\n"); // 上位56ビットを0クリアし下位8ビットにDILの値をセットする
    }

    if (type->size == 1) {
        printf("  mov [rax], dil\n");
    } else if (type->size == 2) {
        printf(" mov [rax], di\n");
    } else if (type->size == 4) {
        printf("  mov [rax], edi\n");
    } else {
        assert(type->size == 8);
        printf("  mov [rax], rdi\n");
    }

    printf("  push rdi\n");
}

static void inc(Type *type) {
    printf("  pop rax\n");
    printf("  add rax, %d\n", type->kind == PTR ? type->pointer_to->size : 1);
    printf("  push rax\n");
}

static void dec(Type *type) {
    printf("  pop rax\n");
    printf("  sub rax, %d\n", type->kind == PTR ? type->pointer_to->size : 1);
    printf("  push rax\n");
}

static void gen_addr(Var *var) {
    if (var->is_local) {
        printf("  lea rax, [rbp-%d]\n", var->offset);
        printf("  push rax\n");
    } else {
        printf("  push offset %s\n", var->name);
    }
}

static void gen_lval(Node *node) {
    if (node->kind != ND_VAR && node->kind != ND_MEMBER && node->kind != ND_DEREF)
        error("代入の左辺値が変数ではありません");

    switch (node->kind) {
    case ND_VAR:
        // 左辺値のアドレスをスタックに積む
        gen_addr(node->var);
        break;
    case ND_MEMBER:
        // 左辺値のアドレスをスタックに積む
        printf("  lea rax, [rbp-%d]\n", node->member->offset);
        printf("  push rax\n");
        break;
    case ND_DEREF:
        // アドレスを計算しスタックに積む
        gen(node->lhs);
        break;
    }
}

static void truncate(Type *type) {
    printf("  pop rax\n");

    if (type->kind == BOOL) {
        printf("  cmp rax, 0\n");
        printf("  setne al\n");
    }

    if (type->size == 1) {
        printf("  movsx rax, al\n");
    } else if (type->size == 2) {
        printf("  movsx rax, ax\n");
    } else if (type->size == 4) {
        printf("  movsxd rax, eax\n");
    }

    printf("  push rax\n");
}
static void gen(Node *node) {
    switch (node->kind) {
    case ND_NUM:
        if (node->val == (int)node->val) {
            printf("  push %ld\n", node->val);
        } else {
            printf("  movabs rax, %ld\n", node->val);
            printf("  push rax\n");
        }
        return;
    case ND_VAR:
        gen_lval(node);

        if (node->type->kind != ARRAY && node->var->is_local)
            load(node->type);

        return;
    case ND_CAST:
        gen(node->lhs);
        truncate(node->type);
        return;
    case ND_EXPR_STMT:
        gen(node->lhs);
        printf("  add rsp, 8\n");
        return;
    case ND_MEMBER:
        gen_lval(node);
        load(node->type);

        return;
    case ND_ASSIGN:
        gen_lval(node->lhs);
        gen(node->rhs);
        store(node->type);
        return;
    case ND_PRE_INC:
        gen_lval(node->lhs);
        printf("  push [rsp]\n");
        load(node->type);
        inc(node->type);
        store(node->type);
        return;
    case ND_PRE_DEC:
        gen_lval(node->lhs);
        printf("  push [rsp]\n");
        load(node->type);
        dec(node->type);
        store(node->type);
        return;
    case ND_POST_INC:
        gen_lval(node->lhs);
        printf("  push [rsp]\n");
        load(node->type);
        inc(node->type);
        store(node->type);
        dec(node->type);
        return;
    case ND_POST_DEC:
        gen_lval(node->lhs);
        printf("  push [rsp]\n");
        load(node->type);
        dec(node->type);
        store(node->type);
        inc(node->type);
        return;
    case ND_NOT:
        gen(node->lhs);
        printf("  pop rax\n");
        // RAX=0であれば1にする
        printf("  cmp rax, 0\n");
        printf("  sete al\n");
        printf("  movzb rax, al\n");
        printf("  push rax\n");
        return;
    case ND_IF: {
        int seq_num = label_seq_num++;
        if (node->els) {
            gen(node->cond);
            printf("  pop rax\n");
            printf("  cmp rax, 0\n");
            printf("  je  .L.else.%d\n", seq_num);
            gen(node->then);
            printf("  jmp .L.end.%d\n", seq_num);
            printf(".L.else.%d:\n", seq_num);
            gen(node->els);
            printf(".L.end.%d:\n", seq_num);
        } else {
            gen(node->cond);
            printf("  pop rax\n");
            printf("  cmp rax, 0\n");
            printf("  je  .L.end.%d\n", seq_num);
            gen(node->then);
            printf(".L.end.%d:\n", seq_num);
        }
        return;
    }
    case ND_WHILE: {
        int seq_num = label_seq_num++;
        printf(".L.begin.%d:\n", seq_num);
        gen(node->cond);
        printf("  pop rax\n");
        printf("  cmp rax, 0\n");
        printf("  je  .L.end.%d\n", seq_num);
        gen(node->then);
        printf("  jmp .L.begin.%d\n", seq_num);
        printf(".L.end.%d:\n", seq_num);
        return;
    }
    case ND_FOR: {
        int seq_num = label_seq_num++;
        if (node->init)
            gen(node->init);
        printf(".L.begin.%d:\n", seq_num);
        if (node->cond) {
            gen(node->cond);
            printf("  pop rax\n");
            printf("  cmp rax, 0\n");
            printf("  je  .L.end.%d\n", seq_num);
        }
        gen(node->then);
        if (node->inc)
            gen(node->inc);
        printf("  jmp .L.begin.%d\n", seq_num);
        printf(".L.end.%d:\n", seq_num);

        return;
    }
    case ND_BLOCK:
    case ND_GNU_STMT_EXPR:
        for (Node *n = node->body; n; n = n->next)
            gen(n);
        return;
    case ND_FUNCCALL: {
        int number_of_args = 0;
        for (Node *arg = node->args; arg; arg = arg->next) {
            gen(arg);
            number_of_args++;
        }
        for (int i = number_of_args-1; i >= 0; i--) {
            printf("  pop %s\n", _8byte_arg_regs[i]);
        }

        int seq_num = label_seq_num++;
        printf("  mov rax, rsp\n");
        printf("  and rax, 15\n");
        printf("  jnz .L.call.%d\n", seq_num);
        printf("  mov rax, 0\n");
        printf("  call %s\n", node->funcname);
        printf("  jmp .L.end.%d\n", seq_num);
        printf(".L.call.%d:\n", seq_num);
        printf("  sub rsp, 8\n");
        printf("  mov rax, 0\n");
        printf("  call %s\n", node->funcname);
        printf("  add rsp, 8\n");
        printf(".L.end.%d:\n", seq_num);
        printf("  push rax\n");
        return;
    }
    case ND_COMMA_OP:
        gen(node->lhs);
        gen(node->rhs);
        return;
    case ND_ADDR:
        gen_lval(node->lhs);
        return;
    case ND_DEREF:
        // 変数のアドレスをスタックに積む
        gen(node->lhs);

        if (node->type->kind != ARRAY)
          load(node->type);
        return;
    case ND_RETURN:
        gen(node->lhs);
        printf("  pop rax\n");
        printf("  mov rsp, rbp\n");
        printf("  pop rbp\n");
        printf("  ret\n");
        return;
    case ND_NOP:
        return;
    }

    gen(node->lhs);
    gen(node->rhs);

    printf("  pop rdi\n");
    printf("  pop rax\n");

    switch (node->kind) {
    case ND_ADD:
        printf("  add rax, rdi\n");
        break;
    case ND_SUB:
        printf("  sub rax, rdi\n");
        break;
    case ND_PTR_ADD:
        printf("  imul rdi, %d\n", node->lhs->type->base->size);
        printf("  add rax, rdi\n");
        break;
    case ND_PTR_SUB:
        printf("  imul rdi, %d\n", node->lhs->type->base->size);
        printf("  sub rax, rdi\n");
        break;
    case ND_PTR_DIFF:
        // アドレスの値を引いた後にそのポインタが指す変数の型のバイト数で割る
        printf("  sub rax, rdi\n");
        printf("  cqo\n");
        if (is_array(node->lhs))
            printf("  imul rdi, %d\n", node->lhs->type->base->size);
        else
            printf("  mov rdi, 4\n");
        printf("  idiv rdi\n");
        break;
    case ND_MUL:
        printf("  imul rax, rdi\n");
        break;
    case ND_DIV:
        printf("  cqo\n");
        printf("  idiv rdi\n");
        break;
    case ND_EQ:
        printf("  cmp rax, rdi\n");
        printf("  sete al\n");
        printf("  movzb rax, al\n");
        break;
    case ND_NE:
        printf("  cmp rax, rdi\n");
        printf("  setne al\n");
        printf("  movzb rax, al\n");
        break;
    case ND_LT:
        printf("  cmp rax, rdi\n");
        printf("  setl al\n");
        printf("  movzb rax, al\n");
        break;
    case ND_LE:
        printf("  cmp rax, rdi\n");
        printf("  setle al\n");
        printf("  movzb rax, al\n");
        break;
    }

    printf("  push rax\n");
}

static void emit_data(Program *program) {
    printf(".data\n");
    for (VarList *vl = program->global_variables; vl; vl = vl->next) {
        Var *global_var = vl->var;

        printf("%s:\n", global_var->name);

        if (!global_var->contents) {
            printf("  .zero %d\n", global_var->type->size);
            continue;
        }

        for (int i = 0; i < global_var->content_len; i++)
            printf("  .byte %d\n", global_var->contents[i]);
    }
}

static void emit_text(Program *program) {
    printf(".text\n");
    for (Function *f = program->functions; f; f = f->next) {
        printf(".global %s\n", f->name);
        printf("%s:\n", f->name);

        // プロローグ
        printf("  push rbp\n");
        printf("  mov rbp, rsp\n");
        printf("  sub rsp, %d\n", f->stack_size);

        int i = 0;
        for (VarList *vl = f->params; vl; vl = vl->next) {
            Var *param = vl->var;

            if (param->type->size == 1) {
                printf("  mov [rbp-%d], %s\n", param->offset, _1byte_arg_regs[i++]);
            } else if (param->type->size == 2) {
                printf("  mov [rbp-%d], %s\n", param->offset, _2byte_arg_regs[i++]);
            } else if (param->type->size == 4) {
                printf("  mov [rbp-%d], %s\n", param->offset, _4byte_arg_regs[i++]);
            } else {
                assert(param->type->size == 8);
                printf("  mov [rbp-%d], %s\n", param->offset, _8byte_arg_regs[i++]);
            }
        }

        for (Node *stmt = f->body; stmt; stmt = stmt->next)
            gen(stmt);

        // エピローグ
        printf("mov rsp, rbp\n");
        printf("pop rbp\n");
        printf("ret\n");
    }
}

// コード生成
void codegen(Program *program) {
    printf(".intel_syntax noprefix\n");
    emit_data(program);
    emit_text(program);
}
