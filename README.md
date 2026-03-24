# JELLL — JavaScript/TypeScript + Emscripten Bundled Compiler

> C++ と JS/TS/TSX を1つの `.jelll` ファイルに。コマンド一発で WebAssembly ビルド。

[日本語は下にあります / Japanese below]

---

## English

JELLL is a zero-config compiler that bundles C/C++ and JS/TS into a single WebAssembly distribution.

### Repository Structure

This repository is organized into three distribution formats inside the `release/` folder:

```
jelll/
├── release/
│   ├── npm/         # For npm distribution (npm install -g jelll)
│   ├── exe/         # Standalone jelll.exe for Windows
│   └── source/      # Source code for building JELLL itself (C++20)
├── README.md
└── LICENSE
```

### Installation

**Option 1: npm (Recommended)**
```bash
npm install -g jelll
```

**Option 2: Standalone Executable**
1. Download `jelll.exe` from the `release/exe/` folder.
2. Run `setup.bat` to install Emscripten and esbuild.

### Usage

```bash
jelll app.jelll
```

For full documentation and examples, see the `docs/` folder inside any of the release distributions.

---

## 日本語

### リポジトリ構成

`release/` フォルダ下に3つの配布形態を用意しています。

```
jelll/
├── release/
│   ├── npm/         # npm配布用パッケージ (npm install -g jelll)
│   ├── exe/         # Windows向け .exe 単体配布パッケージ
│   └── source/      # JELLL本体の開発用ソースコード配付パッケージ
├── README.md
└── LICENSE
```

### インストール

**方法1: npm（推奨）**
```bash
npm install -g jelll
```

**方法2: .exe 単体ダウンロード**
1. `release/exe/` フォルダから `jelll.exe` と `setup.bat` をダウンロード。
2. `setup.bat` を実行し、Emscripten と esbuild を自動セットアップ。

### 使い方

```bash
jelll app.jelll
```

機能の詳細やV2の新機能（`.jelll` 拡張子、`call` ディレクティブなど）については、各パッケージ内の `docs/` フォルダにあるマニュアルをご覧ください。

---

## License

[MIT](LICENSE)

© 2026 userkunngakkou
