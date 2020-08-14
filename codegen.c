#include "pugcc.h"

// ラベルの通し番号
static int label_seq_num = 0;
// 関数呼び出しの引数を積むレジスタ
static char *arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };

static void gen(Node *node);

void gen_lval(Node *node) {
    if (node->kind != ND_VAR && node->kind != ND_DEREF)
        error("代入の左辺値が変数ではありません");

    if (node->kind == ND_DEREF) {
        // アドレスを計算しスタックに積む
        gen(node->lhs);
    } else {
        // 左辺値のアドレスをスタックに積む
        Var *var = node->var;
        if (var->is_local) {
            printf("  mov rax, rbp\n");
            printf("  sub rax, %d\n", node->offset);
            printf("  push rax\n");
        } else {
            printf("  push offset %s\n", var->name);
        }
    }
}

static void gen(Node *node) {
    switch (node->kind) {
    case ND_NUM:
        printf("  push %d\n", node->val);
        return;
    case ND_VAR:
        gen_lval(node);

        if (!is_array(node) && node->var->is_local)
        {
            printf("  pop rax\n");
            printf("  mov rax, [rax]\n");
            printf("  push rax\n");
        }
        return;
    case ND_ASSIGN:
        gen_lval(node->lhs);
        gen(node->rhs);

        printf("  pop rdi\n");
        printf("  pop rax\n");
        printf("  mov [rax], rdi\n");
        printf("  push rdi\n");
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
            printf("  pop %s\n", arg_regs[i]);
        }
        printf("  call %s\n", node->funcname);
        printf("  push rax\n");
        return;
    }
    case ND_ADDR:
        gen_lval(node->lhs);
        return;
    case ND_DEREF:
        // 変数のアドレスをスタックに積む
        gen(node->lhs);

        // アドレスに格納された値をスタックに積む
        printf("  pop rax\n");
        printf("  mov rax, [rax]\n");
        printf("  push rax\n");
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
        printf("  imul rdi, 4\n");
        printf("  add rax, rdi\n");
        break;
    case ND_PTR_SUB:
        printf("  imul rdi, 4\n");
        printf("  sub rax, rdi\n");
        break;
    case ND_PTR_DIFF:
        // アドレスの値を引いた後にそのポインタが指す変数の型のバイト数で割る
        printf("  sub rax, rdi\n");
        printf("  cqo\n");
        printf("  mov rdi, 4\n"); // 暫定
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
    for (Var *global_var = program->global_variables; global_var; global_var = global_var->next) {
        printf("%s:\n", global_var->name);
        printf("  .zero %d\n", global_var->type->size);
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
        for (Var *param = f->params; param; param = param->next)
            printf("  mov [rbp-%d], %s\n", param->offset, arg_regs[i++]);

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
