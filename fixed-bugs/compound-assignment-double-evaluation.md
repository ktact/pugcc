# 複合代入の式値が二重に演算される（`i += 5` が 7 ではなく 12 になる）

## 原因

`codegen.c` の `gen()` 関数において、複合代入（`ND_ADD_EQ` など）の case が `return` ではなく `break` で終わっていた。

`gen()` の switch 文を抜けた後にはバイナリ演算ノード向けの共通処理が続いている。

```c
  // switch の後
  gen(node->lhs);
  gen(node->rhs);
  gen_binary(node);
```

`break` で switch を脱出するとこの共通処理まで実行されてしまう。結果として：

1. case 内の処理（gen_lval → load → gen(rhs) → gen_binary → store）で演算・代入が行われる
2. switch 脱出後、`gen(lhs)` で更新済みの値、`gen(rhs)` で右辺値が再度スタックに積まれる
3. `gen_binary()` が再実行され、式値がもう一度演算される

例: `int i=2; i+=5`
- step 1 で i に 7 が格納され、スタックに 7 が積まれる
- step 2/3 で 7（i の新しい値）と 5 が積まれ、再び加算されて 12 がスタックに残る
- 式値として 12 が返される（正しくは 7）

`ND_BITAND_EQ` など同じく `break` を使っていたビット演算系も同じ構造的バグを抱えていたが、冪等性（`(a & b) & b == a & b`）などにより式値が偶然正しい値になるケースが多く、バグとして顕在化していなかった。

## 修正内容（`codegen.c`）

複合代入の case 末尾を `break` から `return` に変更した。

```c
// 修正前
    case ND_ADD_EQ:
    // ... 他の複合代入ケース ...
      gen_lval(node->lhs);
      printf("  push [rsp]\n");
      load(node->lhs->type);
      gen(node->rhs);
      gen_binary(node);
      store(node->type);
      break;  // ← switch 脱出後の共通処理も実行されてしまう

// 修正後
      store(node->type);
      return;  // ← 関数から抜けて共通処理を実行しない
```
