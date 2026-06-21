# ポインタ差分演算（`ND_PTR_DIFF`）が `char *` で誤った値を返す

## 原因

`codegen.c` の `gen_binary()` 内、`ND_PTR_DIFF`（ポインタ同士の引き算）のコード生成において、非配列ポインタの場合の除数が `4` に固定されていた。

```c
case ND_PTR_DIFF:
  printf("  sub rax, rdi\n");
  printf("  cqo\n");
  if (is_array(node->lhs))
    printf("  imul rdi, %d\n", node->lhs->type->base->size);
  else
    printf("  mov rdi, 4\n");  // ← 常に 4 で割っていた
  printf("  idiv rdi\n");
  break;
```

ポインタ差分はアドレスの差をポインタが指す型のサイズで割ることで要素数を求める。`int *` であればサイズが 4 なので偶然正しく動くが、`char *`（サイズ 1）に対して 4 で割ると結果が 1/4 になる。

`tokenize.c` では `p - start`（いずれも `char *`）を使ってトークン長を計算していたため、長さが実際の 1/4 に切り捨てられ、トークン認識が壊れていた。

セルフホスト達成コミット時のワークアラウンドとして `(long)p - (long)start` へのキャストで回避されていた（整数型の減算に変えることで `ND_PTR_DIFF` を経由させないようにしていた）。

## 修正内容（`codegen.c`）

配列・非配列を問わず `node->lhs->type->base->size` を使うよう統一した。

```c
// 修正前
case ND_PTR_DIFF:
  printf("  sub rax, rdi\n");
  printf("  cqo\n");
  if (is_array(node->lhs))
    printf("  imul rdi, %d\n", node->lhs->type->base->size);
  else
    printf("  mov rdi, 4\n");
  printf("  idiv rdi\n");
  break;

// 修正後
case ND_PTR_DIFF:
  printf("  sub rax, rdi\n");
  printf("  cqo\n");
  printf("  mov rdi, %d\n", node->lhs->type->base->size);
  printf("  idiv rdi\n");
  break;
```
