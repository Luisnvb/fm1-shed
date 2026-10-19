/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* FM1 browser updater.
 * Fixed device protocol: 00 59, 7-bit SysEx packing, USB MIDI only.
 * The bundled vendor loader controls the Flash layout. A successful transfer
 * does not guarantee data preservation or recovery from interrupted power. */
(() => {
  'use strict';
  const fail = text => { throw new Error(text); };
  const check = (ok, text) => { if (!ok) fail(text); };
  const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
  async function abortable(operation,signal) {
    if(!signal)return operation;
    let cancel;
    try {
      return await Promise.race([operation,new Promise((_,reject)=>{
        cancel=()=>reject(signal.reason);signal.addEventListener('abort',cancel,{once:true});
        if(signal.aborted)cancel();
      })]);
    } finally {signal.removeEventListener('abort',cancel);}
  }
  const openingPorts=new WeakMap();
  async function portOperation(port,method,signal) {
    signal?.throwIfAborted();
    let timer,expired=false;
    const attempt={};if(method==='open')openingPorts.set(port,attempt);
    const operation=Promise.resolve().then(()=>port[method]());
    // A delayed open must not leave a MIDI handle behind after a timeout.
    operation.then(()=>{if(expired&&method==='open'&&openingPorts.get(port)===attempt)Promise.resolve().then(()=>port.close()).catch(()=>{});},()=>{});
    try {
      return await abortable(Promise.race([operation,new Promise((_,reject)=>{
        timer=setTimeout(()=>{expired=true;reject(Object.assign(new Error(method==='open'?
          '打开 MIDI 端口超时；请关闭其他 MIDI 软件或网页后重试。':
          '关闭 MIDI 端口超时；请重新插拔 USB 后连接。'),{code:'PORT_TIMEOUT'}));},method==='open'?5000:3000);
      })]),signal);
    } catch(error){expired=true;throw error;}
    finally {clearTimeout(timer);}
  }
  async function closePorts(input,output) {
    const results=await Promise.allSettled([portOperation(input,'close'),portOperation(output,'close')]);
    // Re-enumeration deliberately removes the old port during an update.
    const failure=results.find((result,index)=>result.status==='rejected'&&[input,output][index].state!=='disconnected');
    if(failure)throw failure.reason;
  }
  const hex = bytes => Array.from(bytes, x => x.toString(16).padStart(2, '0')).join('');
  const le = (p, at, n) => { let v=0; for(let i=0;i<n;i++) v += p[at+i] * 2**(i*8); return v; };
  const crc16Table=new Uint16Array([0,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
    0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef]);
  const crc16 = bytes => {
    let c=0;
    for(let i=0;i<bytes.length;i++){c^=bytes[i]<<8;c=((c<<4)^crc16Table[c>>>12])&65535;c=((c<<4)^crc16Table[c>>>12])&65535;}
    return c;
  };
  const decodeHeader = (bytes, key=65535) => {
    const p=Uint8Array.from(bytes); let k=key;
    for(let i=0;i<p.length;i++) { p[i]^=k&255; k=((k<<1)^(k&0x8000?0x1021:0))&65535; } return p;
  };
  const ascii = p => String.fromCharCode(...p).replace(/\0.*$/s, '');
  const openIdentity = value => /^(FM1_OFFICIAL_OPEN|FM1_COMMUNITY_\d+|FM-1_8\d{6})$/.test(value);
  async function sha256(bytes) { return hex(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))); }
  function inspectPackage(bytes, stock=false) {
    check(bytes instanceof Uint8Array && bytes.length>=1024 && bytes.length<=2097152, '固件包长度无效');
    const image=new Uint8Array(bytes.length-20); let product='', ended=false;
    for(let i=0;i<20;i++) {
      image.set(bytes.subarray(i*48,i*48+47),i*47);
      const marker=bytes[i*48+47];
      if(marker===125) ended=true;
      else { const c=(marker-i-1)&255; check(!ended && c>=32 && c<=126,'固件身份无效'); product+=String.fromCharCode(c); }
    }
    image.set(bytes.subarray(960),940);
    const head=decodeHeader(image.subarray(0,64)), count=le(head,8,2);
    check(le(head,0,2)===crc16(head.subarray(2)) && le(head,4,4)===image.length,'固件包头校验失败');
    check(ascii(head.subarray(16,32))==='AC791N' && count>0 && count<=12,'芯片型号或包目录无效');
    const table=image.subarray(64,64+count*80);
    check(crc16(table)===le(head,2,2),'固件目录校验失败');
    const entries=[];
    for(let i=0;i<count;i++) {
      const e=decodeHeader(table.subarray(i*80,i*80+80));
      const type=le(e,0,2), offset=le(e,8,4), size=le(e,12,4), name=ascii(e.subarray(64));
      const stored=le(e,16,4), encrypted=le(e,24,4);
      check(offset>=64+count*80 && offset<=image.length && size<=image.length-offset,'固件条目越界');
      check(stored>=size && stored<=image.length-offset && (!encrypted || encrypted===stored),'固件存储长度无效');
      check(!entries.some(p => stored && p.stored && offset<p.offset+p.stored && p.offset<offset+stored),'固件条目重叠');
      const data=image.subarray(offset,offset+size);
      // Vendor auxiliary metadata has a plaintext CRC but encrypted storage.
      // The outer SHA-256 covers it byte-for-byte; flash/loader must be plain.
      check(encrypted || crc16(data)===le(e,4,2),'固件内容校验失败：'+name);
      check(!encrypted || (type!==0 && type!==100),'固件主镜像或升级程序格式不兼容');
      entries.push({type,offset,size,stored,encrypted,crc:le(e,4,2),name,data});
    }
    const flash=entries.filter(e=>e.type===0), loader=entries.filter(e=>e.type===100);
    check(flash.length===1 && flash[0].size>=16384 && flash[0].size<=1044480 && flash[0].size%4096===0,'Flash 镜像无效');
    check(loader.length===1 && loader[0].size>0 && loader[0].size<32768,'升级程序缺失或无效');
    let factory;
    if(stock) {
      const banks=entries.filter(e=>e.type===50&&e.name==='USR');
      check(banks.length===1&&banks[0].size===73226&&banks[0].encrypted===73248,'原厂音色数据无效');
      const entry=banks[0], decoded=new Uint8Array(entry.stored);
      for(let i=0;i<decoded.length;i+=32)
        decoded.set(decodeHeader(image.subarray(entry.offset+i,entry.offset+i+32),0x980f^((entry.offset+i)>>>2)),i);
      check(crc16(decoded.subarray(0,entry.size))===entry.crc,'原厂音色数据无效');
      factory=decoded.slice(0,16384);
    }
    return {product,image,loader:loader[0].data,...(factory?{factory}: {})};
  }
  const assets=new WeakMap();
  async function assetBytes(meta,limit,message) {
    check(meta&&/^[0-9a-f]{64}$/.test(meta.sha256),message);
    let task=assets.get(meta);
    if(!task){
      task=(async()=>{
        let bytes;
        if(typeof meta.data==='string'){
          check(meta.data.length<=Math.ceil(limit/3)*4,message);
          const text=atob(meta.data);bytes=new Uint8Array(text.length);
          for(let i=0;i<text.length;i++)bytes[i]=text.charCodeAt(i);
        }else{
          check(typeof meta.url==='string'&&meta.url.length>0,message);
          const url=new URL(meta.url,location.href);
          check(url.origin===location.origin&&['https:','http:'].includes(url.protocol),message);
          const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),60000);
          try{
            const response=await fetch(url,{signal:controller.signal,credentials:'omit',cache:'force-cache'});
            check(response.ok&&Number(response.headers.get('Content-Length')||0)<=limit,message);
            const reader=response.body.getReader(),chunks=[];let size=0;
            try{for(;;){const {done,value}=await reader.read();if(done)break;size+=value.length;check(size<=limit,message);chunks.push(value);}}
            finally{await reader.cancel().catch(()=>{});}
            bytes=new Uint8Array(size);let at=0;for(const chunk of chunks){bytes.set(chunk,at);at+=chunk.length;}
          }finally{clearTimeout(timer);}
        }
        check(bytes.length<=limit&&(!meta.bytes||bytes.length===meta.bytes)&&await sha256(bytes)===meta.sha256,message);
        return bytes;
      })();
      assets.set(meta,task);task.catch(()=>{if(assets.get(meta)===task)assets.delete(meta);});
    }
    const bytes=await task;check(bytes.length<=limit,message);return bytes.slice();
  }
  async function prepare(meta) {
    const stock=meta.kind==='stock';
    check(meta.schema===1 && (stock?meta.product==='FM-1_015':openIdentity(meta.product)) && /^[0-9a-f]{64}$/.test(meta.build_id), '网页固件信息无效');
    if(stock)check(meta.sha256==='db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a'&&meta.build_id===meta.sha256,'原厂固件校验失败');
    check(meta.family===7 && meta.bytes>=1024 && meta.bytes<=2097152,'固件配置无效');
    const bytes=await assetBytes(meta,2097152,'网页内的固件 SHA-256 校验失败');
    const pkg=inspectPackage(bytes,stock);
    check(pkg.product===meta.product && await sha256(pkg.loader)===meta.loader_sha256,'固件与升级程序不匹配');
    // The vendor loader reports a fixed identity even when launched by Open.
    // Identify the installed Open application by its full hash.
    if(stock)await factoryCheck(pkg.factory);
    const vendorLoader=meta.loader_sha256==='ef1f2db89b011fa53e71df363f0122de0ea896618c53fb53e8da676322004541';
    return {...meta,...pkg,loaderIdentity:vendorLoader?'ota-FM-1_015':stock?'ota-'+meta.product:'ota-FM1C_'+meta.build_id.slice(0,16)};
  }
  async function factoryCheck(data) {
    check(data instanceof Uint8Array&&data.length===16384&&await sha256(data)==='ecad0b8620f9d784d46c8da1e3c5de02c40214649ed8dec218026431046aad25','原厂音色数据无效');
  }
  function pack7(bytes) {
    const out=[]; let bits=0, value=0;
    for(const byte of bytes) { value|=byte<<bits; bits+=8; while(bits>=7) {out.push(value&127); value>>>=7; bits-=7;} }
    if(bits) out.push(value&127); return out;
  }
  function frame(command,payload=[]) {
    check(payload.length<=520,'协议数据过长');
    let sum=0; for(const b of payload) sum=(sum+b)&255;
    return Uint8Array.from([240,...pack7([0,89,command,payload.length&255,payload.length>>8,0,...payload,(~sum)&255]),247]);
  }
  function parse(bytes) {
    if(!bytes || bytes.length<10 || bytes.length>608 || bytes[0]!==240 || bytes[bytes.length-1]!==247) return null;
    const raw=[]; let bits=0,value=0;
    for(let i=1;i<bytes.length-1;i++) {
      if(bytes[i]>127) return null;
      value|=bytes[i]<<bits; bits+=7;
      if(bits>=8) {raw.push(value&255); value>>>=8; bits-=8;}
    }
    if(bits>=7 || value!==0 || raw.length<7 || raw[0]!==0 || raw[1]!==89 || le(raw,3,3)!==raw.length-7) return null;
    let sum=0; for(let i=6;i<raw.length;i++) sum=(sum+raw[i])&255;
    return sum===255 ? {command:raw[2],data:Uint8Array.from(raw.slice(6,-1))} : null;
  }
  class Link {
    constructor(input,output,decoder=parse,signal) {
      this.signal=signal;
      this.input=input; this.output=output; this.queue=[]; this.waiter=null; this.closed=false; this.error=null;
      this.receive=ev=>{
        const p=decoder(ev.data); if(!p || this.closed) return;
        if(this.queue.length>=32) this.error=new Error('设备响应过多，已停止升级');
        else this.queue.push(p);
        if(this.waiter) this.waiter();
      };
      input.addEventListener('midimessage',this.receive);
    }
    get lost() { return this.closed || this.input.state==='disconnected' || this.output.state==='disconnected'; }
    send(bytes) { this.signal?.throwIfAborted();check(!this.lost,'USB 连接已断开'); this.output.send(bytes); }
    async read(ms) {
      this.signal?.throwIfAborted();
      if(this.error) throw this.error;
      if(!this.queue.length && !this.lost) await new Promise(resolve=>{
        const signal=this.signal;
        const finish=()=>{clearTimeout(timer);signal?.removeEventListener('abort',finish);this.waiter=null;resolve();};
        const timer=setTimeout(finish,ms); this.waiter=finish;
        signal?.addEventListener('abort',finish,{once:true});
      });
      this.signal?.throwIfAborted();
      if(this.error) throw this.error;
      return this.queue.shift()||null;
    }
    async close() {
      if(this.closed) return; this.closed=true; this.input.removeEventListener('midimessage',this.receive);
      if(this.waiter) this.waiter(); this.queue.length=0;
      await closePorts(this.input,this.output);
    }
    async query(command,payload=[],ms=1500) {
      this.queue.length=0; this.send(frame(command,payload));
      const until=Date.now()+ms;
      while(Date.now()<until) {
        const response=await this.read(Math.min(250,until-Date.now()));
        if(response && response.command===command) return response.data;
        check(!this.lost,'USB 连接已断开');
      }
      const error=new Error('设备未响应；当前固件可能尚未支持网页更新');error.code='TIMEOUT';throw error;
    }
  }
  function capability(data) {
    check(data.length===47 && ascii(data.subarray(0,4))==='FMC1' && data[4]===1 && data[5]<=7 && le(data,8,2)===512 && le(data,10,4)===1048576,'设备升级协议不匹配');
    check(data[7]===1,'设备使用未知的升级程序');
    return {family:data[5],state:data[6],build:hex(data.subarray(14,46)),error:data[46]};
  }
  function pairs(access,loader=false,force=false) {
    // Windows exposes the fixed application descriptor as USB-Midi. Its
    // product/capability or project-protocol reply still has to match.
    const name=loader ? /fm.?1|felucca|sloop|clipper|ota|sinco|usb.?midi|composite/i
                      : /fm.?1|^usb.?midi(?:\s+\d+)?$/i;
    const connectedInputs=[...access.inputs.values()].filter(p=>p.state==='connected');
    const inputs=connectedInputs.filter(p=>force || name.test(p.name||''));
    const outputs=[...access.outputs.values()].filter(p=>p.state==='connected');
    return inputs.flatMap(input=>{
      const named=outputs.filter(output=>output.name===input.name);
      const exact=named.filter(output=>(output.manufacturer||'')===(input.manufacturer||''));
      const found=exact.length===1?exact:named.length===1?named:
        !named.length&&connectedInputs.length===1&&outputs.length===1?outputs:[];
      return found.length===1 ? [{input,output:found[0]}] : [];
    });
  }
  class Updater {
    constructor(access,pkg,status=()=>{},timing={}) {
      this.access=access; this.pkg=pkg; this.status=status; this.link=null; this.identity=null; this.busy=false;
      this.timing={idle:10000,writeIdle:180000,reconnect:40000,settle:600,pace:0,...timing};
    }
    async open(pair,signal) {
      try { await portOperation(pair.input,'open',signal); await portOperation(pair.output,'open',signal); }
      catch(e) { await closePorts(pair.input,pair.output).catch(()=>{}); throw e; }
      const link=new Link(pair.input,pair.output,parse,signal);
      try {
        let reply;
        for(let attempt=0;attempt<2;attempt++){
          try {reply=await link.query(17,[],750);break;}
          catch(error){if(error.code!=='TIMEOUT'||attempt||link.lost)throw error;}
        }
        check(reply.length===27 && /^[\x20-\x7e]{1,27}$/.test(ascii(reply)),'设备身份响应无效');
        return {link,identity:ascii(reply)};
      }
      catch(e) { await link.close(); throw e; }
    }
    async discover(force=false,signal) {
      signal?.throwIfAborted();
      const ports=pairs(this.access,true,force),found=[];let lastError;
      this.diagnostics={inputs:[...this.access.inputs.values()].filter(p=>p.state==='connected').map(p=>p.name||'?'),
        outputs:[...this.access.outputs.values()].filter(p=>p.state==='connected').map(p=>p.name||'?'),attempts:[]};
      if(ports.length>16)throw Object.assign(Error('MIDI 端口过多，请断开不相关设备后重试'),{code:'AMBIGUOUS'});
      for(const pair of ports){
        try {signal?.throwIfAborted();found.push(await this.open(pair,signal));}
        catch(error){
          if(signal?.aborted){await Promise.allSettled(found.map(device=>device.link.close()));throw signal.reason;}
          lastError=error;this.diagnostics.attempts.push({input:pair.input.name||'?',output:pair.output.name||'?',error:error.message});
        }
      }
      if(found.length>1){await Promise.allSettled(found.map(device=>device.link.close()));throw Object.assign(Error('检测到多台可能的升级设备，无法确认目标'),{code:'AMBIGUOUS'});}
      if(found.length)return found[0];
      if(lastError&&lastError.code!=='TIMEOUT')throw lastError;
      return null;
    }
    async connect({force=false,signal}={}) {
      check(!this.busy,'正在更新'); await this.disconnect();
      this.force=force;this.mode=null;
      const device=await this.discover(force,signal);
      if(!device){
        const d=this.diagnostics;
        if(!d?.inputs.length||!d.outputs.length)fail('浏览器未发现完整的 MIDI 输入和输出。请连接开机的 FM1 USB-MIDI 接口，再重新连接。');
        if(!d.attempts.length)fail('已发现 MIDI 端口，但无法匹配 FM1 输入和输出。请查看连接详情，只保留目标 FM1 后重试。');
        fail('已找到 MIDI 端口，但设备未回复型号。请关闭其他 MIDI 软件和更新网页，重新开机并插拔 USB 后重试；强制烧录也需要设备先响应。');
      }
      this.link=device.link; this.identity=device.identity;
      try {
        if(this.identity===this.pkg.loaderIdentity || (force && /^ota-/i.test(this.identity))) {
          this.mode='loader';return {resume:true,identity:this.identity};
        }
        if(!force && this.identity!==this.pkg.product && !openIdentity(this.identity)) {
          this.mode='migration';return {identity:this.identity,migration:true};
        }
        // Stock V15 exposes the vendor identity and request-driven updater,
        // but has no Official Open capability command. Recognition is not an
        // update: entering this route still requires explicit force approval.
        if(force && this.identity==='FM-1_015') {
          this.mode='legacy';return {identity:this.identity,legacy:true};
        }
        let cap;
        try {cap=capability(await this.link.query(49));}
        catch(error) {
          // Some third-party identities share Open's numeric prefix. A missing
          // capability reply requires explicit migration, never a silent flash.
          // Malformed replies and disconnected ports remain errors.
          if(error.code!=='TIMEOUT' || this.link.lost)throw error;
          if(!force){this.mode='migration';return {identity:this.identity,migration:true};}
          this.mode='legacy';return {resume:false,identity:this.identity,legacy:true};
        }
        if(!force && cap.family!==this.pkg.family) {
          this.mode='migration';return {identity:this.identity,migration:true};
        }
        check(cap.state===0,'设备正在处理另一次升级');
        this.mode='tagged';this.family=cap.family;
        return {resume:false,identity:this.identity,current:cap.build,same:cap.build===this.pkg.build_id};
      } catch(e) { await this.disconnect(); throw e; }
    }
    async disconnect() { if(this.link) await this.link.close(); this.link=null; this.identity=null; }
    async waitFor(identity,legacyLoader=false) {
      const end=Date.now()+this.timing.reconnect;
      while(Date.now()<end) {
        {
          let device;
          try {device=await this.discover(this.force);}
          catch(error){if(error.code==='AMBIGUOUS')throw error;}
          if(device) {
            if(device.identity===identity || (legacyLoader && /^ota-/i.test(device.identity))) {
              this.link=device.link; this.identity=device.identity; return;
            }
            await device.link.close();
          }
        }
        await sleep(this.timing.settle);
      }
      fail('等待设备重连超时；请保留这个更新网页，重新连接同一台设备后重试');
    }
    async serve(finish,idle,phase) {
      const image=this.pkg.image, covered=new Uint8Array(Math.ceil(image.length/8));
      const started=Date.now(); let last=started,total=0;
      for(;;) {
        check(Date.now()-started<Math.max(idle*3,120000),'升级超过最长等待时间，尚未完成');
        check(Date.now()-last<idle,'设备停止请求数据，升级未完成');
        const packet=await this.link.read(250);
        if(!packet) { check(!this.link.lost,'USB 意外断开；重新连接同一设备后用此网页重试'); check(Date.now()-last<idle,'设备停止请求数据，升级未完成'); continue; }
        if(packet.command!==48 || packet.data.length!==8) continue;
        const p=packet.data, address=le(p,1,4), bytes=le(p,5,3);
        check(bytes>0 && bytes<=512,'设备请求长度无效');
        if(address===0xe0000000 || address===0xf0000000) {
          check(address===finish && bytes===8,'设备的升级阶段不匹配');
          this.link.send(frame(48,[p[0],...p.subarray(1,8),115,117,99,99,101,115,115,0]));
          // Web MIDI send queues bytes; leave the port open for the completion
          // response and bounded duplicate finish requests before disconnecting.
          const end=Date.now()+this.timing.settle;
          while(Date.now()<end && !this.link.lost) {
            const again=await this.link.read(Math.min(100,end-Date.now()));
            if(again && again.command===48 && again.data.length===8 && le(again.data,1,4)===finish && le(again.data,5,3)===8)
              this.link.send(frame(48,[again.data[0],...again.data.subarray(1,8),115,117,99,99,101,115,115,0]));
          }
          return;
        }
        check(address<=image.length && bytes<=image.length-address,'设备读取超出固件包范围');
        // The loader requests the next block only when ready. No fixed delay
        // is needed in addition to that request/response flow control.
        last=Date.now(); if(this.timing.pace>0) await sleep(this.timing.pace);
        this.link.send(frame(48,[p[0],...p.subarray(1,8),...image.subarray(address,address+bytes)]));
        // Partial and overlapping reads count each byte once.
        for(let i=address;i<address+bytes;i++) {
          const index=i>>>3,bit=1<<(i&7);
          if(!(covered[index]&bit)){covered[index]|=bit;total++;}
        }
        this.status(phase,Math.min(99,Math.floor(total/image.length*100)));
      }
    }
    async checkFactory() { await factoryCheck(this.pkg.factory); }
    async restoreFactory() {
      check(this.pkg.kind==='stock'&&this.identity==='FM-1_015'&&this.link&&!this.link.lost,'恢复音色需要原厂 V15');
      // Use only the four voice banks from the hash-pinned vendor package.
      // Stock reads expose the current edit buffer, not this bank address space.
      await factoryCheck(this.pkg.factory);
      check(ascii(await this.link.query(17))==='FM-1_015','恢复音色需要原厂 V15');
      const acknowledge=async(command,payload)=>{
        this.link.queue.length=0;this.link.send(frame(command,payload));
        const end=Date.now()+this.timing.idle;
        while(Date.now()<end){
          const reply=await this.link.read(Math.min(250,end-Date.now()));
          check(!this.link.lost,'USB 连接已断开');
          if(reply?.command===0){check(reply.data.length===1&&reply.data[0]===0,'设备拒绝恢复原厂音色');return;}
        }
        fail('原厂音色恢复未完成；请重新连接原厂 V15 后重试');
      };
      this.status('正在恢复四个原厂音色库',0);
      for(let bank=0;bank<16384;bank+=4096){
        await acknowledge(33,[5,0,bank>>>8,0,0]);
        for(let offset=bank;offset<bank+4096;offset+=512){
          await acknowledge(34,[5,offset&255,offset>>>8,0,0,0,2,0,...this.pkg.factory.subarray(offset,offset+512)]);
          this.status('正在恢复四个原厂音色库',Math.floor((offset+512)/16384*99));
        }
      }
      check(ascii(await this.link.query(17))==='FM-1_015','恢复音色需要原厂 V15');
    }
    async install({force=false}={}) {
      check(this.link && !this.busy,'请先连接设备'); this.busy=true;
      let failure;
      try {
        check(!this.force || force,'强制烧录尚未确认');
        check(this.mode,'请重新连接设备');
        check(this.mode!=='migration','设备已识别；请开启强制烧录并重新连接');
        const original=this.identity;
        const reply=await this.link.query(17);
        check(reply.length===27 && ascii(reply)===original,'设备身份已改变，请重新连接');
        if(this.pkg.kind==='stock')await this.checkFactory();
        if(this.pkg.kind==='stock'&&original==='FM-1_015'){
          await this.restoreFactory();this.status('更新完成',100);return;
        }
        if(this.mode!=='loader') {
          // Recheck immediately before arming; a stale Connect result is insufficient.
          if(this.mode==='tagged') {
          const cap=capability(await this.link.query(49));
          check(cap.family===this.family && cap.state===0,'设备状态已改变，请重新连接');
          check(cap.build!==this.pkg.build_id,'设备已经是这个版本');
          const n=this.pkg.image.length, tag=this.pkg.build_id.match(/../g).map(s=>parseInt(s,16));
          const ack=await this.link.query(50,[1,cap.family,n&255,(n>>>8)&255,(n>>>16)&255,(n>>>24)&255,...tag]);
          check(ack.length===1 && ack[0]===0,'设备拒绝此固件包');
          }
          this.status('准备升级程序',0); this.link.send([240,34,36,53,127,247]);
          await this.serve(0xe0000000,this.timing.idle,'准备升级程序');
          await this.disconnect(); this.status('等待升级设备重新连接',0);
          await this.waitFor(this.pkg.loaderIdentity,this.mode==='legacy'||this.pkg.kind==='stock');
        }
        this.status('写入固件',0); this.link.send([240,34,36,53,127,247]);
        await this.serve(0xf0000000,this.timing.writeIdle,'写入固件');
        await this.disconnect(); this.status('重启并核对新版本',99);
        await this.waitFor(this.pkg.product);
        if(this.pkg.kind==='stock') {
          // Stock firmware has no build-hash capability. Verify its identity
          // again after reconnect; do not claim a hash read-back it cannot do.
          check(ascii(await this.link.query(17))===this.pkg.product,'设备已重启，但固件版本校验不一致');
          await this.restoreFactory();
        } else {
          const after=capability(await this.link.query(49));
          check(after.family===this.pkg.family && after.build===this.pkg.build_id && after.state===0,'设备已重启，但固件版本校验不一致');
        }
        this.status('更新完成',100);
      } catch(error) {failure=error;throw error;}
      finally {
        this.busy=false;
        try {await this.disconnect();}
        catch(error) {if(!failure)throw error;}
      }
    }
  }
  const crc32Table=new Uint32Array([0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
    0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c]);
  function crc32(bytes,previous=0) {
    let crc=~previous;
    for(let i=0;i<bytes.length;i++){crc^=bytes[i];crc=(crc>>>4)^crc32Table[crc&15];crc=(crc>>>4)^crc32Table[crc&15];}
    return (~crc)>>>0;
  }
  function demoFrame(command,token,payload=[]) {
    check(Number.isInteger(command)&&command>=0&&command<128&&Number.isInteger(token)&&token>=0&&token<16384&&payload.length<=132,'文件协议参数无效');
    const data=[240,125,70,77,2,command,token&127,token>>7,...pack7(payload)];
    data.push((-data.slice(1).reduce((a,b)=>a+b,0))&127,247);
    return Uint8Array.from(data);
  }
  function demoParse(bytes) {
    if(!bytes||bytes.length<10||bytes.length>96||bytes[0]!==240||bytes.at(-1)!==247||bytes[1]!==125||bytes[2]!==70||bytes[3]!==77||bytes[4]!==2)return null;
    if(bytes.slice(1,-1).some(b=>b>127)||bytes.slice(1,-1).reduce((a,b)=>a+b,0)%128)return null;
    let value=0,bits=0;const out=[];
    for(const b of bytes.slice(8,-2)) { value|=b<<bits;bits+=7;if(bits>=8){out.push(value&255);value>>>=8;bits-=8;} }
    if(value||bits>=7)return null;
    return {command:bytes[5],token:bytes[6]|bytes[7]<<7,data:Uint8Array.from(out)};
  }
  function validateVoice(patch,clamp=false) {
    check(patch.length===156,'DX7 音色长度无效');
    const op=[99,99,99,99,99,99,99,99,99,99,99,3,3,7,3,7,99,1,31,99,14];
    const global=[99,99,99,99,99,99,99,99,31,7,1,99,99,99,99,1,5,7,48];
    for(let i=0;i<156;i++){
      const limit=i<126?op[i%21]:i<145?global[i-126]:i===155?63:127;
      if(clamp)patch[i]=Math.min(patch[i],limit);
      else check(patch[i]<=limit,'DX7 参数越界：'+i);
    }
    return patch;
  }
  function readVoices(bytes) {
    check(bytes instanceof Uint8Array&&(bytes.length===163||bytes.length===4104),'请选择 DX7 单音色或 32 音色 .syx 文件');
    check(bytes[0]===240&&bytes[1]===67&&bytes[2]<16&&bytes.at(-1)===247&&!bytes.slice(1,-1).some(b=>b>127),'DX7 SysEx 消息无效');
    const bank=bytes.length===4104, size=bank?4096:155;
    check(bytes[3]===(bank?9:0)&&((bytes[4]<<7)|bytes[5])===size,'DX7 数据类型或长度不匹配');
    check(bytes.slice(6,-1).reduce((a,b)=>a+b,0)%128===0,'DX7 音色校验失败');
    const voices=[];
    for(let v=0;v<(bank?32:1);v++) {
      const p=new Uint8Array(156);p[155]=63;
      if(!bank)p.set(bytes.slice(6,161));
      else {
        // Yamaha VMEM-to-VCED field mapping.
        // Reference: github.com/asb2m10/dexed/blob/master/Source/PluginData.cpp
        const b=bytes.slice(6+v*128,6+(v+1)*128);
        for(let o=0;o<6;o++) {
          const i=o*17,j=o*21;
          p.set(b.slice(i,i+11),j);
          p[j+11]=b[i+11]&3;p[j+12]=(b[i+11]>>2)&3;
          p[j+13]=b[i+12]&7;p[j+20]=(b[i+12]>>3)&15;
          p[j+14]=b[i+13]&3;p[j+15]=(b[i+13]>>2)&7;
          p[j+16]=b[i+14];p[j+17]=b[i+15]&1;p[j+18]=(b[i+15]>>1)&31;p[j+19]=b[i+16];
        }
        p.set(b.slice(102,110),126);p[134]=b[110]&31;p[135]=b[111]&7;p[136]=(b[111]>>3)&1;
        p.set(b.slice(112,116),137);p[141]=b[116]&1;p[142]=(b[116]>>1)&7;p[143]=(b[116]>>4)&7;
        p.set(b.slice(117,128),144);
      }
      // V15 banks contain a few out-of-range seven-bit VMEM values. Keep
      // the bank bytes unchanged, but expose safe editable/playable patches.
      validateVoice(p,bank);voices.push(p);
    }
    return voices;
  }
  function packVoice(patch) {
    validateVoice(patch);const b=new Uint8Array(128);
    for(let op=0;op<6;op++){
      const p=patch.subarray(op*21,op*21+21),i=op*17;
      b.set(p.subarray(0,11),i);b[i+11]=p[11]|p[12]<<2;b[i+12]=p[13]|p[20]<<3;
      b[i+13]=p[14]|p[15]<<2;b[i+14]=p[16];b[i+15]=p[17]|p[18]<<1;b[i+16]=p[19];
    }
    b.set(patch.subarray(126,134),102);b[110]=patch[134];b[111]=patch[135]|patch[136]<<3;
    b.set(patch.subarray(137,141),112);b[116]=patch[141]|patch[142]<<1|patch[143]<<4;
    b.set(patch.subarray(144,155),117);return b;
  }
  function bankFile(raw,channel=0){
    check(raw instanceof Uint8Array&&raw.length===4096&&Number.isInteger(channel)&&channel>=0&&channel<16,'DX7 音色库长度或通道无效');
    const file=Uint8Array.from([240,67,channel,9,32,0,...raw,(-raw.reduce((a,b)=>a+b,0))&127,247]);
    readVoices(file);return file;
  }
  function voiceBank(voices){
    check(voices.length===32,'请选择包含 32 个音色的 DX7 音色库');
    const raw=new Uint8Array(4096);voices.forEach((p,i)=>raw.set(packVoice(p),i*128));return raw;
  }
  function voiceFile(patch,channel=0) {
    validateVoice(patch);check(Number.isInteger(channel)&&channel>=0&&channel<16,'MIDI 通道无效');
    const voice=patch.slice(0,155),sum=voice.reduce((a,b)=>a+b,0);
    return Uint8Array.from([240,67,channel,0,1,27,...voice,(-sum)&127,247]);
  }
  function projectVersion(payload) {
    check(payload instanceof Uint8Array&&[4048,4064,4264].includes(payload.length),'项目长度无效');
    const version=payload.length===4048?3:4;
    if(payload.length===4264){
      const block=payload.subarray(4064),view=new DataView(block.buffer,block.byteOffset,block.byteLength);
      check(view.getUint32(0,true)===0x54314d46&&view.getUint32(196,true)===crc32(block.subarray(0,196))&&
        !block.subarray(4,196).includes(128),'项目调音数据无效');
    }
    check(payload[3852]===version,'项目备份格式无效');return version;
  }
  function upgradeProject(payload) {
    if(projectVersion(payload)===4)return payload;
    const result=new Uint8Array(4064);result.set(payload);result[3852]=4;
    for(let t=0;t<8;t++)result[4048+t*2]=66;
    return result;
  }
  function projectFile(payload) {
    const version=projectVersion(payload);
    const file=new Uint8Array(16+payload.length),view=new DataView(file.buffer);
    file.set([70,77,49,80,version,0,0,0]);
    view.setUint32(8,payload.length,true);view.setUint32(12,crc32(payload),true);file.set(payload,16);return file;
  }
  function tunedProject(payload) {
    payload=upgradeProject(payload);
    if(payload.length===4264)return payload;
    const data=new Uint8Array(4264),view=new DataView(data.buffer);data.set(payload);
    view.setUint32(4064,0x54314d46,true);view.setUint32(4260,crc32(data.subarray(4064,4260)),true);
    return data;
  }
  function readProjectFile(file) {
    check(file instanceof Uint8Array&&[4064,4080,4280].includes(file.length),'项目备份文件长度无效');
    check(ascii(file.slice(0,4))==='FM1P'&&[3,4].includes(file[4])&&file[5]===0&&file[6]===0&&file[7]===0,'项目备份格式无效');
    const payload=file.slice(16),view=new DataView(file.buffer,file.byteOffset,file.byteLength);
    check(file[4]===projectVersion(payload)&&view.getUint32(8,true)===payload.length&&view.getUint32(12,true)===crc32(payload),'项目备份校验失败');
    return payload;
  }
  function timbreFile(payload) {
    check(payload instanceof Uint8Array&&payload.length===223,'音色长度无效');
    const file=new Uint8Array(239),view=new DataView(file.buffer);file.set([70,77,49,84,1,0,0,0]);
    view.setUint32(8,payload.length,true);view.setUint32(12,crc32(payload),true);file.set(payload,16);return file;
  }
  function readTimbreFile(file) {
    check(file instanceof Uint8Array&&file.length===239,'音色文件长度无效');
    check(ascii(file.slice(0,4))==='FM1T'&&file[4]===1&&file[5]===0&&file[6]===0&&file[7]===0,'音色格式无效');
    const p=file.slice(16),v=new DataView(file.buffer,file.byteOffset,file.byteLength);
    check(v.getUint32(8,true)===223&&v.getUint32(12,true)===crc32(p),'音色校验失败');return p;
  }
  class Library {
    constructor(access) {this.access=access;this.link=null;this.token=crypto.getRandomValues(new Uint16Array(1))[0]&16383;this.pending=false;}
    async connect({signal}={}) {
      await this.disconnect();signal?.throwIfAborted();const found=pairs(this.access);
      check(found.length===1,found.length?'请只连接一台 FM1':'未找到支持项目库的 FM1 USB MIDI');
      const {input,output}=found[0];
      try {await portOperation(input,'open',signal);await portOperation(output,'open',signal);this.link=new Link(input,output,demoParse,signal);const info=await this.query(0);check(info.length===14&&info[1]===16&&((info[0]===3&&le(info,3,2)===4048)||(info[0]===4&&le(info,3,2)===4064)),'当前固件未支持这个项目库');this.projectBytes=le(info,3,2);await this.extensions();return info;}
      catch(e){if(this.link)await this.disconnect().catch(()=>{});else await closePorts(input,output).catch(()=>{});throw e;}
    }
    async disconnect(){if(this.link)await this.link.close();this.link=null;this.projectBytes=0;this.tuningBytes=0;}
    async extensions(){
      this.tuningBytes=0;this.programs=16;this.sampleCapacity=462848;this.trackRoles=false;this.bankAvailable=false;this.extendedDelay=false;
      try {
        const info=await this.query(21);
        check(info.length===7&&(info[0]&~1)===0&&le(info,1,2)===4264&&le(info,3,4)===462848,'设备文件长度无效');
        if(info[0]&1)this.tuningBytes=4264;
      } catch(error){if(error.deviceCode!==71)throw error;}
      try {
        const info=await this.query(22);
        check(info.length===9&&info[0]===1&&info[1]===128&&info[2]===4&&info[3]<=1&&le(info,4,4)===446464&&(info[8]&~3)===0,'设备扩展能力回复无效');
        this.programs=128;this.sampleCapacity=446464;this.bankAvailable=!!info[3];this.trackRoles=!!(info[8]&1);this.extendedDelay=!!(info[8]&2);
      } catch(error){if(error.deviceCode!==71)throw error;}
    }
    projectPayload(bytes){
      projectVersion(bytes);
      if((this.programs||16)<128){
        let copied=false;
        for(let t=0;t<8;t++){const at=2048+t*223;if(bytes[at]===0&&bytes[at+2]>=16){if(!copied){bytes=bytes.slice();copied=true;}bytes[at+2]=0;bytes[at+3]=1;}}
        // The patch is embedded in the project; only its library label changes.
      }
      check(this.extendedDelay||bytes[3849]<=127,'请先将延迟切换为旧版支持的 FREE 时间（约 353 毫秒以内），再写入旧固件');
      if(this.tuningBytes)return tunedProject(bytes);
      if(bytes.length===4264){
        check(bytes.subarray(4068,4260).every(value=>value===0),'请先更新设备固件，再写入含调音设置的项目');
        bytes=bytes.slice(0,4064);
      }
      check(this.projectBytes!==4048||bytes.length===4048,'请先更新设备固件，再写入含 ARP 设置的项目');
      return this.projectBytes===4064?upgradeProject(bytes):bytes;
    }
    async query(command,payload=[],emptyOK=false) {
      check(this.link&&!this.link.lost,'请先连接项目库');check(!this.pending,'文件传输正在进行');
      if([5,8,10].includes(command))this.onProjectChange?.();
      const link=this.link,token=this.token=(this.token+1)&16383,packet=demoFrame(command,token,payload);
      this.pending=true;
      try {
        link.queue.length=0;
        for(let attempt=0;attempt<3;attempt++) {
          link.send(packet);const until=Date.now()+1000;
          while(Date.now()<until) {
            const r=await link.read(Math.min(200,until-Date.now()));check(!link.lost,'USB 已断开，当前操作未完成');
            if(!r||r.command!==(command|64)||r.token!==token)continue;
            check(r.data.length>0,'设备回复不完整');const code=r.data[0];
            if(code!==0&&!(emptyOK&&code===1))throw Object.assign(Error(code===1?'该项目槽为空':code===73?'旧采样库占用了扩展存储空间，请先备份并缩小采样库':code===72?'内存不足，当前项目保持不变':code===71?'文件或参数无效，当前项目保持不变':'设备操作失败：'+code),{deviceCode:code});
            return r.data.slice(1);
          }
        }
        fail('设备未响应；请检查 USB 或固件是否支持项目库');
      } finally {this.pending=false;}
    }
    async download(kind,target=0) {
      if(kind===0&&this.tuningBytes)kind=3;
      const head=await this.query(1,[kind,target]),size=le(head,0,2);check(head.length===2&&(kind?size===(kind===4?4096:kind===3?4264:kind===2?223:156):[4048,4064].includes(size)),'设备文件长度无效');
      const bytes=new Uint8Array(size);
      try {for(let at=0;at<size;at+=64){const n=Math.min(64,size-at),part=await this.query(2,[at&255,at>>8,n]);check(part.length===n,'文件片段不完整');bytes.set(part,at);}return bytes;}
      finally {if(this.link&&!this.link.lost)await this.query(6).catch(()=>{});}
    }
    async saveProject(slot,bytes) {
      check(Number.isInteger(slot)&&slot>=0&&slot<16,'请选择设备项目槽');
      if(bytes){bytes=this.projectPayload(bytes);await this.upload(0,0,bytes);}
      const expected=await this.download(0);
      check(!bytes||(bytes.length===expected.length&&bytes.every((value,i)=>value===expected[i])),
        '设备工作区与待保存项目不一致，尚未写入项目槽');
      await this.query(7,[slot]);
      // Reload from persistent storage before comparing; an upload echo only
      // proves the RAM workspace changed, not that the slot was written.
      await this.query(8,[slot]);
      const saved=await this.download(0);
      check(saved.length===expected.length&&saved.every((value,i)=>value===expected[i]),
        '保存后的读回校验不一致，请重新连接并检查设备项目槽');
      return saved;
    }
    async samplesRead(progress=()=>{},signal) {
      const info=await this.query(11);check(info.length===9&&info[0]===2,'设备采样库尚未就绪，可用内置库恢复');
      const size=le(info,1,4);check(size>=512&&size<=466944,'采样库长度无效');
      const data=new Uint8Array(size);
      for(let at=0;at<size;at+=64){check(!signal?.aborted,'采样读回已取消，尚未确认写入结果');const n=Math.min(64,size-at),p=await this.query(15,[at&255,(at>>>8)&255,(at>>>16)&255,at>>>24,n]);check(p.length===n,'采样片段不完整');data.set(p,at);progress((at+n)/size);}
      return data;
    }
    async samplesWrite(data,progress=()=>{},signal) {
      check(data instanceof Uint8Array&&data.length>=512&&data.length<=(this.sampleCapacity||462848),'采样库长度无效');
      // Validate before erasing the device; every caller uses the same bank format.
      globalThis.FM1Samples.read(data);
      const info=await this.query(11);globalThis.FM1Samples.status(info);
      check(info[0]!==1&&info[0]!==3,'设备采样库忙，请稍后重试');
      const n=data.length;check(!signal?.aborted,'已取消');
      await this.query(12,[n&255,(n>>>8)&255,(n>>>16)&255,n>>>24]);
      try {
        for(let at=0;at<n;at+=128){check(!signal?.aborted,'已取消；请重新写入完整采样库恢复');const part=data.slice(at,at+128);await this.query(13,[at&255,(at>>>8)&255,(at>>>16)&255,at>>>24,...part]);progress((at+part.length)/n*0.6);}
        await this.query(14);
      } finally {if(this.link&&!this.link.lost)await this.query(16).catch(()=>{});}
      const saved=await this.samplesRead(value=>progress(0.6+value*0.39),signal);
      check(saved.length===n&&saved.every((value,i)=>value===data[i]),'设备采样读回不一致，写入未确认成功；请重试');
      progress(1);
    }
    async readBank(index){
      check(this.programs===128&&Number.isInteger(index)&&index>=0&&index<4,'请先更新固件以使用 128 音色库');
      const raw=await this.download(4,index);bankFile(raw);return raw;
    }
    async writeBank(index,raw){
      check(Number.isInteger(index)&&index>=0&&index<4,'音色库组号无效');bankFile(raw);
      await this.upload(4,index,raw);const saved=await this.readBank(index);
      check(saved.every((v,i)=>v===raw[i]),'音色库读回不一致，请重新读取检查');return saved;
    }
    async saveVoice(program,patch){
      check(Number.isInteger(program)&&program>=0&&program<128,'音色编号无效');
      const voice=packVoice(patch),index=program>>5,raw=await this.readBank(index);
      raw.set(voice,(program&31)*128);return this.writeBank(index,raw);
    }
    async upload(kind,target,bytes) {
      if(kind===1)validateVoice(bytes);
      else if(kind===2){
        check(bytes.length===223,'文件长度无效');
        if((this.programs||16)<128&&bytes[0]===0&&bytes[2]>=16){bytes=bytes.slice();bytes[2]=0;bytes[3]=1;}
      }
      else if(kind===4){check(this.programs===128,'请先更新固件以使用 128 音色库');bankFile(bytes);}
      else {
        bytes=this.projectPayload(bytes);
        if(bytes.length===4264)kind=3;
      }
      await this.query(3,[kind,target,bytes.length&255,bytes.length>>8]);
      try {
        for(let at=0;at<bytes.length;at+=64)await this.query(4,[at&255,at>>8,...bytes.slice(at,at+64)]);
        const c=crc32(bytes);await this.query(5,[c&255,(c>>>8)&255,(c>>>16)&255,c>>>24]);
      } finally {if(this.link&&!this.link.lost)await this.query(6).catch(()=>{});}
    }
  }
  globalThis.FM1Web={abortable,assetBytes,crc16,inspectPackage,prepare,pack7,frame,parse,capability,Link,pairs,Updater,
    crc32,demoFrame,demoParse,validateVoice,readVoices,voiceFile,packVoice,bankFile,voiceBank,projectVersion,upgradeProject,tunedProject,projectFile,readProjectFile,timbreFile,readTimbreFile,Library};
})();
