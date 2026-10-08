'use strict';
const fs=require('node:fs'),zlib=require('node:zlib');
const [src,dst]=process.argv.slice(2),b=fs.readFileSync(src);
const match=b.subarray(0,100).toString('ascii').match(/^P6\n(\d+) (\d+)\n255\n/);
if(!match)throw Error('Unsupported PPM header');
const w=Number(match[1]),h=Number(match[2]),offset=match[0].length;
if(!w||!h||w*h>16000000||b.length!==offset+w*h*3)throw Error('Invalid pixel data');
function crc(buf){let c=0xffffffff;for(const v of buf){c^=v;for(let n=0;n<8;n++)c=(c>>>1)^((c&1)?0xedb88320:0)}return(c^0xffffffff)>>>0}
function chunk(type,data){const t=Buffer.from(type),len=Buffer.alloc(4),sum=Buffer.alloc(4);len.writeUInt32BE(data.length);sum.writeUInt32BE(crc(Buffer.concat([t,data])));return Buffer.concat([len,t,data,sum])}
const ihdr=Buffer.alloc(13);ihdr.writeUInt32BE(w);ihdr.writeUInt32BE(h,4);ihdr[8]=8;ihdr[9]=2;
const scan=Buffer.alloc(h*(w*3+1));for(let y=0;y<h;y++)b.copy(scan,y*(w*3+1)+1,offset+y*w*3,offset+(y+1)*w*3);
fs.writeFileSync(dst,Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',ihdr),chunk('IDAT',zlib.deflateSync(scan)),chunk('IEND',Buffer.alloc(0))]),{flag:'wx'});
console.log(JSON.stringify({width:w,height:h,pixelTransform:false}));
