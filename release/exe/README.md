# JELLL V5 — JavaScript/TypeScript + Emscripten Bundled Compiler

> C/C++・Rust・Zig と JS/TS/TSX を1つの `.jelll` ファイルに。コマンド一発で WebAssembly ビルド。

[日本語は下にあります / Japanese below]

---

## English

JELLL is a zero-config compiler that bundles C/C++, Rust, Zig, and JS/TS into a single WebAssembly distribution.

### Features (V5)
- **Multi-language**: Write C, Rust, Zig, JS, TS, TSX, CSS, HTML in one `.jelll` file
- **One command build**: `jelll app.jelll` — no Makefile, no Webpack
- **Dev server**: `jelll dev app.jelll` — live reload with file watching
- **Auto setup**: `jelll --setup` — interactive multi-select toolchain installer
- **@jelll-sync**: Automatic C struct ↔ TS class synchronization

### Repository Structure

```
jelll/
├── release/
│   ├── npm/         # npm distribution (npm install -g jelll)
│   ├── exe/         # Standalone jelll.exe for Windows
│   └── source/      # Source code (C++17)
├── .testcode/       # Comprehensive feature test suite
├── README.md
└── LICENSE
```

### Installation

**Option 1: npm (Recommended)**
```bash
npm install -g jelll
jelll --setup
```

**Option 2: Standalone Executable**
1. Download `jelll.exe` from the `release/exe/` folder.
2. Run `setup.bat` to install toolchains.

### Usage

```bash
jelll app.jelll          # Build
jelll dev app.jelll      # Dev server with live reload
jelll --setup            # Interactive toolchain installer
```

For full documentation, see `docs/` inside any release distribution.

---

## 日本語

### 特徴 (V5)
- **多言語対応**: C, Rust, Zig, JS, TS, TSX, CSS, HTML を1つの `.jelll` ファイルに
- **ワンコマンドビルド**: `jelll app.jelll` — Makefile も Webpack も不要
- **開発サーバー**: `jelll dev app.jelll` — ファイル監視＋自動リビルド
- **自動セットアップ**: `jelll --setup` — 矢印キーで選ぶ対話型インストーラー
- **@jelll-sync**: C構造体 ↔ TSクラス 自動同期

### インストール

**方法1: npm（推奨）**
```bash
npm install -g jelll
jelll --setup
```

**方法2: .exe 単体ダウンロード**
1. `release/exe/` から `jelll.exe` と `setup.bat` をダウンロード
2. `setup.bat` を実行してツールチェーンを自動セットアップ

### 使い方

```bash
jelll app.jelll          # ビルド
jelll dev app.jelll      # ライブリロード開発サーバー
jelll --setup            # 対話型ツールチェーンインストーラー
```

詳細は各パッケージ内の `docs/` フォルダのマニュアルをご覧ください。

---

## License

[MIT](LICENSE)

© 2026 userkunngakkou
