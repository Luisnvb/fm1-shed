/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Modified 2026-10-17: reserve the final flash sector during sample imports. */
(() => {
  'use strict';
  // One portable C instance owns the panel, project and sound state. Before
  // audio is enabled it runs on the page; its complete memory moves to the
  // audio thread at a C-call boundary, preserving edits and the current page.
  function coreRuntime() {
    return class FM1Core {
      constructor(wasm, flash, snapshot) {
        this.c = new WebAssembly.Instance(new WebAssembly.Module(wasm), {}).exports;
        const c = this.c; c._initialize();
        if (snapshot) {
          if (snapshot.byteLength % 65536 || snapshot.byteLength < c.memory.buffer.byteLength || snapshot.byteLength > 33554432) throw Error('仿真状态无效');
          c.memory.grow((snapshot.byteLength - c.memory.buffer.byteLength) / 65536);
          new Uint8Array(c.memory.buffer).set(new Uint8Array(snapshot));
        } else {
          const saved = flash?.length === 536576;
          if (saved) new Uint8Array(c.memory.buffer,c.sim_flash(),536576).set(flash);
          c.sim_boot(saved ? 0 : 1);
        }
        this.lastFrame = this.lastFlash = -1;
      }
      command(message) {
        const c = this.c; let result = 0, data;
        switch(message.type) {
          case 'control': c.sim_control(message.kind,message.index,message.value); break;
          case 'release': c.sim_release(); break;
          case 'panic': c.sim_panic(); break;
          case 'tapTempo': c.sim_tap_tempo(); break;
          case 'select': result=c.sim_select(message.track,message.source); break;
          case 'open': result=c.sim_open(message.page); if(result)throw Error('当前音源不支持此编辑页。'); break;
          case 'preset': result=c.sim_preset(message.index); break;
          case 'step': result=c.sim_step(message.index,message.toggle?1:0); break;
          case 'lcd': result=c.sim_lcd(message.context,message.action,message.index,message.owner,message.delta); break;
          case 'midi':
            if (!message.data || message.data.length>1024) throw Error('MIDI 数据无效');
            new Uint8Array(c.memory.buffer,c.sim_file(),message.data.length).set(message.data);
            result=c.sim_midi(message.data.length); break;
          case 'export':
            result=message.timbre?c.sim_timbre_export():message.patch?c.sim_patch_export():c.sim_export();
            if (!result) data=new Uint8Array(c.memory.buffer,c.sim_file(),message.timbre?223:message.patch?156:4264).slice();
            break;
          case 'snapshot':
            result=c.sim_export();
            if(!result)data={bytes:new Uint8Array(c.memory.buffer,c.sim_file(),4264).slice(),revision:this.projectRevision()};
            break;
          case 'factory':
            result=c.sim_factory_project(message.index);
            if(!result)data=new Uint8Array(c.memory.buffer,c.sim_file(),4264).slice();
            break;
          case 'factories':
            data=[];
            for(let i=0;i<64;i++){
              const at=c.sim_factory_name(i);if(!at)break;
              const bytes=new Uint8Array(c.memory.buffer,at,13);let name='';
              for(const byte of bytes){if(!byte)break;name+=String.fromCharCode(byte);}
              data.push(name);
            }
            break;
          case 'import':
            if (!message.data || (message.timbre?message.data.length!==223:message.patch?message.data.length!==156:![4048,4064,4264].includes(message.data.length))) throw Error('文件长度无效');
            new Uint8Array(c.memory.buffer,c.sim_file(),message.data.length).set(message.data);
            result=message.timbre?c.sim_timbre_import():message.patch?c.sim_patch_import():c.sim_import(message.data.length); break;
          case 'library-read':
            result=c.sim_library_read(message.bank);if(!result)data=new Uint8Array(c.memory.buffer,c.sim_file(),4096).slice();break;
          case 'library-write':
            if(!(message.data instanceof Uint8Array)||message.data.length!==4096)throw Error('音色库长度无效');
            new Uint8Array(c.memory.buffer,c.sim_file(),4096).set(message.data);result=c.sim_library_write(message.bank);break;
          case 'clear': c.sim_clear(); break;
          case 'samples': {
            if (!(message.data instanceof Uint8Array) || message.data.length<512 || message.data.length>446464) throw Error('采样库长度无效');
            const flash=new Uint8Array(c.memory.buffer,c.sim_flash()+69632,466944),previous=flash.slice();
            // Keep the final 20480 bytes (DX7 library and per-project tuning). The full backup
            // still permits rollback of a pre-cap bank on validation failure.
            flash.fill(255,0,446464);flash.set(message.data);
            result=c.sim_sample_bank(message.data.length);
            if(result){
              new Uint8Array(c.memory.buffer,c.sim_flash()+69632,466944).set(previous);
              c.sim_sample_bank(previous.length);
            }
            break;
          }
          case 'tick': for(let i=0;i<Math.min(32,message.blocks>>>0);i++)c.sim_render();break;
          default: throw Error('未知仿真操作');
        }
        if(result)throw Error(result===-8?'内存不足，当前项目保持不变':'当前音源或参数不支持此操作');
        return data;
      }
      projectRevision(){return this.c.sim_edit_revision()+':'+this.c.sim_project_revision();}
      frame(force=false) {
        const c=this.c,message={type:'frame',state:new Uint8Array(c.memory.buffer,c.sim_state(),225).slice()};
        const name=new Uint8Array(c.memory.buffer,c.sim_project_name(),12),position=c.sim_preset_position();
        const targets=c.sim_lcd_targets(),count=c.sim_lcd_count();
        const labels=new Uint8Array(c.memory.buffer,c.sim_knobs(),128);
        message.screen={first:c.sim_step_first(),knobs:Array.from({length:4},(_,i)=>[0,1].map(field=>String.fromCharCode(...labels.slice(i*32+field*16,i*32+field*16+16)).replace(/\0.*$/s,'')))};
        const bytes=new Uint8Array(c.memory.buffer,targets,count*64),words=new Int32Array(c.memory.buffer,targets,count*16);
        message.screen.context=c.sim_lcd_context();message.screen.mask=c.sim_lcd_mask();
        message.screen.targets=Array.from({length:count},(_,i)=>({
          rect:Array.from(words.slice(i*16,i*16+4)),action:words[i*16+4],index:words[i*16+5],owner:words[i*16+6],
          label:String.fromCharCode(...bytes.slice(i*64+32,i*64+48)).replace(/\0.*$/s,''),
          value:String.fromCharCode(...bytes.slice(i*64+48,i*64+64)).replace(/\0.*$/s,'')
        }));
        message.workspace={name:String.fromCharCode(...name).trim(),slot:position&255,factory:(position&768)===768,dirty:!!c.sim_project_dirty(),revision:this.projectRevision()};
        message.countIn=c.sim_count_in();
        const revision=c.sim_frame_revision();
        if(force||revision!==this.lastFrame){
          const changed=c.sim_frame_changes(),bands=force||this.lastFrame<0?32767:changed;
          const source=new Uint16Array(c.memory.buffer,c.sim_pixels(),57600);
          if(bands===32767)message.pixels=source.slice();
          else {
            let count=0;for(let bits=bands;bits;bits&=bits-1)count++;
            message.pixels=new Uint16Array(count*3840);
            for(let band=0,at=0;band<15;){
              if(!(bands&(1<<band))){band++;continue;}
              const first=band;while(band<15&&(bands&(1<<band)))band++;
              const run=source.subarray(first*3840,band*3840);message.pixels.set(run,at);at+=run.length;
            }
          }
          message.pixelBands=bands;this.lastFrame=revision;
        }
        const flash=c.sim_flash_revision();
        if(flash!==this.lastFlash){message.flash=new Uint8Array(c.memory.buffer,c.sim_flash(),536576).slice();this.lastFlash=flash;}
        return message;
      }
    };
  }
  function audioThread(createCore) {
    const Core=createCore();
    class FM1Processor extends AudioWorkletProcessor {
      constructor(options) {
        super(); const config=options.processorOptions;
        this.core=new Core(config.wasm,config.flash,config.snapshot);this.c=this.core.c;
        this.blockFrames=this.c.sim_block_frames();this.position=this.blockFrames;this.fraction=0;this.ratio=44100/sampleRate;this.count=0;
        this.a=new Float32Array(2);this.b=new Float32Array(2);this.resetAudio();
        this.port.onmessage=e=>this.command(e.data);this.publish(true);this.port.postMessage({type:'ready'});
      }
      render() {
        this.audioAt=this.c.sim_render();this.position=0;
        // Rebind only after memory grows. The C output block has a fixed address.
        if(!this.buffer||this.buffer.buffer!==this.c.memory.buffer)
          this.buffer=new Float32Array(this.c.memory.buffer,this.audioAt,this.blockFrames*2);
      }
      next(target) {
        if(this.position===this.blockFrames)this.render();
        const at=this.position++*2;target[0]=this.buffer[at];target[1]=this.buffer[at+1];
      }
      resetAudio(){this.position=this.blockFrames;this.fraction=0;this.next(this.a);this.next(this.b);}
      command(message) {
        try {
          const data=this.core.command(message);
          if(['panic','import','clear'].includes(message.type))this.resetAudio();
          if(message.id)this.port.postMessage({id:message.id,data},ArrayBuffer.isView(data)?[data.buffer]:[]);
          this.publish();
        }catch(error){this.port.postMessage({id:message.id,error:error.message});}
      }
      publish(force=false){const message=this.core.frame(force);this.port.postMessage(message,Object.values(message).filter(v=>ArrayBuffer.isView(v)).map(v=>v.buffer));}
      process(inputs,outputs) {
        const out=outputs[0];if(!out?.[0])return true;
        const left=out[0],right=out[1],length=left.length;
        if(this.buffer.buffer!==this.c.memory.buffer)
          this.buffer=new Float32Array(this.c.memory.buffer,this.audioAt,this.blockFrames*2);
        if(this.ratio===1&&length>=2) {
          // Keep the same two-sample lookahead and C render boundaries as the
          // resampler, while copying whole runs at the native 44.1 kHz rate.
          left[0]=this.a[0];left[1]=this.b[0];
          if(right){right[0]=this.a[1];right[1]=this.b[1];}
          for(let i=2;i<length;) {
            if(this.position===this.blockFrames)this.render();
            const count=Math.min(this.blockFrames-this.position,length-i),start=this.position*2;
            if(right)for(let j=0;j<count;j++){left[i+j]=this.buffer[start+j*2];right[i+j]=this.buffer[start+j*2+1];}
            else for(let j=0;j<count;j++)left[i+j]=this.buffer[start+j*2];
            this.position+=count;i+=count;
          }
          this.next(this.a);this.next(this.b);
        } else for(let i=0;i<length;i++) {
          left[i]=this.a[0]+(this.b[0]-this.a[0])*this.fraction;
          if(right)right[i]=this.a[1]+(this.b[1]-this.a[1])*this.fraction;
          this.fraction+=this.ratio;
          while(this.fraction>=1){this.fraction--;const spare=this.a;this.a=this.b;this.b=spare;this.next(this.b);}
        }
        this.count+=length;if(this.count>=sampleRate/20){this.count%=sampleRate/20;this.publish();}return true;
      }
    }
    registerProcessor('fm1-engine',FM1Processor);
  }
  globalThis.FM1SimulatorCore=coreRuntime();
  globalThis.FM1SimulatorAudioSource='('+audioThread.toString()+')('+coreRuntime.toString()+');';

  if (typeof document === 'undefined') return;
  const $ = id=>document.getElementById(id), sources=['DX7','303','808','PSG','PLUCK','SAMPLER'];
  const storageKey='fm1.simulator.flash.v4',pending=new Map();
  let context,node,gain,analyser,meterData,state,workspace,preview,wasm,ready=false,starting=false,audioOn=false,handoff=false,sequence=0,selectedFile,fileRevision=0,voices=[],midiInput,midiAccess,midiConnecting=false,presetBusy=false;
  const waiting=[],wakingKeys=new Map();let startTask,wakingPlay,muted=false;
  const say=text=>{FM1I18n.text($('sim-status'),text);};
  const image=$('sim-screen').getContext('2d'),pixels=image.createImageData(240,240);
  const rgb5=new Uint8ClampedArray(32),rgb6=new Uint8ClampedArray(64);
  for(let i=0;i<32;i++)rgb5[i]=i*255/31;
  for(let i=0;i<64;i++)rgb6[i]=i*255/63;
  for(let i=3;i<pixels.data.length;i+=4)pixels.data[i]=255;
  function encode(bytes){let text='';for(let i=0;i<bytes.length;i+=16384)text+=String.fromCharCode(...bytes.subarray(i,i+16384));return btoa(text);}
  function decode(text){return Uint8Array.from(atob(text),c=>c.charCodeAt(0));}
  function save(bytes,name){const url=URL.createObjectURL(new Blob([bytes],{type:'application/octet-stream'})),a=document.createElement('a');a.href=url;a.download=name;document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);}
  function availability() {
    for(const element of ui.controls)element.disabled=!ready;
    $('sim-start').disabled=starting||!ready;
    $('sim-play-control').disabled=starting||!ready;
    $('sim-midi').disabled=!ready||midiConnecting||!navigator.requestMIDIAccess;
    FM1I18n.text($('sim-start'),starting?'正在开启声音…':muted?'取消静音':'静音');
    $('sim-start').setAttribute('aria-pressed',String(muted));
    $('sim-patch-export').disabled=!ready||!state||state[8+state[0]*20]!==0;
    $('sim-file-apply').disabled=!ready||!selectedFile||(selectedFile.patch&&(!state||state[8+state[0]*20]!==0));
    $('sim-placeholder').hidden=ready;
    FM1I18n.text($('sim-engine-state'),ready?'面板已就绪':'正在载入');
    FM1I18n.text($('sim-audio-state'),audioOn?'声音已开启':muted?'静音编辑':'点琴键即可发声');
    $('sim-engine-state').hidden=ready;$('sim-audio-state').hidden=!ready;
    for(const id of ['sim-preset-prev','sim-preset-next','sim-factory','sim-factory-load'])$(id).disabled=!ready||presetBusy;
    $('sim-preset-prev').disabled||=!!workspace?.factory&&workspace.slot===0;
    $('sim-preset-next').disabled||=!!workspace?.factory&&workspace.slot===15;
    $('sim-factory-load').disabled||=$('sim-factory').value==='';
    $('sim-factory-export').disabled=!ready||$('sim-factory').value==='';
    for(const id of ['sim-save-device','sim-read-device'])$(id).disabled=!ready||syncDevice.busy||!navigator.requestMIDIAccess;
  }
  let syncDevice={mode:'offline',epoch:0,busy:false},deviceSnapshot,deviceSnapshotKind;
  let comparedRevision,comparisonMatches=false,syncTimer,syncChecking=false,syncGeneration=0,syncText='';
  function clearDeviceSnapshot(){
    deviceSnapshot=undefined;comparedRevision=undefined;comparisonMatches=false;++syncGeneration;
    clearTimeout(syncTimer);syncTimer=undefined;renderSync();
  }
  function rememberDeviceSnapshot(bytes,kind){
    deviceSnapshot=FM1Web.tunedProject(bytes).slice();deviceSnapshotKind=kind;
    comparedRevision=undefined;comparisonMatches=false;++syncGeneration;renderSync();
  }
  async function compareDeviceSnapshot(){
    syncTimer=undefined;if(syncChecking||!deviceSnapshot||!ready)return;
    syncChecking=true;const generation=syncGeneration,expected=deviceSnapshot;
    try{
      const snapshot=await request('snapshot');
      if(generation!==syncGeneration)return;
      comparedRevision=snapshot.revision;
      comparisonMatches=snapshot.bytes.length===expected.length&&snapshot.bytes.every((v,i)=>v===expected[i]);
    }catch(_){comparisonMatches=false;}
    finally{syncChecking=false;renderSync();}
  }
  function renderSync(){
    let title='网页试听 · 未连接设备',detail='',style='preview';
    if(syncDevice.mode==='update')title='设备已连接用于更新 · 网页试听独立';
    if(syncDevice.mode==='library'){
      const changed=deviceSnapshot&&(!comparisonMatches||comparedRevision!==workspace?.revision);
      title=!deviceSnapshot?'设备已连接 · 请读取项目':changed?'网页修改未写入设备':deviceSnapshotKind==='save'?'已保存到设备 · 校验通过':'已读取设备项目';
      style=!deviceSnapshot?'preview':changed?'pending':'verified';
      detail='网页修改需手动保存；实机更改后请重新读取。';
      if(deviceSnapshot&&ready&&comparedRevision!==workspace?.revision&&!syncTimer&&!syncChecking)
        syncTimer=setTimeout(compareDeviceSnapshot,120);
    }
    if(syncDevice.connection){
      title=syncDevice.connection.failed?'设备连接失败':'正在连接设备…';
      detail=syncDevice.connection.message;style='pending';
    }
    const text=title+'|'+detail+'|'+style;
    if(text===syncText)return;syncText=text;
    $('device-sync-status').dataset.state=style;
    FM1I18n.text($('device-sync-title'),title);FM1I18n.text($('device-sync-detail'),detail);
  }
  document.addEventListener('fm1-device-state',event=>{
    const changed=event.detail.epoch!==syncDevice.epoch;syncDevice=event.detail;
    if(changed)clearDeviceSnapshot();else renderSync();availability();
  });
  document.addEventListener('fm1-device-project-change',clearDeviceSnapshot);
  document.addEventListener('fm1-device-project-saved',event=>rememberDeviceSnapshot(event.detail.bytes,'save'));
  function dispatch(message) {
    if(handoff){waiting.push(message);return;}
    if(node){node.port.postMessage(message);return;}
    if(!preview)return;
    try {const data=preview.command(message);if(message.id)receive({id:message.id,data});receive(preview.frame());}
    catch(error){receive({id:message.id,error:error.message});}
  }
  function send(type,args={}){if(ready)dispatch({type,...args});}
  function request(type,args={}) {
    if(!ready)return Promise.reject(Error('仿真尚未载入'));
    // These operations release/reset the C controls. Clear their page-side
    // owners too, so a stale latch cannot suppress the next press.
    if(['select','import','clear','open','step'].includes(type))release();
    // Presets retain already sounding Play keys, but must cancel gestures
    // still waiting for audio permission on the previous project.
    if(type==='preset'){wakingPlay=undefined;wakingKeys.clear();}
    const id=++sequence;return new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>{pending.delete(id);reject(Error('仿真操作超时'));},7000);
      pending.set(id,{resolve,reject,timer});dispatch({type,id,...args});
    });
  }
  globalThis.FM1SamplePreview = data => request('samples', {data});
  async function select(track,source) {
    try {await request('select',{track,source});}
    catch(error){
      // Failed allocation leaves the C project unchanged. Restore the visible
      // selection too; an unchanged frame will not rewrite these controls.
      if(state){$('sim-track').value=state[0];$('sim-source').value=state[8+state[0]*20];}
      throw error;
    }
  }

  function receive(message) {
    if(message.id){const item=pending.get(message.id);if(item){clearTimeout(item.timer);pending.delete(message.id);message.error?item.reject(Error(message.error)):item.resolve(message.data);}return;}
    if(message.error){say(message.error);return;}
    if(message.type!=='frame')return;
    const before=state;state=message.state;
    const contextChanged=!before||before[1]!==state[1]||before[0]!==state[0]||before[8+before[0]*20]!==state[8+state[0]*20];
    const fm=state[8+state[0]*20]===0;
    if(before&&contextChanged){
      wakingPlay=undefined;wakingKeys.clear();
      const pageTips={
        0:'总览：SELECT 选轨，K3（STEP）选步；PRESETS 换预设，ALGORITHM／K2 换音源。',
        1:'SEQ：ALGORITHM 翻底部八页，PRESETS 选步，SELECT 选轨，OCT−／OCT+ 换步进页；SYNC / LOOP 页设时钟，TRACK ROLE 页设角色与输入通道，长按 SEQ 复制与更多。',
        2:'FM 详细编辑：黑键选择算子／音高／全局，SELECT 翻参数页，K1–K4 调节；长按 OP 键开关算子。',
        3:fm?'FM 总包络：K1 起音，K2 衰减，K3 保持，K4 释放；各算子包络在 EDIT 详细编辑页调节。':'非 FM 包络：K1–K4 调屏幕列出的真实参数，SELECT 选轨；鼓／采样用 PRESETS 或白键选声部，重新触发试听。',
        4:fm?'FM LFO：SELECT 翻参数页，K1–K4 调节；LFO 返回演奏。':'轨道 LFO：K1 速度，K2 深度，K3 音量／声像，K4 波形；深度 0 为关闭，SELECT 选轨。参数随项目和音色保存。',
        5:'音色编辑：K1–K4 调当前四项；短按 EDIT 打开的常用页用 SELECT 选轨，长按后的细调页用 SELECT 翻页。PRESETS 选音色／鼓声部。',
        6:'混音：白键 1–8 静音、9–16 独奏，同轨互斥；SELECT 选轨，K1–K4 调当前四轨组音量。',
        7:'蓝牙：K1 总开关，K2 音频，K3 BLE 角色，K4 时钟；PRESETS 选屏幕下方操作，SAVE 执行，清除配对需再次确认。无线连接请在实机操作。',
        8:'保存项目：SELECT／PRESETS 选 USER 槽，再按 SAVE 保存；覆盖其他已有槽需确认，成功后自动返回。',
        9:'FM 音色命名：SELECT 选字符，K1 改字符；EDIT 返回编辑，HOME 返回轨道。',
        10:'效果：PRESETS 切 TRACK／SHARED／DELAY。DELAY 页 K1 开关 SYNC、K2 调分频或毫秒、K3 调 BPM、K4 调反馈；连按 SEQ 或 TAP 打拍。',
        11:'音源预览：ALGORITHM／K2／SELECT／PRESETS 选择，SAVE 确认，HOME 取消；确认前保留当前音色。',
        12:'复制与粘贴：SELECT 选操作，SAVE 执行；复制后自动选中 PASTE。PRESETS／K2 选目标步，OCT−／OCT+ 选目标页，再按一次 SAVE 粘贴。',
        15:'FM 常用参数：K1 算法、K2 反馈、K3 移调、K4 音量；PRESETS 换当前轨音色。短按 EDIT 返回，长按进细调。',
        17:'ARP：短按开关，长按调参数；ALGORITHM 翻页，第一页调分频／方向／音域／门长，第二页调 Swing／Hold。SELECT 选轨，开关和参数随项目保存，不保存按住的音符。',
        16:'预设列表：PRESETS／SELECT 直接载入，PLAY 载入播放，SAVE 打开项目保存；K1 选 USER／FACTORY。切换直接生效，不提示保存。'
      };
      if(pageTips[state[1]])say(pageTips[state[1]]);
    }
    if(contextChanged){
      for(const button of document.querySelectorAll('[data-sim-page]'))button.setAttribute('aria-pressed',String(Number(button.dataset.simPage)===state[1]||(Number(button.dataset.simPage)===15&&state[1]===5)));
      const presetUse={0:'换整套预设',1:'选步进',3:fm?'开关 FM 包络':[2,5].includes(state[8+state[0]*20])?'选择鼓／采样声部':'此页不使用',4:'此页不使用',5:'换当前轨音色／鼓声部',8:'选保存槽位',10:'切换轨道发送／共用细调',12:'选步进',13:'选步进',15:'换当前轨音色',16:'选整套预设',17:'此页不使用'};
      FM1I18n.text($('sim-context'),'面板 PRESETS 旋钮：'+(presetUse[state[1]]||'以屏幕下方标签为准')+'。网页“内置预设”始终切换整套项目。');
    }
    updateScreenControls(message.screen,contextChanged);
    if(message.pixels)for(let band=0,source=0;band<15;){
      if(!(message.pixelBands&(1<<band))){band++;continue;}
      const first=band;while(band<15&&(message.pixelBands&(1<<band)))band++;
      for(let i=first*3840;i<band*3840;i++){
        const value=message.pixels[source++],at=i*4;
        pixels.data[at]=rgb5[value>>11];pixels.data[at+1]=rgb6[(value>>5)&63];pixels.data[at+2]=rgb5[value&31];
      }
      image.putImageData(pixels,0,0,0,first*16,240,(band-first)*16);
    }
    const selectionChanged=!before||before[0]!==state[0]||before[8+state[0]*20]!==state[8+state[0]*20];
    const oldWorkspace=workspace;
    if(message.workspace){
      workspace=message.workspace;
      if(!oldWorkspace||workspace.name!==oldWorkspace.name)$('sim-current-project').textContent=workspace.name||'UNTITLED';
      if(!oldWorkspace||workspace.dirty!==oldWorkspace.dirty)FM1I18n.text($('sim-project-note'),workspace.dirty?'工作区有修改':'工作区未修改');
      if(!oldWorkspace||workspace.slot!==oldWorkspace.slot||workspace.factory!==oldWorkspace.factory)$('sim-factory').value=workspace.factory?String(workspace.slot):'';
      renderSync();
    }
    const count=Math.min(4,Math.ceil(message.countIn/4));
    const recording=state[16]&&state[17]!==1?'等待外部 MIDI 时钟；在 SEQ 的 SYNC / LOOP 页可切回 INT。':state[4]?
      count?'预备拍：'+count+'；下一小节开始录音。':state[14]?'实时录音：弹琴录入，REC 结束录音。':'步进录入：每次弹琴写入编辑步，REC 结束录入。':state[14]?'正在播放；绿色是播放位置，选中框是编辑位置。':'已停止；REC 开始步进录入，PLAY 播放。';
    $('sim-record-state').hidden=!(state[4]||(state[16]&&state[17]!==1));
    if($('sim-record-state').dataset.message!==recording){$('sim-record-state').dataset.message=recording;FM1I18n.text($('sim-record-state'),recording);}
    if(!before||before[14]!==state[14]){
      FM1I18n.text($('sim-play-control'),state[14]?'暂停':'播放');
      $('sim-play-control').setAttribute('aria-pressed',String(!!state[14]));
    }
    if(selectionChanged){$('sim-track').value=state[0];$('sim-source').value=state[8+state[0]*20];}
    for(let t=0;t<8;t++){
      const at=8+t*20,{button,source,level,dots}=ui.tracks[t];
      if(!before||before[0]!==state[0])button.classList.toggle('selected',state[0]===t);
      if(!before||before[at]!==state[at])FM1I18n.text(source,sources[state[at]]);
      if(!before||before[at+3]!==state[at+3])level.style.setProperty('--level',(state[at+3]/127*100)+'%');
      for(let byte=0;byte<8;byte++){
        const mask=at+12+byte,changed=before?before[mask]^state[mask]:255;
        if(changed)for(let bit=0;bit<8;bit++)if(changed&(1<<bit))dots[byte*8+bit].classList.toggle('on',!!(state[mask]&(1<<bit)));
      }
      if(!before||before[209+t]!==state[209+t])
        for(let i=0;i<64;i++)dots[i].classList.toggle('outside',i>=state[209+t]);
      if(!before||before[at+6]!==state[at+6]||before[at+7]!==state[at+7]){
        if(before&&before[at+6]&&before[at+7]<64)dots[before[at+7]].classList.remove('play');
        if(state[at+6]&&state[at+7]<64)dots[state[at+7]].classList.add('play');
      }
      if(!before||before[at+4]!==state[at+4]||before[at+5]!==state[at+5]||before[209+t]!==state[209+t]||before[217+t]!==state[217+t])FM1I18n.attr(button,'title','轨道 '+(t+1)+' · '+state[209+t]+' 步'+(state[217+t]!==state[209+t]?' · 下轮 '+state[217+t]+' 步':'')+(state[at+4]?' · 静音':'')+(state[at+5]?' · 独奏':''));
    }
    for(const [button,at]of ui.leds)if(!before||before[at]!==state[at])button.classList.toggle('lit',!!state[at]);
    if(message.flash){try{localStorage.setItem(storageKey,encode(message.flash));$('sim-storage-warning').hidden=true;}catch(_){$('sim-storage-warning').hidden=false;}}
    if(selectionChanged||!oldWorkspace||workspace?.slot!==oldWorkspace.slot||workspace?.factory!==oldWorkspace.factory)availability();
  }
  let lcdKey='',lcdScreen,lcdEditing,lcdItems=new Map(),lcdReturnFocus;
  const lcdDialog=$('sim-lcd-dialog');
  const lcdId=item=>[item.action,item.index,item.owner].join(':');
  function lcdAdjust(item,delta){
    if(!item||!ready)return;
    send('lcd',{context:lcdScreen.context,action:item.action,index:item.index,owner:item.owner,delta});
  }
  // Trackpads report many small pixel deltas; wheel notches report large deltas.
  // Keep one fine step per notch, accumulate small movements, and leave browser
  // zoom (Ctrl/Meta + wheel) untouched.
  function lcdWheel(button,getItem){
    let amount=0,last=0,direction=0,context;
    const reset=()=>{amount=last=direction=0;};
    button.addEventListener('pointerleave',reset);
    button.addEventListener('wheel',event=>{
      const item=getItem();
      if(button.disabled||!item||!event.deltaY||event.ctrlKey||event.metaKey||Math.abs(event.deltaX)>Math.abs(event.deltaY))return;
      event.preventDefault();event.stopPropagation();
      const now=performance.now(),sign=Math.sign(event.deltaY);
      const nextContext=lcdScreen.context+':'+lcdId(item);
      if(nextContext!==context){reset();context=nextContext;}
      if(sign!==direction||now-last>250){amount=0;direction=sign;}
      last=now;amount+=Math.abs(event.deltaY);
      if(event.deltaMode||amount>=40){amount=0;lcdAdjust(item,-sign);}
    },{passive:false});
  }
  function lcdValue(){
    const item=lcdItems.get(lcdEditing);
    if(!item)return;
    if($('sim-lcd-label').textContent!==item.label)$('sim-lcd-label').textContent=item.label;
    if($('sim-lcd-value').textContent!==item.value)$('sim-lcd-value').textContent=item.value;
  }
  function editLcd(id,button){
    release();lcdEditing=id;lcdReturnFocus=button;lcdValue();lcdDialog.showModal();$('sim-lcd-plus').focus();
  }
  function closeLcd(){lcdDialog.close();if(lcdReturnFocus?.isConnected)lcdReturnFocus.focus();}
  $('sim-lcd-close').onclick=closeLcd;
  lcdDialog.addEventListener('cancel',event=>{event.preventDefault();closeLcd();});
  $('sim-lcd-minus').onclick=()=>lcdAdjust(lcdItems.get(lcdEditing),-1);
  $('sim-lcd-plus').onclick=()=>lcdAdjust(lcdItems.get(lcdEditing),1);
  lcdWheel($('sim-lcd-value'),()=>lcdDialog.open&&lcdItems.get(lcdEditing));
  lcdDialog.addEventListener('keydown',event=>{
    if(['ArrowLeft','ArrowDown','ArrowRight','ArrowUp'].includes(event.code)){
      event.preventDefault();event.stopPropagation();lcdAdjust(lcdItems.get(lcdEditing),['ArrowLeft','ArrowDown'].includes(event.code)?-1:1);
    }
  });
  function updateScreenControls(screen,contextChanged){
    if(!screen)return;
    const changed=contextChanged||lcdScreen?.context!==screen.context;
    lcdScreen=screen;
    if(changed&&lcdDialog.open)closeLcd();
    const items=screen.targets.slice();
    for(let col=0;col<4;col++)if(screen.mask&(1<<col)){
      const [label,value]=screen.knobs[col];
      items.push({rect:[6+59*col,195,54,32],action:0,index:col,owner:state[0],label,value,knob:col});
    }
    lcdItems=new Map(items.map(item=>[lcdId(item),item]));
    if(lcdDialog.open){if(lcdItems.has(lcdEditing))lcdValue();else closeLcd();}
    const key=screen.context+':'+screen.first+':'+state[209+state[0]]+':'+items.map(item=>[lcdId(item),...item.rect].join(',')).join(';');
    const host=$('sim-lcd-targets');
    if(key!==lcdKey){
      lcdKey=key;host.replaceChildren();
      const target=(x,y,w,h,label,fn)=>{
        const button=document.createElement('button');button.className='lcd-target';
        Object.assign(button.style,{left:x/2.4+'%',top:y/2.4+'%',width:w/2.4+'%',height:h/2.4+'%'});
        FM1I18n.attr(button,'aria-label',label);FM1I18n.attr(button,'title',label);
        button.onclick=()=>action(fn);host.append(button);return button;
      };
      if(state[1]===0){
        for(let t=0;t<8;t++){
          target(5,48+t*18,72,16,'轨道 '+(t+1),()=>select(t,6));
          for(let i=0;i<16;i++){
            const step=screen.first+i;
            if(step>=state[209+t])continue;
            target(80+i*8,48+t*18,8,16,'T'+(t+1)+' / STEP '+(step+1),async()=>{await select(t,6);await request('step',{index:step});}).tabIndex=-1;
          }
        }
      }else if(state[1]===1){
        const length=state[209+state[0]];
        for(let p=0;p<4;p++)if(p*16<length)target(8+p*57,50,53,14,'PAGE '+(p+1),()=>request('step',{index:p*16}));
        for(let i=0;i<16;i++){
          const step=screen.first+i;if(step>=length)continue;
          const cell=target(8+(i%4)*57,72+Math.floor(i/4)*29,53,25,'STEP '+(step+1),()=>request('step',{index:step,toggle:true}));
          cell.dataset.lcdStep=step;
          cell.addEventListener('keydown',event=>{
            const delta={ArrowLeft:-1,ArrowRight:1,ArrowUp:-4,ArrowDown:4}[event.code];if(!delta)return;
            event.preventDefault();event.stopPropagation();
            const next=Math.max(screen.first,Math.min(screen.first+15,length-1,step+delta));
            host.querySelector('[data-lcd-step="'+next+'"]')?.focus();
          });
        }
      }
      for(const item of items){
        const id=lcdId(item),button=target(...item.rect,item.label,()=>editLcd(id,button));
        button.dataset.lcdEdit=id;
        if(item.knob!==undefined)button.dataset.lcdKnob=item.knob;
        lcdWheel(button,()=>lcdItems.get(id));
        button.addEventListener('keydown',event=>{
          const delta={ArrowLeft:-1,ArrowDown:-1,ArrowRight:1,ArrowUp:1}[event.code];
          if(delta){event.preventDefault();event.stopPropagation();lcdAdjust(lcdItems.get(id),delta);}
        });
      }
    }
    for(const cell of host.querySelectorAll('[data-lcd-step]'))cell.tabIndex=Number(cell.dataset.lcdStep)===state[2]?0:-1;
    for(const button of host.querySelectorAll('[data-lcd-edit]')){
      const item=lcdItems.get(button.dataset.lcdEdit);
      button.disabled=!ready||!item;
      const description=(button.dataset.lcdKnob!==undefined?'K'+(Number(button.dataset.lcdKnob)+1)+' / ':'')+item.label+' / '+item.value;
      if(button.getAttribute('aria-label')!==description){button.setAttribute('aria-label',description);button.title=description;}
    }
  }
  function deadline(promise,milliseconds,message) {
    return new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error(message)),milliseconds);Promise.resolve(promise).then(value=>{clearTimeout(timer);resolve(value);},error=>{clearTimeout(timer);reject(error);});});
  }
  function setVolume(value) {
    value=Math.max(0,Math.min(100,value));$('sim-volume').value=value;
    const master=$('sim-master');if(master){master.style.setProperty('--angle',(-135+value*2.7)+'deg');master.setAttribute('aria-valuenow',String(value));}
    if(ready)control(3,0,Math.round(value*1023/100));
    if(gain)gain.gain.setTargetAtTime(audioOn?1:0,context.currentTime,.01);
  }
  async function boot() {
    try {
      if(!globalThis.WebAssembly||!globalThis.crypto?.subtle)throw Error('此预览环境不支持仿真。请保存 HTML 后，用电脑 Chrome 或 Edge 打开。');
      wasm=await FM1Web.assetBytes(FM1_SIMULATOR,2097152,'仿真文件校验失败，请重新下载完整工作台。');
      let flash;try{const saved=localStorage.getItem(storageKey);if(saved)flash=decode(saved);}catch(_){}
      preview=new globalThis.FM1SimulatorCore(wasm,flash);
      ready=true;receive(preview.frame(true));availability();setVolume(Number($('sim-volume').value));
      $('sim-factory').replaceChildren();
      const choose=document.createElement('option');choose.value='';FM1I18n.text(choose,'选择内置预设…');choose.disabled=true;$('sim-factory').append(choose);
      (await request('factories')).forEach((name,index)=>{
        const option=document.createElement('option');option.value=index;option.textContent=String(index+1).padStart(2,'0')+' · '+name;$('sim-factory').append(option);
      });
      $('sim-factory').value=workspace?.factory?String(workspace.slot):'';availability();
      say('已就绪，点琴键或播放即可试听。');
      const blockFrames=preview.c.sim_block_frames();
      let last=performance.now(),frames=0;
      setInterval(()=>{
        updateMeter();
        const now=performance.now();frames+=Math.min(40,now-last)*44.1;last=now;
        const blocks=Math.floor(frames/blockFrames);frames%=blockFrames;
        if(!ready||handoff)return;
        if(preview){preview.command({type:'tick',blocks});receive(preview.frame());}
        else if(context?.state!=='running')send('tick',{blocks});
      },20);
    }catch(error){say(error.message);FM1I18n.text($('sim-engine-state'),'载入失败');FM1I18n.text($('sim-placeholder'),error.message);}
  }
  function updateMeter() {
    let peak=0;
    if(analyser&&audioOn&&context?.state==='running'){
      analyser.getFloatTimeDomainData(meterData);
      for(const value of meterData)peak=Math.max(peak,Math.abs(value));
    }
    const level=Math.min(100,Math.round(peak*100));
    $('sim-output-meter').value=level;
    $('sim-output-meter').setAttribute('aria-valuetext',level+'%');
  }
  function start(requested) {
    if(!startTask)startTask=startAudio(requested).finally(()=>{startTask=undefined;});
    return startTask;
  }
  async function startAudio(requested) {
    if(starting||!ready)return;
    const target=typeof requested==='boolean'?requested:!audioOn;
    if(!target&&!node){audioOn=false;availability();return;}
    starting=true;availability();let candidate;
    try {
      if(node) {
        if(target&&context.state!=='running')await deadline(context.resume(),2500,'浏览器未开启声音，编辑仍可使用。');
        audioOn=target;setVolume(Number($('sim-volume').value));
        say(audioOn?'声音已开启，可用“试听音量”调节。':'声音已关闭，面板与编辑继续运行。');return;
      }
      if(!globalThis.AudioContext||!globalThis.AudioWorkletNode)throw Error('浏览器不支持音频线程，请使用电脑 Chrome 或 Edge；当前面板仍可编辑。');
      context=new AudioContext({sampleRate:44100,latencyHint:'interactive'});
      await deadline(context.resume(),2500,'浏览器未允许播放声音，面板仍可编辑。');
      await deadline(context.audioWorklet.addModule('data:text/javascript;charset=utf-8,'+encodeURIComponent(globalThis.FM1SimulatorAudioSource)),3500,'音频模块载入超时，面板仍可编辑。');
      handoff=true;
      candidate=new AudioWorkletNode(context,'fm1-engine',{numberOfInputs:0,numberOfOutputs:1,outputChannelCount:[2],processorOptions:{wasm,snapshot:preview.c.memory.buffer.slice(0)}});
      gain=context.createGain();gain.gain.value=0;
      analyser=context.createAnalyser();analyser.fftSize=512;meterData=new Float32Array(analyser.fftSize);
      candidate.connect(gain).connect(analyser).connect(context.destination);
      await deadline(new Promise((resolve,reject)=>{
        candidate.onprocessorerror=()=>reject(Error('音频线程启动失败，面板仍可编辑。'));
        candidate.port.onmessage=e=>{if(e.data.type==='ready')resolve();else receive(e.data);};
      }),3500,'音频线程没有响应，面板仍可编辑。');
      node=candidate;preview=null;audioOn=true;
      node.onprocessorerror=()=>{ready=false;audioOn=false;availability();say('音频线程已停止。请重新载入页面；已保存到本地槽的项目仍在。');};
      context.onstatechange=()=>{if(context.state!=='running'){audioOn=false;setVolume(Number($('sim-volume').value));say('声音暂时中断，编辑仍可使用。点琴键或重新播放以恢复声音。');}availability();};
      setVolume(Number($('sim-volume').value));say('声音已开启，面板状态和编辑已保留。');
    }catch(error){
      candidate?.disconnect();candidate?.port.close();
      if(!node&&context)await deadline(context.close().catch(()=>{}),500,'').catch(()=>{});
      if(!node){context=undefined;gain=undefined;analyser=undefined;meterData=undefined;}audioOn=false;say(error.message);
    }finally{
      handoff=false;for(const message of waiting.splice(0))dispatch(message);
      starting=false;availability();
    }
  }

  function control(kind,index,value){
    // The first key press is delivered after audio starts. A quick tap still
    // gets a short audible gate; editing keys never get replayed as notes.
    const key=wakingKeys.get(index);
    if(kind===1&&key){key.down=!!value;return;}
    const black=[1,3,5,8,10,13,15,17,20,22];
    const playable=state&&([0,2,3,4,5,10,15,17].includes(state[1])||(state[1]===1&&state[4]))&&
      !(state[1]===2&&black.includes(index))&&!(state[1]===5&&state[8+state[0]*20]===1&&[20,22].includes(index));
    if(kind===1&&value&&!muted&&!audioOn&&playable){
      const pendingKey={down:true,track:state[0],page:state[1]};wakingKeys.set(index,pendingKey);
      start(true).then(()=>{
        if(wakingKeys.get(index)!==pendingKey)return;
        wakingKeys.delete(index);
        if(!audioOn||state[0]!==pendingKey.track||state[1]!==pendingKey.page)return;
        send('control',{kind,index,value:1});
        if(!pendingKey.down){
          const owner='wake:'+index;clearTimeout(pulseTimers.get(owner));
          pulseTimers.set(owner,setTimeout(()=>{pulseTimers.delete(owner);if(!held.has('1:'+index))send('control',{kind,index,value:0});},100));
        }
      });return;
    }
    if(kind===0&&index===12){
      if(wakingPlay){
        if(value&&held.has('0:8'))wakingPlay=undefined;
        else{wakingPlay.down=!!value;return;}
      }
      // Defer the panel/keyboard transport press just like the toolbar. Failed
      // audio startup must not advance the sequence in a silent preview.
      // Stopping playback and HOME+PLAY remain immediate without audio.
      if(value&&!muted&&!audioOn&&!state?.[14]&&!held.has('0:8')){
        const play={down:true,page:state?.[1],track:state?.[0]};wakingPlay=play;
        start(true).then(()=>{
          if(wakingPlay!==play)return;wakingPlay=undefined;
          if(!audioOn||state?.[1]!==play.page||state?.[0]!==play.track)return;
          send('control',{kind,index,value:1});
          if(!play.down)send('control',{kind,index,value:0});
        });return;
      }
    }
    send('control',{kind,index,value});
  }
  const holders=new Map(),latched=new Set(),pulseTimers=new Map();let held=new Set();
  function syncHolds() {
    const next=new Set(latched);for(const item of holders.values())next.add(item);
    for(const item of held)if(!next.has(item)){const [kind,index]=item.split(':').map(Number);control(kind,index,0);}
    for(const item of next)if(!held.has(item)){const [kind,index]=item.split(':').map(Number);control(kind,index,1);}
    held=next;
    for(const element of ui.holds){
      element.classList.toggle('held',held.has(element.dataset.hold));element.classList.toggle('latched',latched.has(element.dataset.hold));
      element.setAttribute('aria-pressed',String(held.has(element.dataset.hold)));
    }
  }
  function release(){wakingPlay=undefined;wakingKeys.clear();for(const timer of pulseTimers.values())clearTimeout(timer);pulseTimers.clear();holders.clear();latched.clear();held.clear();send('release');for(const e of ui.holds){e.classList.remove('held','latched');e.setAttribute('aria-pressed','false');}}
  function feedback(kind,index,element){
    if(((kind===0&&index===11)||(kind===10&&index===15))&&state?.[1]===10&&lcdScreen?.knobs?.[0]?.[0]==='SYNC'){
      say('DELAY 页连按 SEQ 或 TAP 设置 BPM；外部 MIDI 时钟下速度只读。');return;
    }
    if(kind===0){
    if(state?.[1]===7){say('蓝牙：K1 总开关，K2 音频，K3 BLE 角色，K4 时钟；PRESETS 选屏幕下方操作，SAVE 执行，清除配对需再次确认。无线连接请在实机操作。');return;}
    const tips={2:'FX：SELECT 选轨，K1 混响、K2 延迟，0 关闭发送；PRESETS 切换细调页。',3:'SCL：DX7 算子键盘缩放；其他音源打开音高参数页。',10:'ARP：短按开关，长按调参数；ALGORITHM 翻页，第一页调分频／方向／音域／门长，第二页调 Swing／Hold。SELECT 选轨，开关和参数随项目保存，不保存按住的音符。',11:'SEQ：ALGORITHM 翻底部参数页，PRESETS 选步；RHYTHM/REC 页 K4 选循环长度。再按 SEQ 返回基础页并恢复跟随。'};if(tips[index])say(tips[index]);}}
  function latch(item){latched.has(item)?latched.delete(item):latched.add(item);syncHolds();}
  function pulse(kind,index) {
    const owner='click:'+kind+':'+index;clearTimeout(pulseTimers.get(owner));
    if(holders.delete(owner))syncHolds();
    holders.set(owner,kind+':'+index);syncHolds();
    pulseTimers.set(owner,setTimeout(()=>{pulseTimers.delete(owner);holders.delete(owner);syncHolds();},kind===1?100:55));
  }
  function hold(element,kind,index) {
    const item=kind+':'+index;element.dataset.hold=item;
    element.addEventListener('contextmenu',e=>e.preventDefault());
    element.addEventListener('pointerdown',event=>{
      if(element.disabled||event.button!==0&&!(kind===0&&event.button===2))return;event.preventDefault();feedback(kind,index,element);
      if(kind===0&&(event.shiftKey||event.button===2||latched.has(item))){latch(item);return;}
      element.setPointerCapture(event.pointerId);holders.set('pointer:'+event.pointerId,item);syncHolds();
    });
    const up=event=>{if(holders.delete('pointer:'+event.pointerId))syncHolds();};
    for(const type of ['pointerup','pointercancel','lostpointercapture'])element.addEventListener(type,up);
    element.addEventListener('keydown',event=>{if(!element.disabled&&['Enter','Space'].includes(event.code)){event.preventDefault();event.stopPropagation();if(!event.repeat){feedback(kind,index,element);if(kind===0&&(event.shiftKey||latched.has(item)))latch(item);else{holders.set('focus:'+event.code,item);syncHolds();}}}});
    element.addEventListener('blur',()=>{let changed=false;for(const code of ['Enter','Space'])if(holders.get('focus:'+code)===item){holders.delete('focus:'+code);changed=true;}if(changed)syncHolds();});
    // Accessibility actions and keyboard-generated clicks may not emit pointer events.
    element.addEventListener('click',event=>{if(event.detail===0&&!element.disabled){feedback(kind,index,element);if(kind===0&&(event.shiftKey||latched.has(item)))latch(item);else pulse(kind,index);}});
  }
  function tap(index){control(0,index,1);control(0,index,0);}
  function buildControls() {
    for(let t=0;t<8;t++){
      const button=document.createElement('button');button.id='sim-track-'+t;button.className='track-tile';button.dataset.sim='';button.disabled=true;
      button.innerHTML='<span class="track-number">'+String(t+1).padStart(2,'0')+'</span><span class="track-source"></span><span class="track-level"></span><span class="track-steps">'+'<i></i>'.repeat(64)+'</span>';
      button.addEventListener('click',()=>select(t,6).catch(e=>say(e.message)));$('sim-tracks').append(button);
      const option=document.createElement('option');option.value=t;FM1I18n.text(option,'轨道 '+(t+1));$('sim-track').append(option);
    }
    sources.forEach((name,type)=>{const option=document.createElement('option');option.value=type;option.textContent=name;$('sim-source').append(option);});
    // Control positions follow the FM-1 faceplate. HTML shapes and handlers are
    // local implementations; no remote font, image or script is needed.
    const encoders=[['MASTER',-1,105,100],['SELECT',0,215,100],['PRESETS',6,105,205],['ALGORITHM',1,215,205],['KNOB 1',2,610,100],['KNOB 2',3,714,100],['KNOB 3',4,818,100],['KNOB 4',5,922,100]];
    for(const [label,index,x,y]of encoders){
      const box=document.createElement('div');box.className='encoder';box.style.left=(x/10)+'%';box.style.top=(y/6.24)+'%';
      box.innerHTML='<span>'+label+'</span><button class="dial" data-sim disabled aria-label="'+label+'：拖动、滚轮或点击调节"><i></i></button><div class="detents"><button data-sim disabled aria-label="'+label+' 减少">−</button><button data-sim disabled aria-label="'+label+' 增加">＋</button></div>';
      const dial=box.querySelector('.dial'),buttons=box.lastElementChild.querySelectorAll('button');dial.dataset.encoder=index;
      if(index<0){dial.id='sim-master';dial.setAttribute('role','slider');dial.setAttribute('aria-valuemin','0');dial.setAttribute('aria-valuemax','100');}
      let angle=0,lastY=0,moved=false;
      const turn=delta=>{if(!ready)return;if(index<0)setVolume(Number($('sim-volume').value)+delta*2);else{angle+=delta*18;dial.style.setProperty('--angle',angle+'deg');control(2,index,delta);}};
      buttons[0].onclick=()=>turn(-1);buttons[1].onclick=()=>turn(1);
      dial.addEventListener('wheel',event=>{if(dial.disabled||!event.deltaY||event.ctrlKey||event.metaKey||Math.abs(event.deltaX)>Math.abs(event.deltaY))return;event.preventDefault();turn(event.deltaY<0?1:-1);},{passive:false});
      dial.addEventListener('pointerdown',event=>{if(dial.disabled||event.button!==0)return;event.preventDefault();lastY=event.clientY;moved=false;dial.setPointerCapture(event.pointerId);});
      dial.addEventListener('pointermove',event=>{if(!dial.hasPointerCapture(event.pointerId))return;const delta=Math.trunc((lastY-event.clientY)/6);if(delta){moved=true;lastY-=delta*6;turn(Math.max(-20,Math.min(20,delta)));}});
      dial.addEventListener('pointerup',event=>{if(dial.hasPointerCapture(event.pointerId)&&!moved)turn(event.shiftKey?-1:1);});
      dial.addEventListener('click',event=>{if(event.detail===0)turn(event.shiftKey?-1:1);});
      dial.addEventListener('keydown',event=>{if(['ArrowUp','ArrowRight','ArrowDown','ArrowLeft'].includes(event.code)){event.preventDefault();event.stopPropagation();turn(event.code==='ArrowUp'||event.code==='ArrowRight'?1:-1);}});
      FM1I18n.scan(box);$('sim-device').append(box);
    }
    const buttons=[['OCT −',0,60,258,90,34],['OCT ＋',1,170,258,90,34]];
    ['FX','SCL','ENV','LFO','EDIT','GLO'].forEach((n,i)=>buttons.push([n,2+i,582+i*63,174,54,48]));
    ['HOME','SAVE','ARP','SEQ','PLAY','REC'].forEach((n,i)=>buttons.push([n,8+i,582+i*63,236,54,48]));
    for(const [label,id,x,y,w,h]of buttons){
      const button=document.createElement('button');if(id!==12)FM1I18n.text(button,label);button.className='panel-button';button.dataset.sim='';button.dataset.led=id;button.dataset.button=id;button.disabled=true;
      Object.assign(button.style,{left:x/10+'%',top:y/6.24+'%',width:w/10+'%',height:h/6.24+'%'});
      FM1I18n.attr(button,'title',label+'（Shift 点击或右击可锁定按住）');
      if(id===12)button.innerHTML='PLAY<small>STOP</small>';hold(button,0,id);$('sim-device').append(button);
    }
    const whites=[0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26],edits=[1,3,5,8,10,13,15,17,20,22],names=['OP1','OP2','OP3','OP4','OP5','OP6','PITCH','GLOBAL','MONO','POLY'];
    for(let key=0;key<27;key++){
      const white=whites.indexOf(key),button=document.createElement('button');button.className='piano-key '+(white>=0?'white':'black');button.dataset.sim='';button.dataset.led=key+14;button.dataset.key=key;button.disabled=true;
      const center=white>=0?white+.5:whites.filter(k=>k<key).length;
      button.style.left=(center*100/16)+'%';
      button.innerHTML='<i></i><span>'+(white>=0?String(white+1).padStart(2,'0'):names[edits.indexOf(key)]||'')+'</span>';
      FM1I18n.attr(button,'aria-label',white>=0?'音符／步 '+(white+1):names[edits.indexOf(key)]||'升音键');hold(button,1,key);$('sim-keyboard').append(button);
    }
  }

  async function action(fn){try{await fn();}catch(error){say(error.message);}}
  $('sim-start').onclick=()=>{muted=!muted;return start(!muted);};$('sim-panic').onclick=()=>{release();send('panic');};
  $('sim-tap-tempo').onclick=()=>send('tapTempo');
  $('sim-play-control').onclick=()=>action(async()=>{
    if(starting)return;
    release();
    if(!state?.[14]&&!muted&&!audioOn){
      const play={page:state?.[1],track:state?.[0]};wakingPlay=play;
      await start(true);
      if(wakingPlay!==play)return;wakingPlay=undefined;
      if(!audioOn||state?.[1]!==play.page||state?.[0]!==play.track)return;
    }
    // PLAY on the preset browser loads its selection. The toolbar controls
    // transport only; other editors keep their page and cursor while playing.
    if(state?.[1]===16)send('open',{page:0});
    tap(12);
  });
  $('sim-save-control').onclick=()=>{release();send('open',{page:0});tap(9);};
  $('sim-save-device').onclick=()=>action(async()=>{release();await globalThis.FM1Workbench.save(await request('export'));});
  for(const button of document.querySelectorAll('[data-sim-page]'))button.onclick=()=>action(async()=>{
    release();await request('open',{page:Number(button.dataset.simPage)});
    $('sim-device').scrollIntoView({behavior:'smooth',block:'center'});
    say('已打开'+FM1I18n.sourceText(button)+'；用面板旋钮调整，HOME 返回。');
  });
  $('sim-volume').oninput=()=>setVolume(Number($('sim-volume').value));
  $('sim-track').onchange=()=>action(()=>select(Number($('sim-track').value),6));
  $('sim-source').onchange=()=>action(async()=>{
    const track=Number($('sim-track').value),source=Number($('sim-source').value);
    if(confirm(FM1I18n.t('切换音源将重置此轨的音色；鼓与旋律之间切换还会清空此轨步进。继续吗？')))await select(track,source);
    else $('sim-source').value=String(state[8+track*20]);
  });
  $('sim-project-export').onclick=()=>action(async()=>{save(FM1Web.projectFile(await request('export')),'FM1-simulation.fm1');say('已导出仿真工作区项目。');});
  async function loadPreset(index){
    if(presetBusy||!Number.isInteger(index)||index<0||index>=16)return;
    presetBusy=true;availability();
    try{
      await request('preset',{index});
      $('sim-factory').value=String(index);
      say('已载入 '+$('sim-factory').selectedOptions[0].textContent+'。');
    }finally{presetBusy=false;availability();}
  }
  $('sim-factory-load').onclick=()=>action(()=>loadPreset(Number($('sim-factory').value)));
  $('sim-factory').onchange=()=>action(()=>loadPreset(Number($('sim-factory').value)));
  $('sim-preset-prev').onclick=()=>action(()=>loadPreset(workspace?.factory?Math.max(0,workspace.slot-1):0));
  $('sim-preset-next').onclick=()=>action(()=>loadPreset(workspace?.factory?Math.min(15,workspace.slot+1):0));
  $('sim-factory-export').onclick=()=>action(async()=>{
    const index=Number($('sim-factory').value),data=await request('factory',{index});
    save(FM1Web.projectFile(data),'FM1-'+String(index+1).padStart(2,'0')+'-'+String.fromCharCode(...data.slice(3836,3848)).trim()+'.fm1');
    say('官方工程已导出，当前仿真工作区保持不变。');
  });
  $('sim-timbre-export').onclick=()=>action(async()=>{save(FM1Web.timbreFile(await request('export',{timbre:true})),'FM1-timbre.fm1t');say('已导出当前轨道的音源类型与全部音色参数。');});
  $('sim-patch-export').onclick=()=>action(async()=>{save(FM1Web.voiceFile(await request('export',{patch:true})),'FM1-voice.syx');say('已导出当前轨道的 DX7 音色。');});
  for(let i=0;i<128;i++){const o=document.createElement('option');o.value=i;o.textContent=(i+1)+' · '+String.fromCharCode(65+(i>>5));$('sim-library-slot').append(o);}
  $('sim-library-save').onclick=()=>action(async()=>{
    const program=Number($('sim-library-slot').value),patch=selectedFile?.patch?voices[Number($('sim-voice').value)]:await request('export',{patch:true});
    const raw=await request('library-read',{bank:program>>5});raw.set(FM1Web.packVoice(patch),(program&31)*128);
    await request('library-write',{bank:program>>5,data:raw});say('已保存到仿真独立音色库的音色 '+(program+1)+'。');
  });
  $('sim-library-bank').onclick=()=>action(async()=>{
    const bank=Number($('sim-library-slot').value)>>5,raw=selectedFile?.bank||FM1Web.voiceBank(voices);
    if(!confirm(FM1I18n.t('用文件替换所选位置所在组的全部 32 个仿真音色？')))return;
    await request('library-write',{bank,data:raw});say('整组音色已写入仿真库。');
  });
  $('sim-library-export').onclick=()=>action(async()=>{
    const bank=Number($('sim-library-slot').value)>>5;save(FM1Web.bankFile(await request('library-read',{bank})),'FM1-bank-'+String.fromCharCode(65+bank)+'.syx');
  });
  $('sim-file').onchange=async()=>{
    const revision=++fileRevision;selectedFile=undefined;voices=[];$('sim-voice').replaceChildren();$('sim-voice').hidden=true;availability();const file=$('sim-file').files[0];if(!file)return;
    try {
    if(file.size>4280)throw Error('请选择 .fm1 项目、.fm1t 音色或 DX7 .syx 文件');const bytes=new Uint8Array(await file.arrayBuffer());
    if(revision!==fileRevision)return;
    if(bytes[0]===240){voices=FM1Web.readVoices(bytes);voices.forEach((voice,i)=>{const option=document.createElement('option');option.value=i;option.textContent=(i+1)+' · '+String.fromCharCode(...voice.slice(145,155)).replace(/[^\x20-\x7e]/g,'?');$('sim-voice').append(option);});selectedFile={patch:true,bank:bytes.length===4104?bytes.slice(6,4102):undefined};}
    else if(bytes[3]===84) selectedFile={timbre:true,data:FM1Web.readTimbreFile(bytes)};
    else selectedFile={data:FM1Web.readProjectFile(bytes)};
    $('sim-voice').hidden=!selectedFile.patch;availability();say(selectedFile.patch&&state[8+state[0]*20]!==0?'音色校验通过，请先选择 DX7 轨道再导入。':'文件校验通过，点击“导入到仿真”应用。');
    }catch(error){if(revision===fileRevision)say(error.message);}
  };
  $('sim-file-apply').onclick=()=>action(async()=>{
    if(!selectedFile)return;if(!confirm(FM1I18n.t('将替换仿真工作区'+(selectedFile.patch?'当前 DX7 音色':selectedFile.timbre?'当前轨道音色':'项目')+'，继续吗？')))return;
    const data=selectedFile.patch?voices[Number($('sim-voice').value)]:selectedFile.data;
    await request('import',{patch:!!selectedFile.patch,timbre:!!selectedFile.timbre,data});say('已导入仿真工作区；按面板 SAVE 保存到本地仿真槽。');
  });
  $('sim-read-device').onclick=()=>action(async()=>{
    if(!ready)throw Error('请先启动仿真');
    const data=await globalThis.FM1Workbench.read();
    await request('import',{data});rememberDeviceSnapshot(data,'read');say('设备项目已载入仿真工作区。');
  });
  $('sim-clear').onclick=()=>action(async()=>{if(confirm(FM1I18n.t('清空此浏览器的 16 个仿真项目槽并恢复默认音源？采样库和试听音量保留，设备上的内容不受影响。'))){await request('clear');say('本地项目存档已清空，采样库和试听音量已保留。');}});
  $('sim-midi').onclick=()=>action(async()=>{
    if(midiConnecting)return;
    if(!ready)throw Error('请先启动仿真');if(!navigator.requestMIDIAccess)throw Error('此浏览器不支持 MIDI 输入，请使用电脑 Chrome 或 Edge');
    midiConnecting=true;availability();
    try{
    const audioReady=muted?Promise.resolve():start(true);
    const access=await deadline(navigator.requestMIDIAccess({sysex:false}),30000,
      'MIDI 访问尚未就绪。请检查网站 MIDI 权限；若已允许且没有弹窗，请重启浏览器后重试。');await audioReady;
    if(midiAccess)midiAccess.onstatechange=null;midiAccess=access;
    const refresh=()=>{
      if(midiInput&&(midiInput.state!=='connected'||access.inputs.get(midiInput.id)!==midiInput)){midiInput.onmidimessage=null;midiInput=undefined;release();send('panic');say('MIDI 输入已断开，仿真音符已释放。');}
      $('sim-midi-port').replaceChildren();const empty=document.createElement('option');empty.value='';FM1I18n.text(empty,'选择 MIDI 输入');$('sim-midi-port').append(empty);
      for(const port of access.inputs.values()){if(port.state!=='connected')continue;const option=document.createElement('option');option.value=port.id;option.textContent=port.name;$('sim-midi-port').append(option);}
      $('sim-midi-port').value=midiInput?.id||'';
    };
    access.onstatechange=refresh;refresh();$('sim-midi-port').hidden=false;
    $('sim-midi-port').onchange=()=>{
      if(midiInput)midiInput.onmidimessage=null;
      midiInput=access.inputs.get($('sim-midi-port').value);release();send('panic');
      if(midiInput)midiInput.onmidimessage=event=>{
        if(!ready)return;
        const data=new Uint8Array(event.data);
        // Keep note-offs, controllers and clock ordering while audio resumes.
        send('midi',{data});
        const playing=(data[0]&0xf0)===0x90&&data[2]>0||data[0]===0xfa||data[0]===0xfb;
        // The statechange notification can lag behind the next MIDI event.
        if(playing&&!muted&&(!audioOn||context?.state!=='running'))start(true);
      };
    };
    say($('sim-midi-port').options.length>1?'选择一个输入端口；MIDI 通道沿用设备的轨道映射。':'未发现 MIDI 输入设备。');
    }finally{midiConnecting=false;availability();}
  });
  const keyCodes=['KeyZ','KeyS','KeyX','KeyD','KeyC','KeyF','KeyV','KeyB','KeyH','KeyN','KeyJ','KeyM','Comma','KeyL','Period','Semicolon','Slash','Quote','KeyQ','Digit2','KeyW','Digit3','KeyE','KeyR','Digit5','KeyT','Digit6'];
  window.addEventListener('keydown',event=>{
    if(!ready||$('tab-sim').hidden||document.hidden||document.querySelector('dialog[open]'))return;
    if(event.code==='Escape'){release();send('panic');return;}
    if(event.ctrlKey||event.metaKey||event.altKey||/INPUT|SELECT|TEXTAREA|SUMMARY|A/.test(event.target.tagName))return;
    if(event.code==='Space'){if(event.target.tagName==='BUTTON')return;event.preventDefault();if(!event.repeat)tap(12);return;}
    const index=keyCodes.indexOf(event.code);if(index<0)return;event.preventDefault();if(!event.repeat){holders.set('keyboard:'+event.code,'1:'+index);syncHolds();}
  });
  window.addEventListener('keyup',event=>{const keyboard=holders.delete('keyboard:'+event.code),focus=holders.delete('focus:'+event.code);if(keyboard||focus){event.preventDefault();syncHolds();}});
  window.addEventListener('blur',release);document.addEventListener('visibilitychange',()=>{if(document.hidden)release();});
  document.addEventListener('fm1-view-change',()=>{if($('tab-sim').hidden)release();});
  buildControls();
  const ui={
    controls:document.querySelectorAll('[data-sim]'),holds:document.querySelectorAll('[data-hold]'),
    leds:Array.from(document.querySelectorAll('[data-led]'),button=>[button,168+Number(button.dataset.led)]),
    tracks:Array.from({length:8},(_,t)=>{const button=$('sim-track-'+t);return{button,source:button.querySelector('.track-source'),level:button.querySelector('.track-level'),dots:button.querySelectorAll('i')};})
  };
  availability();boot();
})();
