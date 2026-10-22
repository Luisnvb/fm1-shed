// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Runs a wasm32-wasi binary and exits with its exit code. Used by
// Test-Shed.ps1 to execute the host tests: this machine has no native
// gcc/clang, but the WASI SDK clang that Build-Web.ps1 already needs can
// target wasm32-wasi, and Node's bundled WASI runtime can run the result.
import { readFile } from 'node:fs/promises';
import { WASI } from 'node:wasi';
import { argv, exit } from 'node:process';

const wasmPath = argv[2];
if (!wasmPath) {
    console.error('usage: node wasi-run.mjs <file.wasm> [args...]');
    exit(2);
}
const wasi = new WASI({
    version: 'preview1',
    args: [wasmPath, ...argv.slice(3)],
    env: {},
    stdin: 0,
    stdout: 1,
    stderr: 2,
});
const bytes = await readFile(wasmPath);
const { instance } = await WebAssembly.instantiate(bytes, wasi.getImportObject());
exit(wasi.start(instance));
