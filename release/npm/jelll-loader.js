const { execSync } = require('child_process');
const path = require('path');
const fs = require('fs');

/**
 * jelll-loader
 * 
 * Webpack / Next.js 用のカスタムローダー。
 * .jelll ファイルを検知してコンパイルし、成果物（JS + WASM）を
 * バンドラーのモジュールグラフに自動統合します。
 */
module.exports = function(source) {
  const callback = this.async();
  
  // 1. キャッシュフォルダの設定
  const cacheDir = path.resolve(process.cwd(), '.jelll_cache');
  if (!fs.existsSync(cacheDir)) fs.mkdirSync(cacheDir, { recursive: true });

  const basename = path.basename(this.resourcePath);
  const tempPath = path.join(cacheDir, basename);
  fs.writeFileSync(tempPath, source);

  try {
    // 2. JELLL 本体によるコンパイル実行
    // cwd をキャッシュディレクトリにして出力を局所化
    execSync(`npx jelll "${tempPath}"`, { stdio: 'inherit', cwd: cacheDir });
    
    const distDir = path.join(cacheDir, 'dist');
    const bundlePath = path.join(distDir, 'bundle.js');
    const wasmPath = path.join(distDir, 'native.wasm');

    if (!fs.existsSync(bundlePath) || !fs.existsSync(wasmPath)) {
      return callback(new Error('JELLL: 成果物(bundle.js または native.wasm)の生成に失敗しました。'));
    }

    let jsCode = fs.readFileSync(bundlePath, 'utf8');
    const wasmBuffer = fs.readFileSync(wasmPath);

    // 3. WasmファイルをWebpackの出力アセットとして登録
    const wasmFilename = path.basename(this.resourcePath, '.jelll') + '_' + Date.now() + '.wasm';
    this.emitFile(wasmFilename, wasmBuffer);

    // 4. JS側のWasm読み込みパスをアセットURLに置換
    jsCode = jsCode.replace(
      /new URL\(['"][^'"]+native\.wasm['"],\s*import\.meta\.url\)/g,
      `new URL(__webpack_public_path__ + "${wasmFilename}", import.meta.url)`
    );

    callback(null, jsCode);
  } catch (err) {
    callback(err);
  }
};
