# libmodernimage & Usecase Binding Architecture

## 概要

2層のリポジトリ構成で、画像変換機能を多言語に展開する。

```
[libmodernimage]          1つ         基礎ライブラリ（本リポジトリ）
       |
       v
[usecase binding]      複数可能        ユースケース特化の多言語バインディング
  e.g. "simplewebp"                    e.g. quality指定だけでWebP/AVIF変換
  e.g. "batch-optimizer"               e.g. ディレクトリ一括最適化
```

libmodernimage は cwebp/gif2webp/avifenc の CLI 互換 FFI ライブラリとして**汎用的に**機能を提供する。
ユースケースバインディングはそれを取り込み、**特定の目的に限定した**簡潔な API を各言語に提供する。

---

## Part 1: libmodernimage（本リポジトリ）に残る責務

### 提供物

| 成果物 | 説明 |
|--------|------|
| `libmodernimage.a` | 全依存を含む fat 静的ライブラリ |
| `libmodernimage.so` / `.dylib` | 動的ライブラリ |
| `modernimage.h` | C ヘッダ（公開 API） |
| `cli-compat.json` | CLI 互換仕様（後述） |

### cli-compat.json — CLI 仕様のマシンリーダブル記述

libmodernimage がどのバージョンのどのツールを内包し、どの引数をサポートするかを構造化して提供する。
ユースケースバインディングはこれを参照して argv を組み立てる。

```jsonc
{
  "libmodernimage_version": "0.2.0",
  "tools": {
    "cwebp": {
      "upstream_version": "1.6.0",
      "binary_equiv_tested": true,
      "stdin_support": { "arg": "-- -", "note": "requires -- before -" },
      "output_arg": "-o",
      "key_options": {
        "-q": { "type": "int", "range": [0, 100], "description": "lossy quality" },
        "-lossless": { "type": "flag", "description": "lossless mode" },
        "-resize": { "type": "int int", "description": "width height" },
        "-m": { "type": "int", "range": [0, 6], "description": "compression method" },
        "-short": { "type": "flag", "description": "short output summary" }
      }
    },
    "gif2webp": {
      "upstream_version": "1.6.0",
      "binary_equiv_tested": true,
      "stdin_support": null,
      "output_arg": "-o",
      "key_options": {
        "-q": { "type": "int", "range": [0, 100] },
        "-lossy": { "type": "flag" },
        "-mixed": { "type": "flag" },
        "-m": { "type": "int", "range": [0, 6] }
      }
    },
    "avifenc": {
      "upstream_version": "1.4.1",
      "aom_version": "3.13.2",
      "binary_equiv_tested": true,
      "stdin_support": { "arg": "--stdin", "requires": "--input-format <fmt>" },
      "output_arg": "-o",
      "key_options": {
        "-q": { "type": "int", "range": [0, 100], "description": "quality (color)" },
        "--qalpha": { "type": "int", "range": [0, 100] },
        "-s": { "type": "int", "range": [0, 10], "description": "speed" },
        "-l": { "type": "flag", "description": "lossless" },
        "-j": { "type": "int", "description": "jobs/threads" }
      }
    }
  }
}
```

### GitHub Actions（本リポジトリに必要なもの）

#### 1. `build.yml` — クロスプラットフォームビルド

```
trigger: push to main, tags v*
matrix:
  - linux-x86_64   (ubuntu runner)
  - linux-aarch64  (ubuntu-arm runner or cross-compile)
  - darwin-arm64   (macos-14 runner)
  - darwin-x86_64  (macos-13 runner)

steps:
  - checkout with submodules (recursive)
  - build deps (libwebp, libaom, libavif)
  - build libmodernimage (.a, .so/.dylib)
  - run test_binary_equiv
  - run test_thread_safety
  - upload artifacts: libmodernimage.{a,so/dylib}, modernimage.h, cli-compat.json
```

#### 2. `release.yml` — タグ時のリリース

```
trigger: tags matching v*_libwebp-*_libavif-*
steps:
  - download build artifacts (all platforms)
  - create GitHub Release
  - attach per-platform archives:
      libmodernimage-v0.2.0-linux-x86_64.tar.gz
      libmodernimage-v0.2.0-darwin-arm64.tar.gz
      ...
    各アーカイブの中身:
      libmodernimage.a
      libmodernimage.so (or .dylib)
      modernimage.h
      cli-compat.json
```

#### 3. `update-deps.yml` — 依存ライブラリ更新の半自動化

```
trigger: workflow_dispatch (手動) or schedule (週次)
steps:
  - check libwebp/libavif の最新タグ
  - submodule を更新
  - ビルド + テスト
  - 差分があれば PR を作成
    title: "deps: libwebp 1.6.0 → 1.7.0, libavif 1.4.1 → 1.5.0"
```

### タグ規則

```
v{api_version}_libwebp-{webp_version}_libavif-{avif_version}

例: v0.2.0_libwebp-1.6.0_libavif-1.4.1
```

- `api_version`: API 互換性 (semver)。ヘッダ変更時にバンプ
- `libwebp-*` / `libavif-*`: 内包するツールのバージョン。出力が変わりうる

---

## Part 2: ユースケースバインディングの設計

### 位置づけ

```
libmodernimage (GitHub Release)
    ↓ ダウンロード / git submodule / パッケージマネージャ
usecase-binding リポジトリ
    ├── spec/          ユースケース仕様
    ├── bindings/      各言語の実装
    └── golden/        期待出力
```

libmodernimage の**消費者**であり、libmodernimage を直接フォークや改変はしない。

### リポジトリ構成例（モノレポ）

```
simpleimage/                         # ユースケース名
├── spec/
│   ├── spec.json                    # ユースケース仕様定義
│   ├── fixtures/                    # テスト入力画像
│   │   ├── photo.jpg
│   │   ├── logo.png
│   │   └── animation.gif
│   └── golden/                      # 期待される出力
│       ├── photo_webp_q80.webp
│       ├── photo_webp_q80.json      # メタ情報
│       ├── photo_avif_q60.avif
│       ├── error_corrupt_input.json
│       └── ...
│
├── core/                            # libmodernimage の取り込み
│   ├── download.sh                  # GH Release から取得
│   └── platforms/
│       ├── linux-x86_64/
│       │   ├── libmodernimage.a
│       │   ├── libmodernimage.so
│       │   └── modernimage.h
│       ├── darwin-arm64/
│       └── ...
│
├── bindings/
│   ├── go/
│   │   ├── simpleimage.go           # Go API
│   │   ├── simpleimage_test.go      # golden テスト
│   │   └── go.mod
│   ├── python/
│   │   ├── simpleimage/
│   │   │   ├── __init__.py
│   │   │   └── _ffi.py              # ctypes/cffi ラッパー
│   │   ├── tests/
│   │   └── pyproject.toml
│   ├── ruby/
│   │   ├── lib/simpleimage.rb
│   │   ├── spec/
│   │   └── simpleimage.gemspec
│   ├── node/
│   │   ├── src/index.ts
│   │   ├── test/
│   │   └── package.json
│   └── rust/
│       ├── src/lib.rs
│       └── Cargo.toml
│
├── scripts/
│   ├── generate-golden.sh           # spec/golden/ を再生成
│   ├── update-core.sh               # libmodernimage の新バージョン取り込み
│   └── validate-spec.sh             # spec.json のスキーマ検証
│
└── .github/workflows/
    ├── golden.yml                   # golden 再生成 + diff チェック
    ├── test-bindings.yml            # 全言語 golden テスト
    └── release.yml                  # 各言語パッケージの公開
```

### spec/spec.json — ユースケース仕様

```jsonc
{
  "name": "simpleimage",
  "description": "Quality-only image format conversion",
  "requires_libmodernimage": ">=0.2.0",

  "conversions": {
    "to_webp": {
      "tool": "cwebp",
      "input": "stdin",
      "output": "file",
      "user_params": {
        "quality": { "type": "int", "min": 0, "max": 100, "default": 80 }
      },
      "fixed_argv": ["cwebp", "-q", "{quality}", "-m", "6", "-o", "{output}", "--", "-"],
      "lossless_argv": ["cwebp", "-lossless", "-m", "6", "-o", "{output}", "--", "-"]
    },
    "to_webp_anim": {
      "tool": "gif2webp",
      "input": "tempfile",
      "output": "file",
      "user_params": {
        "quality": { "type": "int", "min": 0, "max": 100, "default": 80 }
      },
      "fixed_argv": ["gif2webp", "-q", "{quality}", "-m", "6", "{input}", "-o", "{output}"]
    },
    "to_avif": {
      "tool": "avifenc",
      "input": "stdin",
      "output": "file",
      "user_params": {
        "quality": { "type": "int", "min": 0, "max": 100, "default": 60 }
      },
      "fixed_argv": ["avifenc", "-q", "{quality}", "-s", "6", "--input-format", "png", "-o", "{output}", "--stdin"]
    }
  },

  "result_schema": {
    "data": "bytes",
    "format": "string",
    "exit_code": "int",
    "error": "string|null"
  }
}
```

### 各言語の統一 API

```python
# Python
result = simpleimage.to_webp(input_bytes, quality=80)
result.data        # bytes: 変換後バイナリ
result.format      # "webp"
result.exit_code   # 0
result.error       # None or エラーメッセージ
```

```go
// Go
result, err := simpleimage.ToWebP(inputBytes, simpleimage.Options{Quality: 80})
result.Data       // []byte
result.Format     // "webp"
result.ExitCode   // 0
```

```ruby
# Ruby
result = SimpleImage.to_webp(input_bytes, quality: 80)
result.data       # String (binary)
result.format     # "webp"
result.exit_code  # 0
result.error      # nil or String
```

### golden テストの流れ（全言語共通）

```
1. spec/fixtures/ から入力を読む
2. spec/spec.json からパラメータとテンプレートを読む
3. 自言語の API で変換実行
4. 結果を spec/golden/*.json と照合:
   - exit_code の一致
   - output_size の一致
   - SHA256 の一致（バイナリ完全一致を保証）
5. エラーケースも同様に照合
```

### libmodernimage 更新時のフロー

```
libmodernimage が v0.3.0_libwebp-1.7.0_libavif-1.5.0 をリリース
    ↓
usecase-binding の scripts/update-core.sh を実行
    ↓
新しい .a/.so/.h をダウンロード
    ↓
scripts/generate-golden.sh で golden を再生成
    ↓
diff があれば確認:
  - 出力サイズの微小変化 → libwebp/libavif のエンコーダ改善（想定内）
  - golden を更新し、各言語テストも更新
  - CHANGELOG に記載
    ↓
全言語テスト通過 → タグ + 各言語パッケージ公開
```

---

## まとめ: 2リポジトリの責務分担

| | libmodernimage (本リポジトリ) | usecase-binding (別リポジトリ) |
|---|---|---|
| **目的** | CLI互換FFIライブラリ | ユースケース特化の多言語バインディング |
| **提供物** | .a, .so, .h, cli-compat.json | 各言語パッケージ (pip, gem, npm, crate, go module) |
| **API粒度** | argc/argv そのまま（CLI完全互換） | quality等の最小パラメータのみ |
| **言語** | C のみ | Go, Python, Ruby, Node, Rust, ... |
| **テスト** | バイナリ一致 + スレッドセーフ | golden 照合（全言語共通仕様） |
| **バージョニング** | v{api}\_libwebp-{ver}\_libavif-{ver} | semver (e.g. v1.2.0) |
| **独立性** | 単体で完結 | libmodernimage に依存 |
| **数** | 1つ | 複数作れる（用途別） |
