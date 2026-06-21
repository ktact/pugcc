# `+=` `-=` `*=` `/=` が演算されず左辺値がそのまま代入される

## 原因

`codegen.c` の `gen_binary()` 関数において、`ND_ADD_EQ` / `ND_PTR_ADD_EQ` / `ND_SUB_EQ` / `ND_PTR_SUB_EQ` / `ND_MUL_EQ` / `ND_DIV_EQ` に対応する `case` が存在しなかった。

複合代入のコード生成（`gen()` 内）は次の流れで進む。

```
gen_lval(lhs)      → 左辺のアドレスをスタックに積む
push [rsp]         → アドレスを複製
load(lhs->type)    → 左辺の現在値をスタックに積む
gen(rhs)           → 右辺の値をスタックに積む
gen_binary(node)   → 演算して結果をスタックに積む ← ここが無処理だった
store(type)        → 結果を左辺に書き戻す
```

`gen_binary()` の switch に該当ケースがないと、`pop rdi` / `pop rax` だけ実行されて何も加算・乗算されず、`push rax` で左辺の元の値がそのまま積まれる。結果として `p += 2` は `p = p` と等価になり、ポインタが進まない。

`ND_BITAND_EQ` / `ND_BITOR_EQ` / `ND_SHL_EQ` などは `gen_binary()` 内で非 `_EQ` 形と `case` を共有する形で実装済みだったため、このバグは加減乗除の複合代入にのみ存在していた。

## 修正内容（`codegen.c`）

各演算ケースに `_EQ` バリアントを fall-through で追加した。

```c
// 修正前
case ND_ADD:
  printf("  add rax, rdi\n");
  break;
case ND_PTR_ADD:
  printf("  imul rdi, %d\n", node->lhs->type->base->size);
  printf("  add rax, rdi\n");
  break;
// ND_ADD_EQ, ND_PTR_ADD_EQ の case が存在しない

// 修正後
case ND_ADD:
case ND_ADD_EQ:
  printf("  add rax, rdi\n");
  break;
case ND_PTR_ADD:
case ND_PTR_ADD_EQ:
  printf("  imul rdi, %d\n", node->lhs->type->base->size);
  printf("  add rax, rdi\n");
  break;
```

`ND_SUB_EQ` / `ND_PTR_SUB_EQ` / `ND_MUL_EQ` / `ND_DIV_EQ` も同様に追加した。
