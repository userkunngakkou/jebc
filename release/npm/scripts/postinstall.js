const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');
const readline = require('readline');

// ── 言語判定 ──
const isJa = Intl.DateTimeFormat().resolvedOptions().locale.startsWith('ja');

// ── 翻訳テーブル ──
const t = isJa ? {
    title: 'JELLL - Toolchain Auto Installer',
    desc1: '必要な開発ツールをデータ容量が小さい順に自動セットアップします。',
    desc2: '(既にインストール済みのツールは自動でスキップされます)',
    envOverride: '[JELLL] 環境変数 JELLL_LANGS (${langs}) に基づき、指定されたツールのみインストールします...',
    npmDetected: '[JELLL] npm install を検出しました。',
    npmAutoMsg: 'コンソールから直接対話プロンプトを表示します。',
    npmFallback: '対話プロンプトが利用できないため、全てのコンパイラを自動インストールします。',
    npmSkipHint: '※ 不要な場合は環境変数 JELLL_LANGS=none を指定してください',
    noTty: '[TTY未検出] 非対話環境のため全てインストールします。',
    promptQ: 'どのコンパイラをインストールしますか？',
    promptDesc: '(※必須の esbuild は自動インストールされます)',
    promptAll: 'すべて (C++, Rust, Zig)',
    promptRecommend: '推奨',
    promptCpp: 'C/C++ (Emscripten)',
    promptRust: 'Rust (wasm32)',
    promptZig: 'Zig',
    promptSkip: 'スキップ (esbuildのみ)',
    promptInput: '番号を入力してください (例: 1)',
    promptInvalid: '無効な入力のため、デフォルト(すべて)をインストールします。',
    timeoutAll: '入力タイムアウト(30秒)により、デフォルトで全てインストールします...',
    noneSelected: '※選択されなかったため、追加のコンパイラはインストールしません。',
    checkEsbuild: 'esbuildを確認中...',
    esbuildOk: 'esbuild はインストール済みです (スキップ)',
    esbuildInstall: 'esbuild をインストールしています...',
    esbuildDone: 'esbuild をインストールしました',
    checkZig: 'Zig (約50MB) を確認中...',
    zigOk: 'Zig はインストール済みです (スキップ)',
    zigInstall: 'Zig をダウンロード＆展開しています...',
    zigExt: 'Zig を解凍しています...',
    zigDone: 'Zig をインストール＆パスを通しました',
    checkRust: 'Rust (約300MB) を確認中...',
    rustOk: 'Rust はインストール済みです (スキップ)',
    rustWasm: 'wasm32 ターゲットを確認しています...',
    rustWasmDone: 'Rust WebAssembly構成を適用しました',
    rustInstall: 'Rust ツールチェーンをダウンロードしています...',
    rustAutoInstall: 'Rust を自動インストールしています...',
    rustAddWasm: 'wasm32 ターゲットを追加しています...',
    rustDone: 'Rust をインストール＆パスを通しました',
    checkCpp: 'Emscripten (約1GB) を確認中...',
    cppOk: 'Emscripten はインストール済みです (スキップ)',
    cppClone: 'Emscripten リポジトリを取得しています...',
    cppExt: 'Emscripten インストーラーを解凍しています...',
    cppDl: 'Emscripten 本体をダウンロード中... (数分〜10分程度かかります)',
    cppAct: 'Emscripten を有効化しています...',
    cppDone: 'Emscripten をインストール＆パスを通しました',
    success: '🎉 [JELLL] 必要な開発ツールの自動セットアップが完了しました！',
    restartWarning: '⚠️  重要: PATHの変更を反映させるため、ターミナル(またはVSCode等)を再起動してください。 ⚠️',
    nextStep: 'その後、 jelll dev main.jelll でWebAssembly実行が可能です。',
    failTitle: '一部のセットアップがタイムアウト・通信エラーで中断されました。',
    failDetail: '❌ [JELLL] エラー詳細:',
    failDesc1: '⚠️ ネットワークの問題等で巨大なファイル（Emscripten等）のダウンロードに失敗した可能性があります。',
    failDesc2: '※ JELLLパッケージ自体のインストールは完了させます。後ほど以下のコマンドで不足分を手動インストールしてください。',
    setupFailed: 'セットアップに失敗しました'
} : {
    title: 'JELLL - Toolchain Auto Installer',
    desc1: 'Installing necessary dev tools automatically from smallest to largest.',
    desc2: '(Already installed tools will be skipped automatically)',
    envOverride: '[JELLL] Based on JELLL_LANGS environment variable (${langs}), installing specified tools only...',
    npmDetected: '[JELLL] npm install detected.',
    npmAutoMsg: 'Displaying interactive prompt directly from console.',
    npmFallback: 'Interactive prompt unavailable. Auto-installing all compilers.',
    npmSkipHint: '* To skip, set environment variable JELLL_LANGS=none',
    noTty: '[TTY Not Detected] Non-interactive environment. Installing all tools.',
    promptQ: 'Which compilers would you like to install?',
    promptDesc: '(* Mandatory esbuild will be installed automatically)',
    promptAll: 'All (C++, Rust, Zig)',
    promptRecommend: 'recommended',
    promptCpp: 'C/C++ (Emscripten)',
    promptRust: 'Rust (wasm32)',
    promptZig: 'Zig',
    promptSkip: 'Skip (esbuild only)',
    promptInput: 'Enter a number (e.g. 1)',
    promptInvalid: 'Invalid input. Installing all by default.',
    timeoutAll: 'Input timeout (30s). Installing all tools by default...',
    noneSelected: '* No extra compilers selected. Skipping additional installations.',
    checkEsbuild: 'Checking esbuild...',
    esbuildOk: 'esbuild is already installed (skipping)',
    esbuildInstall: 'Installing esbuild...',
    esbuildDone: 'esbuild installed successfully',
    checkZig: 'Checking Zig (~50MB)...',
    zigOk: 'Zig is already installed (skipped)',
    zigInstall: 'Downloading & extracting Zig...',
    zigExt: 'Extracting Zig...',
    zigDone: 'Zig installed and added to PATH',
    checkRust: 'Checking Rust (~300MB)...',
    rustOk: 'Rust is already installed (skipped)',
    rustWasm: 'Checking wasm32 target...',
    rustWasmDone: 'Rust WebAssembly configuration applied',
    rustInstall: 'Downloading Rust toolchain...',
    rustAutoInstall: 'Auto-installing Rust...',
    rustAddWasm: 'Adding wasm32 target...',
    rustDone: 'Rust installed and added to PATH',
    checkCpp: 'Checking Emscripten (~1GB)...',
    cppOk: 'Emscripten is already installed (skipped)',
    cppClone: 'Fetching Emscripten repository...',
    cppExt: 'Extracting Emscripten installer...',
    cppDl: 'Downloading Emscripten data... (may take 5-10 minutes)',
    cppAct: 'Activating Emscripten...',
    cppDone: 'Emscripten installed and added to PATH',
    success: '🎉 [JELLL] Required dev tools setup complete!',
    restartWarning: '⚠️  IMPORTANT: Restart your terminal (or VSCode) to apply PATH changes. ⚠️',
    nextStep: 'After that, you can compile to WebAssembly using: jelll dev main.jelll',
    failTitle: 'Some toolchain setups were interrupted by timeout/network errors.',
    failDetail: '❌ [JELLL] Error Details:',
    failDesc1: '⚠️ Massive file downloads (like Emscripten) might have failed due to network instability.',
    failDesc2: '* JELLL package installation itself is completed. Please install the missing tools manually later by running:',
    setupFailed: 'Setup failed'
};

// ── ヘッダー表示 ──
console.log('');
console.log('\x1b[1m\x1b[36m' + '  ┌──────────────────────────────────────────────────────────┐');
console.log(`  │  ${t.title.padEnd(56)}│`);
console.log('  │                                                          │');
console.log(`  │  ${t.desc1.padEnd(52)}│`);
console.log(`  │  ${t.desc2.padEnd(52)}│`);
console.log('  └──────────────────────────────────────────────────────────┘' + '\x1b[0m');
console.log('');
console.log('');

// ── 定数 ──
const toolsDir = "C:\\jelll-tools";
if (!fs.existsSync(toolsDir)) fs.mkdirSync(toolsDir, { recursive: true });
const isNpm = !!process.env.npm_lifecycle_event;
const isWindows = os.platform() === 'win32';

// ── ログ関数 (プレミアム風) ──
function logStep(message) {
    console.log(`\x1b[34m[INFO]\x1b[0m ${message}`);
}
function logOk(message) {
    console.log(`\x1b[32m[DONE]\x1b[0m ${message}`);
}
function logFail(message) {
    console.log(`\x1b[31m[FAIL]\x1b[0m ${message}`);
}
function logProgress(chunk) {
    // インデントを付けて出力を流す
    const lines = chunk.toString().split('\n');
    lines.forEach(line => {
        if (line.trim()) {
            process.stdout.write(`    \x1b[90m> ${line.trim()}\x1b[0m\n`);
        }
    });
}

// ── コマンド実行 (ストリーミング & タイムアウト付き) ──
const CMD_TIMEOUT = 20 * 60 * 1000; // 20分 (Emscriptenは長い)

function runCmd(cmd, args = [], cwd = process.cwd(), timeoutMs = CMD_TIMEOUT, stream = true) {
    return new Promise((resolve, reject) => {
        const commandString = cmd + (args.length > 0 ? ' ' + args.join(' ') : '');
        const child = spawn(commandString, { cwd, shell: true, stdio: ['ignore', 'pipe', 'pipe'] });

        let fullLog = '';
        child.stdout.on('data', chunk => {
            fullLog += chunk.toString();
            if (stream) logProgress(chunk);
        });
        child.stderr.on('data', chunk => {
            fullLog += chunk.toString();
            if (stream) logProgress(chunk);
        });

        const timer = setTimeout(() => {
            child.kill();
            reject(new Error(`Timeout after ${timeoutMs / 1000}s\nCmd: ${commandString}\nLog: ${fullLog.substring(fullLog.length - 500)}`));
        }, timeoutMs);

        child.on('close', code => {
            clearTimeout(timer);
            if (code === 0) resolve(fullLog);
            else reject(new Error(`Exit ${code}\nCmd: ${commandString}\nLog: ${fullLog.substring(fullLog.length - 500)}`));
        });
        child.on('error', err => {
            clearTimeout(timer);
            reject(err);
        });
    });
}

async function runWithRetry(cmd, args = [], cwd = process.cwd(), retries = 3, timeoutMs = CMD_TIMEOUT) {
    for (let i = 0; i < retries; i++) {
        try {
            return await runCmd(cmd, args, cwd, timeoutMs);
        } catch (err) {
            if (i < retries - 1) {
                await new Promise(r => setTimeout(r, 3000));
            } else {
                throw err;
            }
        }
    }
}

function checkCmd(cmd, args = []) {
    return new Promise(resolve => {
        const commandString = cmd + (args.length > 0 ? ' ' + args.join(' ') : '');
        const child = spawn(commandString, { shell: true, stdio: 'ignore' });
        const timer = setTimeout(() => { child.kill(); resolve(false); }, 15000);
        child.on('close', code => { clearTimeout(timer); resolve(code === 0); });
        child.on('error', () => { clearTimeout(timer); resolve(false); });
    });
}

async function addToPath(newPath) {
    if (!isWindows) return;
    const psCmd = `$p = [Environment]::GetEnvironmentVariable('Path','User'); if ($p -and ($p.Split(';') -contains '${newPath}')) { exit 0 }; [Environment]::SetEnvironmentVariable('Path', $p + ';${newPath}', 'User')`;
    await runCmd('powershell', ['-NoProfile', '-Command', psCmd], process.cwd(), 30000, false);
}

// ── CONデバイスを使ったプロンプト (npm install中でも動作) ──
function openConPrompt(questionText, timeoutMs = 30000) {
    return new Promise(resolve => {
        let conIn, conOut, rl;
        try {
            // Windows CONデバイスでnpmのstdin/stdoutバイパス
            if (isWindows) {
                const fdIn = fs.openSync('\\\\.\\CONIN$', 'rs');
                const fdOut = fs.openSync('\\\\.\\CONOUT$', 'w');
                conIn = fs.createReadStream(null, { fd: fdIn });
                conOut = fs.createWriteStream(null, { fd: fdOut });
            } else {
                // Unix: /dev/tty
                conIn = fs.createReadStream('/dev/tty');
                conOut = fs.createWriteStream('/dev/tty');
            }
        } catch (e) {
            // CONデバイスが開けない場合はnull
            resolve(null);
            return;
        }

        rl = readline.createInterface({ input: conIn, output: conOut });

        const timer = setTimeout(() => {
            conOut.write(`\n\x1b[33m${t.timeoutAll}\x1b[0m\n`);
            rl.close();
            try { conIn.destroy(); conOut.destroy(); } catch(e) {}
            resolve(null);
        }, timeoutMs);

        rl.question(questionText, (answer) => {
            clearTimeout(timer);
            rl.close();
            try { conIn.destroy(); conOut.destroy(); } catch(e) {}
            resolve(answer);
        });
    });
}

// ── 言語選択メニュー ──
// ── 引数の処理 ──
const args = process.argv.slice(2);
const isInteractiveMode = args.includes('--interactive');
const isNpmMode = args.includes('--npm') || (process.env.npm_lifecycle_event === 'postinstall' && !isInteractiveMode);

// ── 多機能選択プロンプト (矢印キー & スペース) ──
async function multiSelect(question, items, isJa) {
    return new Promise(resolve => {
        let cursor = 0;
        const selected = new Set(); // デフォルトは未選択状態
        const isWindows = process.platform === 'win32';

        function render() {
            process.stdout.write('\x1b[?25l'); // カーソル非表示
            items.forEach((item, i) => {
                const isCursor = i === cursor;
                const isChecked = selected.has(i);
                
                let line = '';
                if (isCursor) {
                    line += '\x1b[36m>\x1b[0m '; // カーソル
                } else {
                    line += '  ';
                }

                if (isChecked) {
                    // 選択済み: [●] を表示 (塗りつぶしなし)
                    line += `\x1b[1m [●] ${item} \x1b[0m`;
                } else {
                    // 未選択: [ ]
                    line += `\x1b[90m [ ] ${item} \x1b[0m`;
                }
                
                process.stdout.write(line + '\n');
            });
            process.stdout.write('\n\x1b[90m(Space: 選択/解除, Enter: 決定)\x1b[0m\n');
        }

        function clear() {
            // 出力した行数分をクリアして上に戻る
            for (let i = 0; i < items.length + 2; i++) {
                process.stdout.write('\x1b[1A\x1b[2K');
            }
        }

        process.stdin.setRawMode(true);
        process.stdin.resume();
        process.stdin.setEncoding('utf8');

        console.log(`  \x1b[32m?\x1b[0m \x1b[1m${question}\x1b[0m`);
        render();

        const onData = (key) => {
            if (key === '\u0003') { // Ctrl+C
                process.exit();
            }
            if (key === '\r' || key === '\n') {
                process.stdin.setRawMode(false);
                process.stdin.pause();
                process.stdin.removeListener('data', onData);
                process.stdout.write('\x1b[?25h'); // カーソル復帰
                resolve(Array.from(selected));
                return;
            }
            if (key === ' ') {
                if (selected.has(cursor)) selected.delete(cursor);
                else selected.add(cursor);
            }
            if (key === '\u001b[A' || key === 'k') { // Up
                cursor = (cursor - 1 + items.length) % items.length;
            }
            if (key === '\u001b[B' || key === 'j') { // Down
                cursor = (cursor + 1) % items.length;
            }

            clear();
            render();
        };

        process.stdin.on('data', onData);
    });
}

async function askLanguages() {
    // インタラクティブモード以外かつ、npm install中なら esbuild のみ (後で setup)
    if (!isInteractiveMode && isNpmMode) {
        return ['esbuild-only'];
    }

    // 環境変数優先
    if (process.env.JELLL_LANGS) {
        if (process.env.JELLL_LANGS.trim().toLowerCase() === 'none') return [];
        const langs = process.env.JELLL_LANGS.split(',').map(s => s.trim().toLowerCase());
        const msg = t.envOverride.replace('${langs}', process.env.JELLL_LANGS);
        console.log(`  \x1b[32m${msg}\x1b[0m\n`);
        return langs;
    }

    if (isNpm && !isInteractiveMode) {
        // npm install中かつ非対話ならデフォルト
        return ['cpp', 'rust', 'zig'];
    }

    // TTYチェック
    if (!process.stdin.isTTY) {
        return ['cpp', 'rust', 'zig'];
    }

    const options = [
        t.promptCpp,
        t.promptRust,
        t.promptZig
    ];

    const chosenIndices = await multiSelect(t.promptQ, options, isJa);
    const result = [];
    if (chosenIndices.includes(0)) result.push('cpp');
    if (chosenIndices.includes(1)) result.push('rust');
    if (chosenIndices.includes(2)) result.push('zig');
    
    if (result.length === 0) {
        console.log(`  \x1b[33m${t.noneSelected}\x1b[0m`);
    } else {
        console.log(`  \x1b[32mSelected:\x1b[0m ${result.join(', ')}`);
    }
    
    return result;
}

// ── メインインストール処理 ──
async function main() {
    const langs = await askLanguages();

    console.log('');

    // --- 1. esbuild (必須, 小さい) ---
    logStep(t.checkEsbuild);
    if (await checkCmd('esbuild', ['--version'])) {
        logOk(t.esbuildOk);
    } else {
        logStep(t.esbuildInstall);
        await runCmd('npm', ['install', '-g', 'esbuild']);
        logOk(t.esbuildDone);
    }

    // --- コアのみモードの場合、ここで終了 ---
    if (langs.includes('esbuild-only')) {
        console.log('');
        console.log('  \x1b[1m\x1b[32m' + '🎉 JELLL 核心部分のインストールが完了しました！');
        console.log('  \x1b[0mC++, Rust, Zig コンパイラを導入するには、いつでも以下のコマンドを実行してください：');
        console.log('');
        console.log('  \x1b[36m\x1b[1m    jelll setup\x1b[0m');
        console.log('');
        return;
    }

    // --- 2. Zig (~50MB) ---
    if (langs.includes('zig')) {
        logStep(t.checkZig);
        if (await checkCmd('zig', ['version'])) {
            logOk(t.zigOk);
        } else {
            logStep(t.zigInstall);
            const zigZip = path.join(toolsDir, 'zig.zip');
            const zigFolder = 'zig-windows-x86_64-0.12.0';
            const zigExtracted = path.join(toolsDir, zigFolder);

            if (!fs.existsSync(zigExtracted)) {
                await runWithRetry('curl', ['-fSL', '-o', `"${zigZip}"`, `https://ziglang.org/download/0.12.0/${zigFolder}.zip`]);
                logStep(t.zigExt);
                await runCmd('tar', ['-xf', `"${zigZip}"`, '-C', `"${toolsDir}"`]);
                // ZIPファイル削除
                try { fs.unlinkSync(zigZip); } catch(e) {}
            }
            await addToPath(path.join(toolsDir, zigFolder));
            logOk(t.zigDone);
        }
    }

    // --- 3. Rust (~300MB) ---
    if (langs.includes('rust')) {
        logStep(t.checkRust);
        if (await checkCmd('rustc', ['--version'])) {
            logOk(t.rustOk);
            logStep(t.rustWasm);
            await runCmd('rustup', ['target', 'add', 'wasm32-unknown-unknown']);
            logOk(t.rustWasmDone);
        } else {
            logStep(t.rustInstall);
            const rustupFile = path.join(toolsDir, 'rustup-init.exe');
            await runWithRetry('curl', ['-fSL', '-o', `"${rustupFile}"`, 'https://win.rustup.rs/']);

            logStep(t.rustAutoInstall);
            await runCmd(`"${rustupFile}"`, ['-y', '--default-toolchain', 'stable', '--profile', 'minimal']);

            const cargoBin = path.join(os.homedir(), '.cargo', 'bin');
            logStep(t.rustAddWasm);
            await runCmd(`"${path.join(cargoBin, 'rustup.exe')}"`, ['target', 'add', 'wasm32-unknown-unknown']);

            await addToPath(cargoBin);
            logOk(t.rustDone);
            // インストーラ削除
            try { fs.unlinkSync(rustupFile); } catch(e) {}
        }
    }

    // --- 4. Emscripten (~1GB) ---
    if (langs.includes('cpp')) {
        logStep(t.checkCpp);
        if (await checkCmd('emcc', ['--version'])) {
            logOk(t.cppOk);
        } else {
            logStep(t.cppClone);
            const emsdkDirMain = path.join(toolsDir, 'emsdk-main');
            const emsdkDir = path.join(toolsDir, 'emsdk');

            if (!fs.existsSync(emsdkDir)) {
                const emsdkZip = path.join(toolsDir, 'emsdk.zip');
                await runWithRetry('curl', ['-fSL', '-o', `"${emsdkZip}"`, 'https://github.com/emscripten-core/emsdk/archive/refs/heads/main.zip']);
                logStep(t.cppExt);
                await runCmd('tar', ['-xf', `"${emsdkZip}"`, '-C', `"${toolsDir}"`]);

                // GitHub zipはemsdk-mainとして展開される
                if (fs.existsSync(emsdkDirMain)) {
                    fs.renameSync(emsdkDirMain, emsdkDir);
                }
                // ZIPファイル削除
                try { fs.unlinkSync(emsdkZip); } catch(e) {}
            }

            logStep(t.cppDl);
            await runWithRetry('call', ['emsdk', 'install', 'latest'], emsdkDir, 3);

            logStep(t.cppAct);
            await runWithRetry('call', ['emsdk', 'activate', '--permanent', 'latest'], emsdkDir, 3);
            logOk(t.cppDone);
        }
    }

    console.log('');
    console.log(`  \x1b[32m${t.success}\x1b[0m`);
    console.log(`  \x1b[33m${t.restartWarning}\x1b[0m`);
    console.log(`  ${t.nextStep}`);
    console.log('');
}

main().catch(err => {
    logFail(t.failTitle);
    console.error(`\n  \x1b[31m${t.failDetail}\x1b[0m\n  ${err.message}`);
    console.log(`\n  \x1b[33m${t.failDesc1}\x1b[0m`);
    console.log(`  \x1b[33m${t.failDesc2}\x1b[0m\n`);
    console.log('  \x1b[36mcd C:\\jelll-tools\\emsdk\x1b[0m');
    console.log('  \x1b[36memsdk install latest\x1b[0m');
    console.log('  \x1b[36memsdk activate latest\x1b[0m\n');
    process.exit(0);
});
