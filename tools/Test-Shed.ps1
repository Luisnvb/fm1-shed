# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
#
# Builds and runs the Shed host tests (tests/shed/), which exercise
# app/shed/theory.c and app/shed/exercises.c with no FM-1 SDK dependency.
#
# This machine has no native gcc/clang/cl, so instead of linking a native
# host executable, this script compiles the tests to a wasm32-wasi binary
# with the WASI SDK clang that Build-Web.ps1 already uses, and runs it
# through Node's bundled WASI runtime (tools/wasi-run.mjs). That is a real,
# optimized, warnings-as-errors C11 build of theory.c/exercises.c exactly as
# shipped, just executed in a WASI sandbox instead of natively -- it is not
# a reimplementation or a mock.
#
# Usage: .\tools\Test-Shed.ps1
#   Reuses an already-extracted WASI SDK 27 via -WasiSdk or $env:FM1_WASI_SDK,
#   or the copy tools\Build-Web.ps1 -LocalPreview downloads into
#   build\tools\wasi-sdk-27.0-x86_64-windows.
[CmdletBinding()]
param(
    [string]$WasiSdk = $env:FM1_WASI_SDK
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

if (!$WasiSdk) {
    $WasiSdk = Join-Path $root 'build/tools/wasi-sdk-27.0-x86_64-windows'
}
$WasiSdk = [IO.Path]::GetFullPath($WasiSdk)
$compiler = Join-Path $WasiSdk 'bin/clang.exe'
$sysroot = Join-Path $WasiSdk 'share/wasi-sysroot'
if (!(Test-Path -LiteralPath $compiler) -or !(Test-Path -LiteralPath $sysroot)) {
    throw "No WASI SDK 27 at $WasiSdk. Run '.\tools\Build-Web.ps1 -LocalPreview' once to download it (see SHED_SPEC.md section 2 for the curl/PS5.1 workaround), or pass -WasiSdk / set FM1_WASI_SDK to an extracted wasi-sdk-27.0 directory."
}

$outDir = Join-Path $root 'build/tests'
[IO.Directory]::CreateDirectory($outDir) | Out-Null
$wasm = Join-Path $outDir 'shed_tests.wasm'

$sources = @(
    'app/shed/theory.c', 'app/shed/exercises.c', 'app/shed/progress.c',
    'tests/shed/main.c', 'tests/shed/test_theory.c', 'tests/shed/test_exercises.c',
    'tests/shed/test_progress.c'
) | ForEach-Object { Join-Path $root $_ }

$options = @(
    ('--sysroot=' + $sysroot), '-std=c11', '-O1', '-g',
    '-Wall', '-Wextra', '-Werror',
    ('-I' + (Join-Path $root 'app/shed')), ('-I' + (Join-Path $root 'tests/shed')),
    '-o', $wasm
) + $sources

Write-Host 'Compile the Shed host tests'
& $compiler @options
if ($LASTEXITCODE -ne 0) { throw "Compilation failed ($LASTEXITCODE)" }

Write-Host 'Run the Shed host tests (wasm32-wasi via Node)'
# --no-warnings: Node's WASI "experimental" notice goes to stderr, which
# PowerShell 5.1 promotes to a terminating NativeCommandError under
# $ErrorActionPreference = 'Stop' even though the process exits 0 (same
# class of issue as the WASI SDK download in Build-Web.ps1; see SHED_SPEC.md
# section 2).
& node --no-warnings (Join-Path $PSScriptRoot 'wasi-run.mjs') $wasm
exit $LASTEXITCODE
