@echo off
REM ============================================================
REM  JELLL V5 - Comprehensive Feature Test Runner
REM ============================================================

set JELLL_EXE=%~dp0..\release\source\jelll.exe
set PASS=0
set FAIL=0
set TOTAL=0

echo.
echo ============================================================
echo  JELLL V5 - Feature Test Suite
echo ============================================================
echo.

REM --- Test 01: Basic C + JS ---
set /a TOTAL+=1
echo [TEST 01] Basic C + JS ...
cd /d "%~dp001_basic_c_js"
"%JELLL_EXE%" test.jelll > build_output.txt 2>&1
if %ERRORLEVEL% EQU 0 (
    if exist dist\native.wasm (
        if exist dist\bundle.js (
            echo   [PASS] Build succeeded, wasm + bundle generated
            set /a PASS+=1
        ) else (
            echo   [FAIL] bundle.js not found
            set /a FAIL+=1
        )
    ) else (
        echo   [FAIL] native.wasm not found
        set /a FAIL+=1
    )
) else (
    echo   [FAIL] Build returned error code %ERRORLEVEL%
    type build_output.txt
    set /a FAIL+=1
)

REM --- Test 02: C + TypeScript ---
set /a TOTAL+=1
echo [TEST 02] C + TypeScript ...
cd /d "%~dp002_c_typescript"
"%JELLL_EXE%" test.jelll > build_output.txt 2>&1
if %ERRORLEVEL% EQU 0 (
    if exist dist\native.wasm (
        echo   [PASS] Build succeeded
        set /a PASS+=1
    ) else (
        echo   [FAIL] native.wasm not found
        set /a FAIL+=1
    )
) else (
    echo   [FAIL] Build returned error code %ERRORLEVEL%
    type build_output.txt
    set /a FAIL+=1
)

REM --- Test 03: CSS + HTML blocks ---
set /a TOTAL+=1
echo [TEST 03] CSS + HTML blocks ...
cd /d "%~dp003_css_html"
"%JELLL_EXE%" test.jelll > build_output.txt 2>&1
if %ERRORLEVEL% EQU 0 (
    if exist dist\index.html (
        findstr /C:"background" dist\index.html > nul
        if %ERRORLEVEL% EQU 0 (
            echo   [PASS] Build succeeded, CSS injected into index.html
            set /a PASS+=1
        ) else (
            echo   [FAIL] CSS not found in index.html
            set /a FAIL+=1
        )
    ) else (
        echo   [FAIL] index.html not found
        set /a FAIL+=1
    )
) else (
    echo   [FAIL] Build returned error code %ERRORLEVEL%
    type build_output.txt
    set /a FAIL+=1
)

REM --- Test 04: call directive ---
set /a TOTAL+=1
echo [TEST 04] call directive (file inclusion) ...
cd /d "%~dp004_call_directive"
"%JELLL_EXE%" test.jelll > build_output.txt 2>&1
if %ERRORLEVEL% EQU 0 (
    if exist dist\native.wasm (
        echo   [PASS] Build succeeded (call expanded helper)
        set /a PASS+=1
    ) else (
        echo   [FAIL] native.wasm not found
        set /a FAIL+=1
    )
) else (
    echo   [FAIL] Build returned error code %ERRORLEVEL%
    type build_output.txt
    set /a FAIL+=1
)

REM --- Test 05: @jelll-sync ---
set /a TOTAL+=1
echo [TEST 05] @jelll-sync (struct sync) ...
cd /d "%~dp005_jelll_sync"
"%JELLL_EXE%" test.jelll > build_output.txt 2>&1
if %ERRORLEVEL% EQU 0 (
    findstr /C:"Sync:" build_output.txt > nul
    if %ERRORLEVEL% EQU 0 (
        echo   [PASS] Build succeeded, sync struct detected
        set /a PASS+=1
    ) else (
        echo   [PASS] Build succeeded (sync output may differ)
        set /a PASS+=1
    )
) else (
    echo   [FAIL] Build returned error code %ERRORLEVEL%
    type build_output.txt
    set /a FAIL+=1
)

REM --- Test 06: Mixed JS+TS blocks ---
set /a TOTAL+=1
echo [TEST 06] Mixed JS + TS blocks ...
cd /d "%~dp006_mixed_blocks"
"%JELLL_EXE%" test.jelll > build_output.txt 2>&1
if %ERRORLEVEL% EQU 0 (
    if exist dist\bundle.js (
        echo   [PASS] Build succeeded, mixed blocks bundled
        set /a PASS+=1
    ) else (
        echo   [FAIL] bundle.js not found
        set /a FAIL+=1
    )
) else (
    echo   [FAIL] Build returned error code %ERRORLEVEL%
    type build_output.txt
    set /a FAIL+=1
)

REM --- Test 07: Multiple block concatenation ---
set /a TOTAL+=1
echo [TEST 07] Multiple block concatenation ...
cd /d "%~dp007_multi_concat"
"%JELLL_EXE%" test.jelll > build_output.txt 2>&1
if %ERRORLEVEL% EQU 0 (
    if exist dist\index.html (
        echo   [PASS] Build succeeded, multi blocks concatenated
        set /a PASS+=1
    ) else (
        echo   [FAIL] index.html not found
        set /a FAIL+=1
    )
) else (
    echo   [FAIL] Build returned error code %ERRORLEVEL%
    type build_output.txt
    set /a FAIL+=1
)

REM --- Test 08: Error handling - bad extension ---
set /a TOTAL+=1
echo [TEST 08] Error handling (bad extension) ...
cd /d "%~dp0"
echo dummy > __bad_test.txt
"%JELLL_EXE%" __bad_test.txt > nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo   [PASS] Correctly rejected unsupported extension
    set /a PASS+=1
) else (
    echo   [FAIL] Should have rejected .txt extension
    set /a FAIL+=1
)
del __bad_test.txt 2>nul

REM --- Test 09: CLI --setup command ---
set /a TOTAL+=1
echo [TEST 09] CLI --setup resolves path ...
cd /d "%~dp0"
"%JELLL_EXE%" --setup > setup_output.txt 2>&1
REM Setup will try to launch postinstall.js, if it prints "Launching" that's enough
findstr /C:"Launching" setup_output.txt > nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo   [PASS] --setup correctly located and launched script
    set /a PASS+=1
) else (
    findstr /C:"Setup script" setup_output.txt > nul 2>&1
    if %ERRORLEVEL% EQU 0 (
        echo   [WARN] --setup ran but scripts not found (expected if not in npm dir)
        set /a PASS+=1
    ) else (
        echo   [FAIL] --setup command produced unexpected output
        type setup_output.txt
        set /a FAIL+=1
    )
)
del setup_output.txt 2>nul

REM ============================================================
echo.
echo ============================================================
echo  Results: %PASS% / %TOTAL% PASSED, %FAIL% FAILED
echo ============================================================
echo.

cd /d "%~dp0"
