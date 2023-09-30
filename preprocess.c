#include "pugcc.h"

static bool line_starts_with_hash(Token *token) {
  return token->at_beginning_of_line && memcmp(token->str, "#", token->len) == 0 && token->len == 1;
}

Token *preprocess(Token *token) {
  Token head = {};
  Token *cur = &head;

  while (token->kind != TK_EOF) {
    if (!line_starts_with_hash(token)) {
      cur = cur->next = token;
      token = token->next;
      continue;
    }

    token = token->next;

    if (token->at_beginning_of_line)
      continue;

    error_tok(token, "無効なプリプロセッサディレクティブです");
  }

  cur->next = token;

  return head.next;
}
