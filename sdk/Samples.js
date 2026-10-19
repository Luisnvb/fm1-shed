/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
(() => {
  'use strict';
  const SLOTS=8,DISK_SLOTS=16,CAPACITY=446464,REGION=466944,HEADER=512,RATE=32000,CHUNK_FRAMES=320,CHUNK_BYTES=228,MAX_FILE=32*1024*1024;
  const MAX_FRAMES=Math.floor((REGION-HEADER)/CHUNK_BYTES)*CHUNK_FRAMES;
  const check=(ok,message)=>{if(!ok)throw Error(message);};
  const crcZero=new Uint8Array(8);
  const crc=data=>{
    let value=FM1Web.crc32(data.subarray(0,12));
    value=FM1Web.crc32(crcZero.subarray(0,Math.max(0,Math.min(8,data.length-12))),value);
    return FM1Web.crc32(data.subarray(20),value);
  };
  function read(data) {
    check(data instanceof Uint8Array&&data.length>=HEADER&&data.length<=REGION,'采样库长度无效');
    const v=new DataView(data.buffer,data.byteOffset,data.byteLength);
    check(v.getUint32(0,true)===0x53314d46&&v.getUint32(4,true)===1&&v.getUint32(8,true)===data.length&&
      v.getUint32(16,true)===0&&v.getUint32(20,true)===RATE&&v.getUint32(24,true)===0&&v.getUint32(28,true)===0,'采样库格式无效');
    check(v.getUint32(12,true)===crc(data),'采样库校验失败');
    check(data.slice(480,512).every(b=>b===0),'采样库保留字段无效');
    let next=HEADER;const entries=[];
    for(let i=0;i<DISK_SLOTS;i++){
      const at=32+i*28,offset=v.getUint32(at,true),frames=v.getUint32(at+4,true),bytes=v.getUint32(at+8,true);
      const name=data.slice(at+12,at+28);
      check(offset===next&&frames<=MAX_FRAMES&&bytes===Math.ceil(frames/CHUNK_FRAMES)*CHUNK_BYTES&&bytes<=data.length-next&&name[15]===0&&
        name.every(b=>b===0||(b>=32&&b<=126)),'采样槽 '+(i+1)+' 无效');
      for(let p=offset;p<offset+bytes;p+=CHUNK_BYTES)check(v.getUint32(p,true)===0x5a144501,'SEA5 数据块无效');
      entries.push({name:String.fromCharCode(...name).replace(/\0.*$/s,''),frames,data:data.slice(offset,offset+bytes)});next+=bytes;
    }
    check(next===data.length,'采样库包含多余数据');
    check(entries.slice(SLOTS).every(s=>!s.frames&&!s.name),'采样库格式无效');
    return entries.slice(0,SLOTS);
  }
  function encodeBank(entries,capacity) {
    check(entries.length===SLOTS,'采样库必须包含 8 个槽位');
    const size=HEADER+entries.reduce((n,s)=>n+s.data.length,0);check(size<=capacity,'采样空间不足，请缩短音频或清空其他槽位');
    const data=new Uint8Array(size),v=new DataView(data.buffer);v.setUint32(0,0x53314d46,true);v.setUint32(4,1,true);
    v.setUint32(8,size,true);v.setUint32(20,RATE,true);let offset=HEADER;
    entries.forEach((s,i)=>{const at=32+i*28;v.setUint32(at,offset,true);v.setUint32(at+4,s.frames,true);v.setUint32(at+8,s.data.length,true);
      const name=s.name.replace(/[^\x20-\x7e]/g,'_').slice(0,15);data.set(Array.from(name,c=>c.charCodeAt(0)),at+12);data.set(s.data,offset);offset+=s.data.length;});
    for(let i=SLOTS;i<DISK_SLOTS;i++)v.setUint32(32+i*28,offset,true);
    v.setUint32(12,crc(data),true);read(data);return data;
  }
  function pack(entries){return encodeBank(entries,CAPACITY);}
  function status(data){
    check(data instanceof Uint8Array&&data.length===9&&data[0]<=4,'设备采样状态回复无效，未写入采样库');
    const view=new DataView(data.buffer,data.byteOffset,data.byteLength),size=view.getUint32(1,true);
    check(view.getUint32(5,true)===REGION&&(data[0]===2?size>=HEADER&&size<=REGION:size===0),'设备采样区信息不匹配，未写入采样库');
    return data[0];
  }
  async function ensure(workbench,factory,report=()=>{},progress=()=>{}){
    for(let attempt=0;attempt<10;attempt++){
      let info;
      try{info=await workbench.sampleInfo();}
      catch(error){if(attempt===9)throw error;await new Promise(resolve=>setTimeout(resolve,500));continue;}
      const state=status(info);
      if(state===2)return;
      if(state===0){
        const data=await factory();read(data);
        report('正在初始化内置采样，请保持 USB 与供电…');
        await workbench.samplesWrite(data,progress);report('内置采样初始化完成。');return;
      }
      check(state!==3,'设备采样库正在写入，请等待完成后重试');
      check(state!==4,'采样区读取或校验失败；请检查设备后手动恢复采样库');
      if(attempt<9)await new Promise(resolve=>setTimeout(resolve,500));
    }
    throw Error('采样区仍在校验，请稍后在采样页检查');
  }
  function wavInfo(data){
    check(data instanceof ArrayBuffer&&data.byteLength>=44&&data.byteLength<=MAX_FILE,'请选择不超过 32 MiB 的标准 WAV 文件');
    const v=new DataView(data),tag=at=>String.fromCharCode(...new Uint8Array(data,at,4));
    check(tag(0)==='RIFF'&&tag(8)==='WAVE'&&v.getUint32(4,true)+8===data.byteLength,'WAV 文件不完整或格式无效');
    let format,bytes;
    for(let at=12;at<data.byteLength;){
      check(at+8<=data.byteLength,'WAV 文件不完整或格式无效');
      const size=v.getUint32(at+4,true),start=at+8,end=start+size;
      check(end<=data.byteLength,'WAV 文件不完整或格式无效');
      if(tag(at)==='fmt '){
        check(!format&&size>=16,'WAV 文件不完整或格式无效');
        let kind=v.getUint16(start,true);const channels=v.getUint16(start+2,true),rate=v.getUint32(start+4,true),align=v.getUint16(start+12,true),bits=v.getUint16(start+14,true);
        if(kind===0xfffe){
          check(size>=40&&v.getUint16(start+16,true)>=22&&v.getUint16(start+18,true)===bits&&
            [0,0,16,0,128,0,0,170,0,56,155,113].every((n,i)=>v.getUint8(start+28+i)===n),'不支持此 WAV 编码，请导出 PCM 或 32 位浮点 WAV');
          kind=v.getUint32(start+24,true);
        }
        check((kind===1&&[8,16,24,32].includes(bits))||(kind===3&&bits===32),'不支持此 WAV 编码，请导出 PCM 或 32 位浮点 WAV');
        check((channels===1||channels===2)&&rate>=8000&&rate<=192000,'WAV 仅支持单声道或立体声，采样率 8–192 kHz');
        check(align===channels*bits/8&&v.getUint32(start+8,true)===rate*align,'WAV 文件不完整或格式无效');
        format={channels,rate,align,bits};
      }else if(tag(at)==='data'){check(bytes===undefined,'WAV 文件不完整或格式无效');bytes=size;}
      at=end+(size&1);
      check(at<=data.byteLength,'WAV 文件不完整或格式无效');
    }
    check(format&&bytes>0&&bytes%format.align===0,'WAV 文件不完整或格式无效');
    const duration=bytes/format.align/format.rate;
    check(duration<=120,'WAV 原文件最长 120 秒，请先裁剪后再导入');
    return {...format,duration};
  }
  function availableFrames(entries,slot){
    check(Number.isInteger(slot)&&slot>=0&&slot<SLOTS,'请选择有效采样槽');
    const used=HEADER+entries.reduce((sum,s,i)=>sum+(i===slot?0:s.data.length),0);
    return Math.max(0,Math.floor((CAPACITY-used)/CHUNK_BYTES)*CHUNK_FRAMES);
  }
  globalThis.FM1Samples={read,pack,crc,status,ensure,wavInfo,availableFrames,capacity:CAPACITY,slots:SLOTS};
  if(typeof document==='undefined')return;
  const $=id=>document.getElementById(id),say=s=>{FM1I18n.text($('sample-status'),s);};
  let entries,bank,decoded,audio,playing,worker,operation=0,busy=false,cancel,pendingAudio=false;
  function refresh(){
    const select=$('sample-slot'),old=select.value;select.replaceChildren();
    entries.forEach((s,i)=>{const o=document.createElement('option');o.value=i;FM1I18n.text(o,(i+1)+' · '+(s.name||'空槽')+' · '+(s.frames/RATE).toFixed(2)+' 秒');select.append(o);});
    select.value=old||'0';bank=encodeBank(entries,REGION);
    FM1I18n.text($('sample-space'),'已用 '+((bank.length-HEADER)/1024).toFixed(1)+' / '+((CAPACITY-HEADER)/1024).toFixed(1)+' KiB；余 '+(Math.max(0,Math.floor((CAPACITY-bank.length)/CHUNK_BYTES))*CHUNK_FRAMES/RATE).toFixed(2)+' 秒（按 SEA5 估算）。');
    limits();
  }
  function limits(){if(entries)FM1I18n.text($('sample-limit'),'当前槽最多可放 '+(availableFrames(entries,Number($('sample-slot').value))/RATE).toFixed(2)+' 秒；写入会自动导入当前选段。');}
  function enable(){for(const e of document.querySelectorAll('[data-sample]'))e.disabled=busy||!entries;
    for(const id of ['sample-audition','sample-replace','sample-name','sample-start','sample-end'])$(id).disabled=busy||!decoded;
    for(const id of ['sample-read','sample-write'])$(id).disabled||=!navigator.requestMIDIAccess||FM1Workbench.state().busy;
    FM1I18n.text($('sample-write'),pendingAudio?'导入选段并写入设备':'写入设备采样库');$('sample-cancel').disabled=!cancel&&!playing;}
  document.addEventListener('fm1-device-state',enable);
  async function action(fn){if(busy)return;busy=true;progress(0);enable();try{await fn();}catch(e){say(e.message);}finally{busy=false;cancel=undefined;enable();}}
  const progress=value=>{$('sample-progress').value=Math.round(value*100);};
  function save(data,name){const u=URL.createObjectURL(new Blob([data])),a=document.createElement('a');a.href=u;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(u),1000);}
  function moduleBytes(){return FM1Web.assetBytes(FM1_SIMULATOR,2097152,'网页音源库校验失败');}
  async function factory(){const c=new WebAssembly.Instance(await WebAssembly.compile(await moduleBytes()),{}).exports;c._initialize();const at=c.sim_factory(),bytes=c.sim_factory_size();if(!at||!bytes)throw Error('内置采样载入失败');return new Uint8Array(c.memory.buffer,at,bytes).slice();}
  function encoderWorker(){
    onmessage=({data:m})=>{try{
      const c=new WebAssembly.Instance(new WebAssembly.Module(m.wasm),{}).exports;c._initialize();
      const input=c.sim_sample_alloc(m.pcm.length);if(!input)throw Error('音频太长或内存不足');
      new Int16Array(c.memory.buffer,input,m.pcm.length).set(m.pcm);const p=c.sim_sample_encode(),n=c.sim_sample_bytes();
      if(!p||!n)throw Error('SEA5 编码失败');const data=new Uint8Array(c.memory.buffer,p,n).slice();postMessage({data},[data.buffer]);
    }catch(e){postMessage({error:e.message});}};
  }
  async function encode(pcm){
    const wasm=await moduleBytes();
    return new Promise((resolve,reject)=>{
      const url=URL.createObjectURL(new Blob(['('+encoderWorker.toString()+')()'],{type:'text/javascript'}));
      try{worker=new Worker(url);}finally{URL.revokeObjectURL(url);}
      const token=++operation,done=(error,data)=>{if(token!==operation)return;++operation;clearTimeout(timer);worker?.terminate();worker=undefined;error?reject(Error(error)):resolve(data);};
      cancel={abort:()=>done('已取消编码，采样库保持不变')};enable();
      const timer=setTimeout(()=>done('编码超时，请缩短音频后重试'),120000);
      worker.onmessage=e=>done(e.data.error,e.data.data);worker.onerror=()=>done('编码线程失败');
      try{worker.postMessage({wasm,pcm},[wasm.buffer,pcm.buffer]);}catch(e){done(e.message);}
    });
  }
  function trim(){
    check(decoded,'请先选择 WAV 音频');const start=Number($('sample-start').value),end=Number($('sample-end').value);
    check(Number.isFinite(start)&&Number.isFinite(end)&&start>=0&&end>start&&end<=decoded.duration+0.0001,'裁剪起止时间无效');
    const first=Math.round(start*RATE),last=Math.min(decoded.length,Math.round(end*RATE)),n=last-first;
    check(n>0&&n<=MAX_FRAMES,'选段超出采样库总容量，请缩短选段');
    check(n<=availableFrames(entries,Number($('sample-slot').value)),'空间不足：请缩短选段或清空其他槽位');return {first,n};
  }
  $('sample-wav').onchange=()=>action(async()=>{
    const f=$('sample-wav').files[0];$('sample-wav').value='';if(!f)return;
    decoded=undefined;pendingAudio=false;check(f.size<=MAX_FILE,'音频文件不能超过 32 MiB');
    const data=await f.arrayBuffer();wavInfo(data);
    const context=new OfflineAudioContext(1,1,RATE);
    try{decoded=await context.decodeAudioData(data);}catch{throw Error('无法解码 WAV，请导出 PCM 或 32 位浮点 WAV 后重试');}
    pendingAudio=true;$('sample-start').value='0';$('sample-end').value=(Math.floor(Math.min(decoded.duration,availableFrames(entries,Number($('sample-slot').value))/RATE)*RATE)/RATE).toFixed(5);$('sample-name').value=f.name.replace(/\.wav$/i,'').replace(/[^\x20-\x7e]/g,'_').slice(0,15);
    say('WAV 已载入，尚未写入设备。可试听、调整选段，再点“导入选段并写入设备”。');
  });
  for(const id of ['sample-start','sample-end','sample-name','sample-slot'])$(id).addEventListener(id==='sample-slot'?'change':'input',()=>{pendingAudio=!!decoded;limits();enable();});
  $('sample-audition').onclick=()=>action(async()=>{
    const {first,n}=trim();if(!audio)audio=new AudioContext();
    let timer;try{await Promise.race([audio.resume(),new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('浏览器未开启声音，编辑仍可使用。')),2500);})]);}finally{clearTimeout(timer);}
    playing?.stop();
    const b=audio.createBuffer(1,n,RATE),p=b.getChannelData(0);for(let ch=0;ch<decoded.numberOfChannels;ch++){const src=decoded.getChannelData(ch);for(let i=0;i<n;i++)p[i]+=src[first+i]/decoded.numberOfChannels;}
    playing=audio.createBufferSource();playing.buffer=b;playing.connect(audio.destination);
    const source=playing;source.onended=()=>{source.disconnect();if(playing===source)playing=undefined;enable();};
    playing.start();say('试听裁剪后的单声道音频。');
  });
  async function replaceSelected(){
    const {first,n}=trim(),slot=Number($('sample-slot').value),size=HEADER+entries.reduce((sum,s,i)=>sum+(i===slot?Math.ceil(n/CHUNK_FRAMES)*CHUNK_BYTES:s.data.length),0);
    check(size<=CAPACITY,'空间不足：请缩短选段或清空其他槽位');
    const channels=Array.from({length:decoded.numberOfChannels},(_,ch)=>decoded.getChannelData(ch)),pcm=new Int16Array(n);
    for(let i=0;i<n;i++){let v=0;for(let ch=0;ch<channels.length;ch++)v+=channels[ch][first+i]/channels.length;check(Number.isFinite(v),'WAV 包含无效音频数值');pcm[i]=Math.max(-32768,Math.min(32767,Math.round(v*32768)));}
    say('正在编码采样…');const data=await encode(pcm);
    const next=entries.slice();next[slot]={name:$('sample-name').value||'SAMPLE',frames:n,data};pack(next);entries=next;pendingAudio=false;refresh();say('已替换网页采样库中的槽 '+(slot+1)+'。尚未写入设备。');
  }
  $('sample-replace').onclick=()=>action(replaceSelected);
  $('sample-empty').onclick=()=>action(async()=>{const slot=Number($('sample-slot').value);if(!confirm(FM1I18n.t('清空网页采样库槽 '+(slot+1)+'？写入设备前仍不会修改实机。')))return;entries[slot]={name:'',frames:0,data:new Uint8Array()};decoded=undefined;pendingAudio=false;refresh();say('网页槽位已清空。');});
  $('sample-factory').onclick=()=>action(async()=>{if(!confirm(FM1I18n.t('用 8 个内置音色替换网页采样库？设备暂不修改。')))return;entries=read(await factory());decoded=undefined;pendingAudio=false;refresh();say('内置采样已载入网页，可写入设备恢复。');});
  $('sample-read').onclick=()=>action(async()=>{say('正在读取设备完整采样库…');entries=read(await FM1Workbench.samplesRead(progress));decoded=undefined;pendingAudio=false;refresh();say('设备采样已读取；导出备份后再编辑更方便恢复。');});
  $('sample-write').onclick=()=>action(async()=>{
    if(pendingAudio)trim();read(bank);if(!confirm(FM1I18n.t('写入整个 8 槽采样库？设备播放会暂停。写入中断需重新写入完整采样库；项目槽不变。')))return;
    if(pendingAudio)await replaceSelected();
    cancel=new AbortController();enable();say('正在写入并读回采样库，请保持供电与 USB 连接…');await FM1Workbench.samplesWrite(bank,progress,cancel.signal);say('采样库已写入，设备读回一致。请选择采样器引擎的对应槽重新触发；808 是独立合成引擎。');
  });
  $('sample-sim').onclick=()=>action(async()=>{if(pendingAudio)await replaceSelected();await FM1SamplePreview(bank);say('已送入仿真；请选择采样器轨道演奏。');});
  $('sample-export').onclick=()=>action(async()=>{if(pendingAudio)await replaceSelected();save(bank,'FM1-samples.fm1s');});
  $('sample-bank-file').onchange=()=>action(async()=>{const f=$('sample-bank-file').files[0];$('sample-bank-file').value='';if(!f)return;check(f.size<=REGION,'采样库过大');entries=read(new Uint8Array(await f.arrayBuffer()));decoded=undefined;pendingAudio=false;refresh();say('采样库文件已载入网页，尚未写入设备。');});
  $('sample-cancel').onclick=()=>{cancel?.abort();playing?.stop();playing=undefined;enable();};
  globalThis.FM1EnsureSamples=(workbench=FM1Workbench)=>ensure(workbench,factory,say,progress);
  action(async()=>{entries=read(await factory());refresh();say('已载入网页内置采样库。编辑前可先读取设备库，保留已有自定义采样。');});
})();
