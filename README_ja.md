# libmodernimage

[English](README.md)

**cwebp**, **gif2webp**, **avifenc** をC関数として呼び出せるスレッドセーフなFFIブリッジライブラリです。単一の `.a` または `.so` をリンクするだけで、CLIバイナリを同梱せずに画像フォーマット変換が可能になります。

## 内包ツール

| ツール | 元ライブラリ | 変換 |
|--------|-------------|------|
| cwebp | libwebp 1.6.0 | PNG/JPEG → WebP |
| gif2webp | libwebp 1.6.0 | GIF → アニメーションWebP |
| avifenc | libavif 1.4.1 + aom 3.13.2 | PNG/JPEG → AVIF |

## クイックスタート

### ビルド

```bash
git clone --recursive https://github.com/user/libmodernimage.git
cd libmodernimage

./scripts/build-deps.sh   # libwebp, libaom, libavif
./scripts/build.sh         # libmodernimage.a, .so/.dylib
```

必要なもの: cmake, ninja, libpng-dev, libjpeg-dev, libgif-dev

### リンク

```bash
# 静的リンク（全依存を含む.a — システムライブラリのみ追加で必要）
cc -o myapp myapp.c -Lbuild -lmodernimage -lpng -ljpeg -lgif -lpthread -lm

# 動的リンク
cc -o myapp myapp.c -Lbuild -lmodernimage -lpthread
```

## API

```c
#include "modernimage.h"

// 1. コンテキスト作成
modernimage_context_t* ctx = modernimage_context_new();

// 2.（任意）ファイルの代わりにメモリ上のデータをstdinとして渡す
modernimage_set_stdin(ctx, png_data, png_size);

// 3. 実行 — CLIと同じ引数
const char* argv[] = {"cwebp", "-q", "80", "-o", "out.webp", "--", "-"};
int rc = modernimage_cwebp(ctx, 7, argv);

// 4. キャプチャされた出力を取得
size_t err_size = modernimage_get_stderr_size(ctx);
char* err = malloc(err_size + 1);
modernimage_copy_stderr(ctx, err, err_size);
err[err_size] = '\0';

// 5. 再利用 or 解放
modernimage_context_reset(ctx);  // 再利用
modernimage_context_free(ctx);   // 完了
```

### 関数一覧

| カテゴリ | 関数 | 説明 |
|---------|------|------|
| ライフサイクル | `modernimage_context_new()` | コンテキスト作成 |
| | `modernimage_context_free(ctx)` | コンテキスト破棄 |
| | `modernimage_context_reset(ctx)` | クリアして再利用 |
| 入力 | `modernimage_set_stdin(ctx, data, size)` | メモリ上のデータをstdinに設定 |
| 実行 | `modernimage_cwebp(ctx, argc, argv)` | cwebp実行 |
| | `modernimage_gif2webp(ctx, argc, argv)` | gif2webp実行 |
| | `modernimage_avifenc(ctx, argc, argv)` | avifenc実行 |
| 出力 | `modernimage_get_stdout_size(ctx)` | キャプチャされたstdoutのサイズ |
| | `modernimage_get_stderr_size(ctx)` | キャプチャされたstderrのサイズ |
| | `modernimage_copy_stdout(ctx, buf, size)` | stdoutを呼び出し側バッファにコピー |
| | `modernimage_copy_stderr(ctx, buf, size)` | stderrを呼び出し側バッファにコピー |
| | `modernimage_get_exit_code(ctx)` | 直前の実行の終了コード |
| 情報 | `modernimage_version()` | ライブラリバージョン文字列 |

### stdin対応

| ツール | 方法 | argv例 |
|--------|------|--------|
| cwebp | `set_stdin` + `"-- -"` | `cwebp -q 80 -o out.webp -- -` |
| avifenc | `set_stdin` + `"--stdin"` | `avifenc -q 60 --input-format png -o out.avif --stdin` |
| gif2webp | 非対応 | ファイルパスを使用 |

## スレッドセーフティ

- グローバルIOミューテックスで全呼び出しを直列化（dup2はプロセス全体に影響するため）
- 各コンテキストは独自の出力バッファを持ち、コンテキスト間の干渉なし
- 異なるコンテキストを使えば複数スレッドからの呼び出しが安全
- テスト済み: 8スレッド x 15回の混合操作でゼロ失敗

## テスト

```bash
./build/test_binary_equiv    # CLIツールとバイト単位で一致することを検証
./build/test_thread_safety   # 並行実行、メモリリーク、出力分離の検証
```

## リリース成果物

[GitHub Release](https://github.com/user/libmodernimage/releases) でプラットフォームごとのアーカイブを提供:

```
libmodernimage-linux-x86_64.tar.gz
libmodernimage-linux-aarch64.tar.gz
libmodernimage-darwin-arm64.tar.gz
libmodernimage-windows-x86_64.tar.gz   # MSYS2 UCRT64
```

各アーカイブの内容:

| ファイル | 説明 |
|---------|------|
| `libmodernimage.a` | fat静的ライブラリ（全依存込み） |
| `libmodernimage.so` / `.dylib` / `.dll` | 動的ライブラリ |
| `modernimage.h` | 公開Cヘッダ |
| `cli-compat.json` | マシンリーダブルなツールバージョン・CLI仕様 |

## cli-compat.json

内包するツールのバージョンとサポートする引数を記述します。下流のバインディングライブラリがプログラム的にargvを構築するために使います。

```json
{
  "libmodernimage_version": "0.2.0",
  "tools": {
    "cwebp": {
      "upstream_version": "1.6.0",
      "stdin_support": { "arg": "-- -" },
      "key_options": { "-q": { "type": "int", "range": [0, 100] }, ... }
    },
    ...
  }
}
```

## アーキテクチャ

各ツールの `main()` 関数を共有ライブラリにコンパイルする仕組み:

1. `#define main modernimage_xxx_main` + `#include "original_source.c"` でmainをリネーム
2. 実行時に `dup2()` でstdin/stdout/stderrをパイプにリダイレクト
3. グローバルミューテックスでfdレベルのリダイレクトをスレッドセーフに
4. キャプチャされた出力はコンテキストごとのバッファに格納

上流のソースコードは一切改変しません。ブリッジファイルがマクロリネームでオリジナルをインクルードします。

## 設計思想

このライブラリはCLI完全互換のローレベルFFIブリッジです。quality指定のみの簡易APIなど、ユースケースに特化した高レベルバインディングは、このライブラリのリリースアセットを取り込む別リポジトリで構築することを想定しています。詳細は [usecase-binding.md](usecase-binding.md) を参照。

## ライセンス

MIT。内包ライブラリはそれぞれのライセンスを保持（libwebp/libavif/libaom: BSD）。
