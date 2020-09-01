#include "pugcc.h"

// 新しいトークンを作成してcurに繋げる
Token *new_token(TokenKind kind, Token *cur, char *str, int len) {
    Token *tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->str  = strndup(str, len);
    tok->len  = len;
    cur->next = tok;
    return tok;
}

// エラーを報告するための関数
// printfと同じ引数を取る
void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

// エラー箇所を報告する
void error_at(char *loc, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    // locが含まれている行の開始地点と終了地点を取得
    char *line = loc;
    while (user_input < line && line[-1] != '\n')
        line--;

    char *end = loc;
    while (*end != '\n')
        end++;

    // 見つかった行が全体の何行目なのか調べる
    int line_num = 1;
    for (char *p = user_input; p < line; p++)
        if (*p == '\n')
            line_num++;

    // 見つかった行を、ファイル名と行番号と一緒に表示
    int indent = fprintf(stderr, "%s:%d: ", filename, line_num);
    fprintf(stderr, "%.*s\n", (int)(end - line), line);

    // エラー箇所を"^"で指し示して、エラーメッセージを表示
    int pos = loc - line + indent;
    fprintf(stderr, "%*s", pos, "");
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");

    exit(1);
}

// 次のトークンが期待している記号の場合にはトークンを1つ読み進めて真を返す。
// それ以外の場合には偽を返す。
bool consume(char *op) {
    if (token->kind != TK_RESERVED || strlen(op) != token->len ||
        memcmp(token->str, op, token->len))
        return false;
    token = token->next;
    return true;
}

// 次のトークンが期待する記号の場合にはそのトークンを返す。
// それ以外の場合にはNULLを返す。
Token *peek(char *s) {
    if (token->kind != TK_RESERVED || strlen(s) != token->len ||
        memcmp(token->str, s, token->len))
        return NULL;
    return token;
}

// 次のトークンが識別子であればトークンを1つ読み進めそのトークンの文字列を返す。
// それ以外の場合にはNULLを返す。
char *consume_ident() {
    if (token->kind != TK_IDENT)
        return NULL;
    Token *t = token;
    token = token->next;
    return strndup(t->str, t->len);
}

// 次のトークンが期待している記号の場合にはトークンを1つ読み進める。
// それ以外の場合にはエラーを報告する。
void expect(char *op) {
    if (token->kind != TK_RESERVED || strlen(op) != token->len ||
        memcmp(token->str, op, token->len))
        error_at(token->str, "'%s'ではありません", op);
    token = token->next;
}

// 次のトークンが数値の場合、トークンを1つ読み進めてその数値を返す。
// それ以外の場合にはエラーを報告する。
int expect_number() {
    if (token->kind != TK_NUM)
        error_at(token->str, "数ではありません");
    int val = token->val;
    token = token->next;
    return val;
}

// 次のトークンが識別子の場合、トークンを1つ読み進めてそのトークンの文字列をを返す。
// それ以外の場合にはエラーを報告する。
char *expect_ident() {
    if (token->kind != TK_IDENT)
        error_at(token->str, "識別子ではありません");
    Token *t = token;
    token = token->next;
    return strndup(t->str, t->len);
}

bool at_eof() {
    return token->kind == TK_EOF;
}

bool startswith(char *p, char *q) {
    return memcmp(p, q, strlen(q)) == 0;
}

static bool is_alpha(char c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

static bool is_alnum(char c) {
    return is_alpha(c) || ('0' <= c && c <= '9');
}

static int reserved_word(char *p) {
    char *keywords[] = { "return", "if", "else", "while", "for", "int", "char", "struct", "sizeof" };
    for (int i = 0; i < sizeof(keywords) / sizeof(*keywords); i++) {
        int len = strlen(keywords[i]);
        if (startswith(p, keywords[i]) && !is_alnum(p[len]))
            return len;
    }
    return 0;
}

// 入力文字列pをトークナイズしてそれを返す
Token *tokenize() {
    char *p = user_input;
    Token head;
    head.next = NULL;
    Token *cur = &head;

    while (*p) {
        // 空白文字をスキップ
        if (isspace(*p)) {
            p++;
            continue;
        }

        // 行コメントをスキップ
        if (strncmp(p, "//", 2) == 0) {
            p += 2;
            while (*p != '\n')
                p++;
            continue;
        }

        // ブロックコメントをスキップ
        if (strncmp(p, "/*", 2) == 0) {
            char *q = strstr(p + 2, "*/");
            if (!q)
                error_at(p, "コメントが閉じられていません");
            p = q + 2;
            continue;
        }

        // 文字列リテラル
        if (*p == '"') {
            char *q = p++;
            char buf[1024];
            int len = 0;
            for (;;) {
                if (len == sizeof(buf))
                    error_at(q, "文字列リテラルが大きすぎます");
                if (*p == '\0')
                    error_at(q, "文字列リテラルの終端がありません");
                if (*p == '"')
                    break;

                if (*p == '\\') {
                    switch (*++p) {
                    case 'a': buf[len++] = '\a'; break;
                    case 'b': buf[len++] = '\b'; break;
                    case 't': buf[len++] = '\t'; break;
                    case 'n': buf[len++] = '\n'; break;
                    case 'v': buf[len++] = '\v'; break;
                    case 'f': buf[len++] = '\f'; break;
                    case 'r': buf[len++] = '\r'; break;
                    case 'e': buf[len++] =   27; break;
                    case '0': buf[len++] = '\0'; break;
                    default:  buf[len++] = *p;   break;
                    }
                } else {
                    buf[len++] = *p;
                }
                p++;
            }
            buf[len++] = '\0';
            p++; // 読み出し位置を、文字列リテラルの終端の"の次の文字にセットする

            cur = new_token(TK_STR, cur, buf, len);
            continue;
        }

        int len = 0;
        if ((len = reserved_word(p)) > 0) {
            cur = new_token(TK_RESERVED, cur, p, len);
            p += len;
            continue;
        }

        // 識別子
        if (is_alpha(*p)) {
            char *q = p++;
            while (is_alnum(*p))
                p++;
            cur = new_token(TK_IDENT, cur, q, p - q);
            continue;
        }

        if (startswith(p, "==") || startswith(p, "!=") ||
            startswith(p, "<=") || startswith(p, ">=")) {
            cur = new_token(TK_RESERVED, cur, p, 2);
            p += 2;
            continue;
        }

        if (strchr("+-*/&(){}<>=;,[].", *p)) {
            cur = new_token(TK_RESERVED, cur, p++, 1);
            continue;
        }

        if (isdigit(*p)) {
            cur = new_token(TK_NUM, cur, p, 0);
            char *q = p;
            cur->val = strtol(p, &p, 10);
            cur->len = p - q;
            continue;
        }

        error_at(p, "不正なトークンです");
    }

    new_token(TK_EOF, cur, p, 0);
    return head.next;
}