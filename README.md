# JEBC — JavaScript/TypeScript + Emscripten Bundled Compiler

> C++ と JS/TS/TSX を1つの `.jebc` ファイルに。コマンド一発で WebAssembly ビルド。

[日本語は下にあります / Japanese below]

---

## English

JEBC is a zero-config compiler that bundles C/C++ and JS/TS into a single WebAssembly distribution.

### Repository Structure

This repository is organized into three distribution formats inside the `release/` folder:

```
jebc/
├── release/
│   ├── npm/         # For npm distribution (npm install -g jebc)
│   ├── exe/         # Standalone jebc.exe for Windows
│   └── source/      # Source code for building JEBC itself (C++20)
├── README.md
└── LICENSE
```

### Installation

**Option 1: npm (Recommended)**
```bash
npm install -g jebc
```

**Option 2: Standalone Executable**
1. Download `jebc.exe` from the `release/exe/` folder.
2. Run `setup.bat` to install Emscripten and esbuild.

### Usage

```bash
jebc app.jebc
```

For full documentation and examples, see the `docs/` folder inside any of the release distributions.

---

## 日本語

### リポジトリ構成

`release/` フォルダ下に3つの配布形態を用意しています。

```
jebc/
├── release/
│   ├── npm/         # npm配布用パッケージ (npm install -g jebc)
│   ├── exe/         # Windows向け .exe 単体配布パッケージ
│   └── source/      # JEBC本体の開発用ソースコード配付パッケージ
├── README.md
└── LICENSE
```

### インストール

**方法1: npm（推奨）**
```bash
npm install -g jebc
```

**方法2: .exe 単体ダウンロード**
1. `release/exe/` フォルダから `jebc.exe` と `setup.bat` をダウンロード。
2. `setup.bat` を実行し、Emscripten と esbuild を自動セットアップ。

### 使い方

```bash
jebc app.jebc
```

機能の詳細やV2の新機能（`.jebc` 拡張子、`call` ディレクティブなど）については、各パッケージ内の `docs/` フォルダにあるマニュアルをご覧ください。

---

## License

[MIT](LICENSE)

© 2026 userkunngakkou
