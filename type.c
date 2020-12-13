#include "pugcc.h"

Type *int_type  = &(Type){ INT,  4, 4 };
Type *char_type = &(Type){ CHAR, 1, 1 };

int align_to(int n, int align) {
    return (n + align -1) & ~(align - 1);
}

Type *pointer_to(Type *base_type) {
    Type *ptr = calloc(1, sizeof(Type));
    ptr->base = base_type;
    ptr->kind = PTR;
    ptr->size  = 8;
    ptr->align = 8;
    ptr->pointer_to = base_type;
    return ptr;
}

Type *array_of(Type *base_type, int len) {
    Type *array = calloc(1, sizeof(Type));
    array->base       = base_type;
    array->kind       = ARRAY;
    array->array_size = len;
    array->size       = base_type->size * len;
    array->align      = base_type->align;
    return array;
}

bool is_pointer(Node *node) {
    return node->kind == ND_VAR && node->type->kind == PTR;
}

bool is_array(Node *node) {
    return node->kind == ND_VAR && node->type->kind == ARRAY;
}

void add_type(Node *node) {
    if (node == NULL || node->type)
        return;

    add_type(node->lhs);
    add_type(node->rhs);
    add_type(node->cond);
    add_type(node->then);
    add_type(node->els);
    add_type(node->init);
    add_type(node->inc);

    for (Node *n = node->body; n; n = n->next)
        add_type(n);
    for (Node *n = node->args; n; n = n->next)
        add_type(n);

    switch (node->kind) {
    case ND_ADD:
    case ND_SUB:
    case ND_PTR_DIFF:
    case ND_MUL:
    case ND_DIV:
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
        node->type = int_type;
        break;
    case ND_VAR:
        node->type = node->var->type;
        break;
    case ND_MEMBER:
        node->type = node->member->type;
        break;
    case ND_FUNCCALL:
    case ND_NUM:
        node->type = int_type;
        break;
    case ND_PTR_ADD:
    case ND_PTR_SUB:
    case ND_ASSIGN:
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
        node->type = node->lhs->type;
        break;
    case ND_ADDR: {
        Type *type = node->lhs->type;
        if (type->kind == ARRAY)
            node->type = pointer_to(type->base);
        else
            node->type = pointer_to(type);
        }
        break;
    case ND_DEREF:
        if (node->lhs->type->kind == PTR)
            node->type = node->lhs->type->pointer_to;
        else
            node->type = node->lhs->type->base;
        break;
    case ND_GNU_STMT_EXPR: {
            Node *last = node->body;
            while (last->next)
                last = last->next;
            node->type = last->type;
        }
        break;
    }
}
