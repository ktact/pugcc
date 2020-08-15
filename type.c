#include "pugcc.h"

Type *int_type   = &(Type){ INT, 8 };

Type *pointer_to(Type *base_type) {
    Type *ptr = calloc(1, sizeof(Type));
    ptr->kind = PTR;
    ptr->size  = 8;
    ptr->pointer_to = base_type;
    return ptr;
}

Type *array_of_int(int len) {
    Type *array = calloc(1, sizeof(Type));
    array->kind       = ARRAY;
    array->array_size = len;
    array->size       = 8 * array->array_size;
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
    case ND_VAR:
    case ND_FUNCCALL:
    case ND_NUM:
        node->type = int_type;
        break;
    case ND_PTR_ADD:
    case ND_PTR_SUB:
    case ND_ASSIGN:
        node->type = node->lhs->type;
        break;
    case ND_ADDR:
        node->type = pointer_to(node->lhs->type);
        break;
    case ND_DEREF:
        if (node->lhs->type->kind == PTR)
            node->type = node->lhs->type;
        else
            node->type = int_type;
        break;
    }
}