# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
[CmdletBinding()]
param(
    [string]$WasiSdk = $env:FM1_WASI_SDK,
    [string]$PackageJson,
    [string]$SourceUrl = $env:FM1_SOURCE_URL,
    [switch]$LocalPreview
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$utf8 = New-Object Text.UTF8Encoding $false
function Hash([string]$file) { (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() }
function Build-Simulator {
    $out = Join-Path $root 'build/web'
    [IO.Directory]::CreateDirectory($out) | Out-Null
    function Digest([string[]]$items) {
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try { ([BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($items -join "`n")))).Replace('-','').ToLowerInvariant() }
        finally { $algorithm.Dispose() }
    }
    function Run([string]$exe, [string[]]$arguments) {
        & $exe @arguments | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Command failed: $exe ($LASTEXITCODE)" }
    }
    if (!(Test-Path -LiteralPath (Join-Path $root 'web/engine/Simulator.c'))) {
        throw 'Missing browser sources. Download the complete source tree.'
    }
    $inputs = @{}
    foreach ($file in Get-ChildItem -LiteralPath @((Join-Path $root 'app'), (Join-Path $root 'web'), (Join-Path $root 'third_party/synth')) -Recurse -File) {
        $name = $file.FullName.Substring($root.Length+1).Replace('\','/')
        if (!$name.StartsWith('app/board/') -and $file.Extension -cin @('.c','.h','.inc')) { $inputs[$name] = Hash $file.FullName }
    }
    foreach ($name in @('tools/Build-Web.ps1') +
        @(Get-ChildItem -LiteralPath (Join-Path $root 'sdk/include') -Filter '*.h' | ForEach-Object { 'sdk/include/' + $_.Name })) {
        $inputs[$name] = Hash (Join-Path $root $name)
    }
    [string[]]$names = @($inputs.Keys)
    [Array]::Sort($names, [StringComparer]::Ordinal)
    $sourceHash = Digest @($names | ForEach-Object { $_ + ':' + $inputs[$_] })
    $wasm = Join-Path $out 'simulator.wasm'
    $metadata = Join-Path $out 'simulator.json'
    if (!$WasiSdk) {
        $cache = Join-Path $root 'build/tools'
        $WasiSdk = Join-Path $cache 'wasi-sdk-27.0-x86_64-windows'
        if (!(Test-Path -LiteralPath (Join-Path $WasiSdk 'bin/clang.exe'))) {
            [IO.Directory]::CreateDirectory($cache) | Out-Null
            $archive = Join-Path $cache 'wasi-sdk-27.0-x86_64-windows.tar.gz'
            $expected = '4a576c13125c91996d8cc3b70b7ea0612c2044598d2795c9be100d15f874adf6'
            if (!(Test-Path -LiteralPath $archive) -or (Hash $archive) -cne $expected) {
                Write-Host 'Downloading WASI SDK 27 (518 MiB). Set FM1_WASI_SDK to reuse an extracted compiler.'
                Run 'curl.exe' @('--fail','--location','--retry','2','--output',$archive,
                    'https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-27/wasi-sdk-27.0-x86_64-windows.tar.gz')
            }
            if ((Hash $archive) -cne $expected) { throw 'WASI SDK archive SHA-256 mismatch.' }
            Run 'tar.exe' @('-xzf',$archive,'-C',$cache)
        }
    }
    $WasiSdk = [IO.Path]::GetFullPath($WasiSdk)
    $compiler = Join-Path $WasiSdk 'bin/clang.exe'
    if (!(Test-Path -LiteralPath $compiler) -or !(Test-Path -LiteralPath (Join-Path $WasiSdk 'share/wasi-sysroot'))) {
        throw 'Set FM1_WASI_SDK to an extracted WASI SDK 27 directory.'
    }
    # Switching SDKs or changing their files must invalidate the build cache.
    $scanPath = if ($WasiSdk.StartsWith('\\?\')) { $WasiSdk } elseif ($WasiSdk.StartsWith('\\')) {
        '\\?\UNC\' + $WasiSdk.TrimStart('\')
    } else { '\\?\' + $WasiSdk }
    $toolStamp = Digest @($WasiSdk; Get-ChildItem -LiteralPath $scanPath -Recurse -File | Sort-Object FullName |
        ForEach-Object { $_.FullName + ':' + $_.Length + ':' + $_.LastWriteTimeUtc.Ticks })
    if ((Test-Path -LiteralPath $wasm) -and (Test-Path -LiteralPath $metadata)) {
        try { $cached = Get-Content -LiteralPath $metadata -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $cached = $null }
        if ($cached -and $cached.schema -eq 2 -and $cached.toolchain_stamp -ceq $toolStamp -and
            $cached.source_sha256 -ceq $sourceHash -and $cached.wasm_sha256 -ceq (Hash $wasm)) {
            return [pscustomobject]@{file=$wasm; source_sha256=$sourceHash; cache_hit=$true}
        }
    }
    # Discard invalid cache files before starting a new compilation.
    foreach ($file in @($wasm,$metadata)) { if ([IO.File]::Exists($file)) { [IO.File]::Delete($file) } }
    $options = @(('--sysroot=' + (Join-Path $WasiSdk 'share/wasi-sysroot')),
        '-std=c11','-O2','-fwrapv','-fno-fast-math','-ffp-contract=off',
        '-DFM1_HOST_TEST','-DFM1_OPEN_WEB','-Wall','-Wextra','-Werror',
        '-Wno-unused-function','-Wno-array-parameter','-mexec-model=reactor')
    foreach ($name in @('web/engine','web/synth','third_party/synth','sdk/include','app/synth','app')) { $options += '-I' + (Join-Path $root $name) }
    foreach ($name in $names) { if ($name.EndsWith('.c') -and !$name.StartsWith('sdk/')) { $options += Join-Path $root $name } }
    $options += @('-Wl,--strip-all','-Wl,-z,stack-size=131072','-Wl,--initial-memory=4194304','-Wl,--max-memory=33554432','-o',$wasm)
    Write-Host 'Compile the browser application'
    try {
        Run $compiler $options
        $record = @{schema=2;source_sha256=$sourceHash;toolchain_stamp=$toolStamp;wasm_sha256=(Hash $wasm)} | ConvertTo-Json
        [IO.File]::WriteAllText($metadata,$record,(New-Object Text.UTF8Encoding $false))
    } catch {
        foreach ($file in @($wasm,$metadata)) { if ([IO.File]::Exists($file)) { [IO.File]::Delete($file) } }
        throw
    }
    [pscustomobject]@{file=$wasm; source_sha256=$sourceHash; cache_hit=$false}
}
$output = Join-Path $root $(if ($PackageJson) { 'build/official-web/FM1-Official-Open-Source.html' } else { 'build/preview/index.html' })
# Invalidate old output before source URL validation can stop this build.
foreach ($file in @($output, (Join-Path (Split-Path $output) 'SHA256SUMS.txt'))) {
    if ([IO.File]::Exists($file)) { [IO.File]::Delete($file) }
}
if ($LocalPreview -and $PackageJson) { throw 'LocalPreview cannot produce a firmware distribution.' }
if ([string]::IsNullOrWhiteSpace($SourceUrl) -and !$LocalPreview) {
    throw 'Set FM1_SOURCE_URL or -SourceUrl to the HTTPS URL of the matching source archive.'
}
$sourceUri = $null
if (!$LocalPreview -and (![Uri]::TryCreate($SourceUrl, [UriKind]::Absolute, [ref]$sourceUri) -or
    $sourceUri.Scheme -cne 'https' -or $sourceUri.UserInfo -or $SourceUrl -match '[\r\n]')) {
    throw 'FM1_SOURCE_URL must be an HTTPS source URL without credentials.'
}
$versionMatch = [regex]::Match([IO.File]::ReadAllText((Join-Path $root 'app/version.h')), '(?m)^#define FM1_RELEASE_VERSION "(V(?:0[1-9]|[1-9][0-9]{1,2}))"\s*$')
if (!$versionMatch.Success) { throw 'Set FM1_RELEASE_VERSION in app/version.h.' }
$releaseVersion = $versionMatch.Groups[1].Value
$notices = @("APP: GPLv3 with the FM1 SDK linking exception; no warranty. SDK and firmware resources have separate redistribution terms in THIRD_PARTY.md. Source: $SourceUrl")
if ($PackageJson) {
    $package = $PackageJson | ConvertFrom-Json
    if ($package.schema -ne 1 -or $package.release_version -cne $releaseVersion) { throw 'Firmware and web release versions differ.' }
    if ($package.build_id -cnotmatch '^[0-9a-f]{64}$') { throw 'Firmware source identifier is invalid.' }
    $notices += 'FM-1 V15 recovery firmware: https://www.m-vave.com/download (PC Firmware / FM-1 V15). Distributed by M-VAVE under Apache-2.0. Retain applicable licenses and notices; components with separate licenses retain their terms. See THIRD_PARTY.md.'
}
$compiled = Build-Simulator
$wasm = [IO.File]::ReadAllBytes($compiled.file)
$simulation = [ordered]@{schema=1;release_version=$releaseVersion;sha256=(Hash $compiled.file);source_sha256=$compiled.source_sha256;
    sample_bank='synthesized-demo';data=[Convert]::ToBase64String($wasm)} | ConvertTo-Json -Compress
$web = [IO.File]::ReadAllText((Join-Path $root 'sdk/Update.html.in'))
if ($LocalPreview) {
    $web = [regex]::Replace($web, '<a\b[^>]*href="/\*FM1_SOURCE_URL\*/"[^>]*>(.*?)</a>', '<span>$1 (local preview)</span>')
}
$replacements = @{
    '/*FM1_SOURCE_URL*/'=[Net.WebUtility]::HtmlEncode($sourceUrl)
    '/*FM1_PAYLOAD*/'=('const FM1_PACKAGE = ' + $(if ($PackageJson) { $PackageJson } else { 'null' }) + ';')
    '/*FM1_SIMULATOR*/'=('const FM1_SIMULATOR = ' + $simulation + ';')
    '/*FM1_I18N*/'=[IO.File]::ReadAllText((Join-Path $root 'sdk/I18n.js'))
    '/*FM1_SCRIPT*/'=[IO.File]::ReadAllText((Join-Path $root 'sdk/WebUpdate.js'))
    '/*FM1_SIM_SCRIPT*/'=[IO.File]::ReadAllText((Join-Path $root 'sdk/Simulator.js'))
    '/*FM1_SAMPLE_SCRIPT*/'=[IO.File]::ReadAllText((Join-Path $root 'sdk/Samples.js'))
}
foreach ($file in @('THIRD_PARTY.md','LICENSE.md')) {
    $notice = [IO.File]::ReadAllText((Join-Path $root $file))
    if ($file -ceq 'THIRD_PARTY.md' -and !$notice.StartsWith('# Third-party notices')) { throw 'Use the public distribution notices.' }
    $notices += "`n===== $file =====`n" + $notice
}
$replacements['/*FM1_NOTICES*/'] = [Net.WebUtility]::HtmlEncode(($notices -join "`n"))
foreach ($key in $replacements.Keys) { $web = $web.Replace($key,$replacements[$key]) }
if ($web -match '/\*FM1_[A-Z_]+\*/') { throw 'Unexpanded HTML placeholder.' }
[IO.Directory]::CreateDirectory((Split-Path $output)) | Out-Null
[IO.File]::WriteAllText($output,$web,$utf8)
if ($PackageJson) {
    $hash = Hash $output
    [IO.File]::WriteAllText((Join-Path (Split-Path $output) 'SHA256SUMS.txt'), "$hash  $([IO.Path]::GetFileName($output))`n", $utf8)
    Write-Host "Integrated FM1 workbench: $output"
    [pscustomobject]@{file=$output;sha256=$hash;simulator_sha256=(Hash $compiled.file);
        simulator_source_sha256=$compiled.source_sha256;simulator_bytes=$wasm.Length}
} else { Write-Host 'Preview: build/preview/index.html' }
