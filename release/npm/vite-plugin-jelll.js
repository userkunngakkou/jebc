import { execSync } from 'child_process';
import path from 'path';
import fs from 'fs';

/**
 * vite-plugin-jelll
 * 
 * Vite (React, Vue, Svelte等) 用のプラグイン。
 * import myModule from './logic.jelll' を可能にします。
 */
export default function jelllVitePlugin() {
  return {
    name: 'vite-plugin-jelll',
    enforce: 'pre',
    async transform(code, id) {
      if (!id.endsWith('.jelll')) return null;

      // 1. 一時ビルド環境の用意
      const cacheDir = path.resolve(process.cwd(), '.jelll_cache');
      if (!fs.existsSync(cacheDir)) fs.mkdirSync(cacheDir, { recursive: true });

      const basename = path.basename(id);
      const tempPath = path.join(cacheDir, basename);
      fs.writeFileSync(tempPath, code);

      try {
        // 2. JELLLコンパイル実行
        execSync(`npx jelll "${tempPath}"`, { stdio: 'inherit', cwd: cacheDir });
        
        const distDir = path.join(cacheDir, 'dist');
        const bundlePath = path.join(distDir, 'bundle.js');
        const wasmPath = path.join(distDir, 'native.wasm');

        if (!fs.existsSync(bundlePath) || !fs.existsSync(wasmPath)) {
          throw new Error('JELLL compilation failed to produce output.');
        }

        let jsCode = fs.readFileSync(bundlePath, 'utf8');
        const wasmBuffer = fs.readFileSync(wasmPath);

        // 3. Wasmアセットの発行
        const referenceId = this.emitFile({
          type: 'asset',
          name: path.basename(id, '.jelll') + '.wasm',
          source: wasmBuffer
        });

        // 4. JS側のフェッチURLをVite固有のアセット解決記法に書き換え
        jsCode = jsCode.replace(
          /new URL\(['"][^'"]+native\.wasm['"],\s*import\.meta\.url\)/g,
          `import.meta.ROLLUP_FILE_URL_${referenceId}`
        );

        return {
          code: jsCode,
          map: null
        };

      } catch (err) {
        this.error(`JELLL Compiler Error: ${err.message}`);
      }
    }
  };
}
