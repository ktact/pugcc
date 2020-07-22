#include "pugcc.h"

// ラベルの通し番号
static int label_seq_num = 0;
// 関数呼び出しの引数を積むレジスタ
static char *arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };

static void gen(Node *node);

void gen_lval(Node *node) {
    if (node->kind != ND_LVAR && node->kind != ND_DEREF)
        error("代入の左辺値が変数ではありません");

    if (node->kind == ND_DEREF) {
        // アドレスを計算しスタックに積む
        gen(node->lhs);
    } else {
        // 左辺値のアドレスをスタックに積む
        printf("  mov rax, rbp\n");
        printf("  sub rax, %d\n", node->offset);
        printf("  push rax\n");
    }
}

static void gen(Node *node) {
    switch (node->kind) {
    case ND_NUM:
        printf("  push %d\n", node->val);
        return;
    case ND_LVAR:
        gen_lval(node);
        printf("  pop rax\n");
        printf("  mov rax, [rax]\n");
        printf("  push rax\n");
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
        gen(node->lhs);
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

// コード生成
void codegen(Function *program) {
    printf(".intel_syntax noprefix\n");
    for (Function *f = program; f; f = f->next) {
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


