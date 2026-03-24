#!/usr/bin/env node
const { spawnSync } = require('child_process');
const path = require('path');
const os = require('os');

const platform = os.platform();
const args = process.argv.slice(2);

try {
    if (args.includes('--setup') || args[0] === 'setup') {
        const scriptPath = path.join(__dirname, '..', 'scripts', 'postinstall.js');
        const result = spawnSync('node', [scriptPath, '--interactive'], { stdio: 'inherit' });
        process.exit(result.status !== null ? result.status : 0);
    }

    if (platform === 'win32') {
        // Windowsの場合は同梱されたjelll.exeを直接実行
        const exePath = path.join(__dirname, '..', 'jelll.exe');
        const result = spawnSync(exePath, args, { stdio: 'inherit' });
        process.exit(result.status !== null ? result.status : 1);
    } else {
        // Mac/Linuxの場合は現状エラー表示
        console.error('\n\x1b[31m❌ [JELLL Error]\x1b[0m 現在のJELLLパッケージにはWindows用(jelll.exe)のみ同梱されています。');
        console.error('macOSやLinux対応のネイティブバイナリは今後のアップデートで提供予定です。\n');
        process.exit(1);
    }
} catch (err) {
    console.error(`\n\x1b[31m❌ [JELLL Error]\x1b[0m 実行に失敗しました: ${err.message}\n`);
    process.exit(1);
}
