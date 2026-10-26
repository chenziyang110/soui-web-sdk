(function () {
  'use strict';
  // Chrome49-compatible frozen workload. Real feedback background and marker
  // share one canvas update; the observer independently reads its content pixel.
  var seed=window.souiBurstConfig;
  var query=seed?'?run='+seed.run+'&kind='+seed.kind:(location.search||location.hash.replace(/^#/,'?'));
  var runMatch = /(?:^|[?&])run=([a-f0-9]{32})(?:&|$)/.exec(query);
  var kindMatch = /(?:^|[?&])kind=(pointer_move|wheel)(?:&|$)/.exec(query);
  if (!runMatch || !kindMatch || window.devicePixelRatio !== 1) throw new Error('Frozen run/kind and DPR1 required');
  var nonce = parseInt(runMatch[1].slice(24),16) || 1;
  var kind = kindMatch[1], canvas = document.getElementById('feedback'), context = canvas.getContext('2d');
  var sequence = 0, frame = 0, pending = false, seenCount = 0, rejected = 0, invalid = false, wheelUnits=0;
  var seen = new Uint32Array(100000);
  function render() {
    pending = false;
    if (frame >= 65535) throw new Error('Marker frame capacity exhausted');
    ++frame;
    context.fillStyle = 'rgb('+(sequence&255)+','+((sequence>>>8)&255)+','+((sequence>>>16)&255)+')';
    context.fillRect(0,0,1100,720);
    context.fillStyle = '#ffffff'; context.font = '24px sans-serif';
    context.fillText('Input '+sequence+' / observed '+seenCount,100,500);
    var bytes = new Uint8Array(16), i, bit, crc = 65535;
    bytes[0]=167; bytes[1]=83;
    for (i=0;i<4;++i) bytes[2+i]=((invalid?0:nonce)>>>(i*8))&255;
    for (i=0;i<3;++i) bytes[6+i]=(sequence>>>(i*8))&255;
    var delivered=kind==='wheel'?wheelUnits/120:seenCount;
    bytes[9]=frame&255; bytes[10]=(frame>>>8)&255; bytes[11]=1|((delivered>>>16)<<4);
    bytes[14]=delivered&255;bytes[15]=(delivered>>>8)&255;
    for (i=0;i<16;++i) { if(i===12||i===13)continue;crc^=bytes[i]<<8; for(bit=0;bit<8;++bit) crc=((crc<<1)^((crc&32768)?4129:0))&65535; }
    bytes[12]=crc&255; bytes[13]=(crc>>>8)&255;
    for (i=0;i<128;++i) {
      context.fillStyle=(bytes[i>>>3]&(1<<(i&7)))?'#ffffff':'#000000';
      context.fillRect(8+(i%16)*4,8+Math.floor(i/16)*4,4,4);
    }
  }
  function receive(event) {
    var x=event.clientX-100,y=event.clientY-100;
    var next;
    if(kind==='wheel') {
      event.preventDefault();
      if(typeof event.wheelDelta!=='number' || event.wheelDelta>=0 || (-event.wheelDelta)%120!==0)invalid=true;
      else wheelUnits-=event.wheelDelta;
      next=wheelUnits/120;
    } else {
      if (x<0 || x>=1000 || y<0 || y>=100 || x!==Math.floor(x) || y!==Math.floor(y)) return;
      next=y*1000+x+1;
    }
    if (invalid || next<=sequence || next>100000 || seenCount>=seen.length) {
      ++rejected; invalid=true;
      if(!pending){pending=true;requestAnimationFrame(render);}
      return;
    }
    sequence=next; seen[seenCount++]=next;
    if (kind==='wheel') event.preventDefault();
    if (!pending) { pending=true; requestAnimationFrame(render); }
  }
  canvas.addEventListener(kind==='wheel'?'wheel':'mousemove',receive,false);
  render();
  window.souiBurstResult=function () {
    return {schema:'soui.burst-page/v1',run_id:runMatch[1],kind:kind,nonce:nonce,
      latest:sequence,observed:seenCount,rejected:rejected,device_pixel_ratio:1,
      marker_region:[8,8,64,36],content_probe:[10,42]};
  };
}());
