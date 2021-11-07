#include "pugcc.h"

// 指定されたファイルの内容を返す
char *read_file(char *path) {
    // ファイルを開く
    FILE *fp = fopen(path, "r");
    if (!fp)
        error("cannot open %s: %s", path, strerror(errno));

    // ファイル内容を読み込む
    int filemax = 10 * 1024 * 1024;
    char *buf = malloc(filemax);
    size_t size = fread(buf, 1, filemax - 2, fp);
    if (!feof(fp))
        error("%s: file too large", path);

    // ファイルが必ず"\n\0"で終わっているようにする
    if (size == 0 || buf[size - 1] != '\n')
        buf[size++] = '\n';
    buf[size] = '\0';
    fclose(fp);
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "引数の個数が正しくありません\n");
        return 1;
    }

    // トークナイズしてパースする
    filename = argv[1];
    user_input = read_file(filename);
    token = tokenize();
    Program *prog = program();

    codegen(prog);

    return 0;
}
