const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');

console.log('===========================================================');
console.log('[JEBC] Downloading and installing all required toolchains...');
console.log('[JEBC] This might take a few minutes. Please wait!');
console.log('===========================================================');

const toolsDir = "C:\\jebc-tools";
if (!fs.existsSync(toolsDir)) fs.mkdirSync(toolsDir, { recursive: true });

// 1. esbuild
try { 
  execSync('esbuild --version', { stdio: 'ignore' }); 
  console.log('✅ esbuild OK'); 
} catch { 
  console.log('📦 Installing esbuild globally...');
  execSync('npm install -g esbuild', { stdio: 'inherit' }); 
}

// 2. Emscripten (emsdk)
try { 
  execSync('emcc --version', { stdio: 'ignore' }); 
  console.log('✅ emcc OK'); 
} catch { 
  console.log('📦 Installing Emscripten (emsdk)...');
  const emsdkDir = path.join(toolsDir, 'emsdk');
  if (!fs.existsSync(emsdkDir)) {
      execSync(`git clone https://github.com/emscripten-core/emsdk.git "${emsdkDir}"`, { stdio: 'inherit' });
  }
  // --permanent adds the emcc tools to the Windows registry PATH automatically!
  execSync(`cd "${emsdkDir}" && emsdk install latest && emsdk activate --permanent latest`, { stdio: 'inherit' });
}

// 3. Rust (rustup)
try {
  execSync('rustc --version', { stdio: 'ignore' });
  // Ensure the target is added
  execSync('rustup target add wasm32-unknown-unknown', { stdio: 'ignore' });
  console.log('✅ Rust (rustc) OK');
} catch {
  console.log('📦 Installing Rust (rustup)...');
  const rustupFile = path.join(toolsDir, 'rustup-init.exe');
  execSync(`powershell -Command "Invoke-WebRequest -Uri https://win.rustup.rs/ -OutFile ${rustupFile}"`, { stdio: 'inherit' });
  execSync(`${rustupFile} -y --default-toolchain stable --profile minimal`, { stdio: 'inherit' });
  
  const cargoBin = path.join(os.homedir(), '.cargo', 'bin');
  execSync(`"${path.join(cargoBin, 'rustup.exe')}" target add wasm32-unknown-unknown`, { stdio: 'inherit' });
  
  // Add to User PATH
  console.log('[JEBC] Adding Rust to User PATH...');
  execSync(`powershell -Command "$path = [Environment]::GetEnvironmentVariable('Path', 'User'); if ($path -notmatch '(?i)[^a-z]cargo\\\\bin') { [Environment]::SetEnvironmentVariable('Path', $path + ';${cargoBin}', 'User') }"`, { stdio: 'ignore' });
}

// 4. Zig
try {
  execSync('zig version', { stdio: 'ignore' });
  console.log('✅ Zig OK');
} catch {
  console.log('📦 Installing Zig...');
  const zigZip = path.join(toolsDir, 'zig.zip');
  const zigFolder = 'zig-windows-x86_64-0.12.0';
  const zigExtracted = path.join(toolsDir, zigFolder);
  
  if (!fs.existsSync(zigExtracted)) {
      execSync(`powershell -Command "Invoke-WebRequest -Uri https://ziglang.org/download/0.12.0/${zigFolder}.zip -OutFile ${zigZip}"`, { stdio: 'inherit' });
      execSync(`powershell -Command "Expand-Archive -Force ${zigZip} ${toolsDir}"`, { stdio: 'inherit' });
  }
  
  // Add to User PATH
  console.log('[JEBC] Adding Zig to User PATH...');
  execSync(`powershell -Command "$path = [Environment]::GetEnvironmentVariable('Path', 'User'); if ($path -notmatch '(?i)[^a-z]zig-') { [Environment]::SetEnvironmentVariable('Path', $path + ';${zigExtracted}', 'User') }"`, { stdio: 'ignore' });
}

console.log('');
console.log('🎉 [JEBC] ALL TOOLCHAINS SETUP AUTOMATICALLY!');
console.log('⚠️  IMPORTANT: Please RESTART your terminal (or open a new tab) so the new PATH takes effect! ⚠️');
console.log('Then you can run: jebc dev main.jebc');
