#include "pugcc.h"

// ===========================================================================
// データ構造
// ===========================================================================
//
// [Hideset] 再帰展開防止用の展開済みマクロ名の集合
//
//   既に展開したマクロと同名のマクロが再度現れた場合に展開をスキップするために
//   展開済みマクロを記録するためのデータ構造
//
//   例: #define A (1 + A)
//
//     A を展開すると repl は (1 + A) になる。
//     repl の各トークンには hideset = {"A"} が付与されるため、
//     repl 内の A は再展開されない。
//
//     {"A"} -> {"B"} -> NULL
//
// [Macro] マクロ定義のリスト（グローバル変数 macros が先頭）
//
//   新しい定義ほどリスト先頭に追加される。
//   同名の再定義は先頭エントリが優先されるため自動的に上書き相当になる。
//
//     macros -> [name="TRUE" repl=1->EOF] -> [name="NULL" repl=0->EOF] -> NULL
//
// [Token 列]
//
//   各トークンは next ポインタで連結し TK_EOF で終端する。
//
//     [TK_IDENT "FOO"] -> [TK_PUNCT "+"] -> [TK_NUM "1"] -> [TK_EOF]
//
// ===========================================================================

static Macro *macros;

// グローバルリスト macros を先頭から探索し token と同名のエントリを返す。
// 見つからなければ NULL を返す。
static Macro *find_macro(Token *token) {
  for (Macro *m = macros; m; m = m->next)
    if (m->len == token->len && memcmp(m->name, token->str, token->len) == 0)
      return m;

  return NULL;
}

// マクロをグローバルリストの先頭に登録する。
// 再定義時は新エントリが先頭に来るため find_macro が新しい定義を優先することになる。
//
//   登録前: macros -> [NULL=0] -> NULL
//   登録後: macros -> [NULL=1] -> [NULL=0] -> NULL
//                      ^^ find_macro はこちらを返す
static void add_macro(Token *name, Token *repl) {
  Macro *m = calloc(1, sizeof(Macro));
  m->name = name->str;
  m->len  = name->len;
  m->repl = repl;
  m->next = macros;
  macros  = m;
}

// Hideset を線形探索して名前 s（長さ len）が含まれるか返す。
static bool hideset_contains(Hideset *hs, char *s, int len) {
  for (; hs; hs = hs->next)
    if (strlen(hs->name) == len && memcmp(hs->name, s, len) == 0)
      return true;

  return false;
}

// 2 つの Hideset を結合して新しい Hideset を返す。
//
//   hs1 の要素をコピーし末尾に hs2 を連結する。
//
//   hs1: {"A"} -> {"B"} -> NULL
//   hs2: {"C"} -> NULL
//
//   戻り値: {"A"} -> {"B"} -> {"C"} -> NULL
//           ^^^^^^^^^^^^^^^^^^
static Hideset *hideset_union(Hideset *hs1, Hideset *hs2) {
  Hideset head = {};
  Hideset *cur = &head;
  for (; hs1; hs1 = hs1->next) {
    cur = cur->next = calloc(1, sizeof(Hideset));
    cur->name = hs1->name;
  }
  cur->next = hs2;
  return head.next;
}

// トークン列の各トークンに Hideset を付与したコピーを返す。
// TK_EOF 自体はコピーせず共有する。
//
//   入力例: [A(hs={})] -> [1(hs={})] -> [EOF]   hs = {"A"}
//   出力例: [A(hs={"A"})] -> [1(hs={"A"})] -> [EOF]
static Token *hsadd(Hideset *hs, Token *token) {
  Token head = {};
  Token *cur = &head;
  for (; token->kind != TK_EOF; token = token->next) {
    Token *t = calloc(1, sizeof(Token));
    *t = *token;
    t->hideset = hideset_union(t->hideset, hs);
    cur = cur->next = t;
  }
  cur->next = token;
  return head.next;
}

static bool line_starts_with_hash(Token *token) {
  return token->at_beginning_of_line && memcmp(token->str, "#", token->len) == 0 && token->len == 1;
}

// 2 つのトークン列を連結する。
// t1 の TK_EOF を t2 の先頭に置き換える形で繋ぐ。
// t1 が TK_EOF のみなら t2 をそのまま返す。
//
//   t1: [X] -> [Y] -> [EOF]
//   t2: [Z] -> [EOF]
//
//   戻り値: [X] -> [Y] -> [Z] -> [EOF]
static Token *append(Token *t1, Token *t2) {
  if (t1->kind == TK_EOF)
    return t2;

  Token head = {};
  Token *cur = &head;
  for (; t1->kind != TK_EOF; t1 = t1->next)
    cur = cur->next = t1;
  cur->next = t2;
  return head.next;
}

// ソースコード中の着目行の残りのトークンをコピーし TK_EOF で終端させる。
// また、*rest には次行の先頭トークンをセットする。
//
//   入力 ('|' が行境界):  [NULL] -> [0] -> | [int] -> ...
//                                           ^ at_beginning_of_line=true
//
//   *rest:   [int] -> ...
//   戻り値:  [NULL(copy)] -> [0(copy)] -> [TK_EOF(新規)]
static Token *copy_line(Token **rest, Token *token) {
  Token head = {};
  Token *cur = &head;
  for (; !token->at_beginning_of_line && token->kind != TK_EOF; token = token->next) {
    Token *t = calloc(1, sizeof(Token));
    *t = *token;
    cur = cur->next = t;
  }

  Token *eof = calloc(1, sizeof(Token));
  eof->kind = TK_EOF;
  cur->next = eof;
  *rest = token;

  return head.next;
}

// トークン token がオブジェクト形式マクロなら展開し true を返す。
//
//   展開しない条件:
//     1. token の hideset に自身の名前が含まれる（再帰展開防止）
//     2. マクロ定義が存在しない
//
//   展開手順（例: #define FOO (1+2)、token = FOO）:
//
//     1. hs = token.hideset U {"FOO"}
//
//     2. repl のコピーに hs を付与:
//          [(hs={"FOO"})] -> [1] -> [+] -> [2] -> [EOF]
//
//     3. repl コピーの後ろに token->next を append:
//          *rest = [(] -> [1] -> [+] -> [2] -> [tok->next ...]
//
//     4. true を返す -> preprocess ループが *rest から再スキャン
//
//   repl 内の FOO は hideset に {"FOO"} を持つため再展開されない。
static bool expand_macro(Token **rest, Token *token) {
  if (hideset_contains(token->hideset, token->str, token->len))
    return false;

  Macro *m = find_macro(token);
  if (!m) return false;

  Hideset *hs = calloc(1, sizeof(Hideset));
  hs->name = strndup(token->str, token->len);
  hs->next = token->hideset;
  *rest = append(hsadd(hs, m->repl), token->next);

  return true;
}

// トークン列を走査してプリプロセッサを処理する。
//
//   [マクロ展開]
//     TK_IDENT かつ展開可能なら expand_macro を呼び、
//     展開後のトークン列から再スキャンする。
//     展開結果にさらにマクロが含まれていても再展開される。
//
//   [ディレクティブ以外]
//     そのまま出力リストに繋ぐ。
//
//   [# ディレクティブ]
//
//     null directive  （# の直後が次行の先頭）
//       -> 何もしない
//
//     #define NAME repl...
//       -> copy_line でReplacement Listを取り込み add_macro で登録
//
//          # -> define -> NAME -> repl... -> [次行]
//               ^^^^^^^^ token はここから処理
//
//     それ以外 -> エラー
Token *preprocess(Token *token) {
  Token head = {};
  Token *cur = &head;

  while (token->kind != TK_EOF) {
    if (token->kind == TK_IDENT && expand_macro(&token, token))
      continue;

    if (!line_starts_with_hash(token)) {
      cur = cur->next = token;
      token = token->next;
      continue;
    }

    token = token->next;

    // null directive
    if (token->at_beginning_of_line)
      continue;

    // #define
    if (token->len == 6 && memcmp(token->str, "define", 6) == 0) {
      token = token->next;
      Token *name = token;
      token = token->next;
      Token *repl = copy_line(&token, token);
      add_macro(name, repl);
      continue;
    }

    error_tok(token, "無効なプリプロセッサディレクティブです");
  }

  cur->next = token;

  return head.next;
}
