// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Reproduce the immutable V15 VMEM bank. No network or device access.
import fs from 'node:fs';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import '../sdk/WebUpdate.js';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const pkg=fs.readFileSync(new URL('../sdk/firmware/FM-1_015.fwsc',import.meta.url));
assert.equal(sha(pkg),'db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a');
const raw=FM1Web.inspectPackage(new Uint8Array(pkg),true).factory;
assert.equal(sha(raw),'ecad0b8620f9d784d46c8da1e3c5de02c40214649ed8dec218026431046aad25');
// VMEM literals are seven-bit. A high bit denotes a 3..130-byte match,
// followed by a one/two-byte base-128 backward distance. No heap is needed.
const packed=[];
for(let i=0;i<raw.length;){
 let length=2,distance=0;
 for(let j=0;j<i;j++){
  let n=0;while(n<130&&i+n<raw.length&&raw[j+n]===raw[i+n])n++;
  if(n>length){length=n;distance=i-j;}
 }
 if(distance){packed.push(128+length-3,(distance&127)|(distance>=128?128:0));if(distance>=128)packed.push(distance>>7);i+=length;}
 else{assert(raw[i]<128);packed.push(raw[i++]);}
}
const decoded=[];
for(let i=0;i<packed.length;){const token=packed[i++];if(token<128)decoded.push(token);else{
 const n=token-128+3,lo=packed[i++],distance=(lo&127)|(lo>=128?packed[i++]<<7:0);
 assert(distance>0&&distance<=decoded.length);
 for(let k=0;k<n;k++)decoded.push(decoded[decoded.length-distance]);
}}
assert.deepEqual(Buffer.from(decoded),Buffer.from(raw));
const lines=[];for(let i=0;i<packed.length;i+=24)lines.push('    '+packed.slice(i,i+24).join(',')+',');
const content='/* SPDX-License-Identifier: Apache-2.0\n'+
 ' * M-VAVE original V15 factory DX7 bank; lossless encoding, 2026-10-19.\n'+
 ' * Regenerate: node tools/Generate-Factory-DX7.mjs\n'+
 ' * VMEM SHA-256: '+sha(raw)+'\n'+
 ' * Source and license: THIRD_PARTY.md (FM-1_015.fwsc). */\n'+
 'static const uint8_t factory_packed[] = {\n'+lines.join('\n')+'\n};\n';
const target=new URL('../app/fm_factory.inc',import.meta.url);
if(process.argv.includes('--check'))assert.equal(fs.readFileSync(target,'utf8'),content);
else fs.writeFileSync(target,content);
console.log(`V15 factory bank verified: 128 voices, ${raw.length} VMEM bytes, ${packed.length} encoded bytes.`);
