'use strict';
// A predeclared half-open codebook, never rounded/clamped to the nearest slot.
const geometry=Object.freeze({count:64,x0:32,y0:120,width:16,height:16,stride:16,centerX0:40,centerY:128,interval:'half-open'});
function sequence(x,y){return Number.isFinite(x)&&Number.isFinite(y)&&x>=32&&x<1056&&y>=120&&y<136?Math.floor((x-32)/16)+1:null;}
function bounds(seq){if(!Number.isInteger(seq)||seq<1||seq>64)throw Error('Cell sequence outside64');return[32+16*(seq-1),120,48+16*(seq-1),136];}
function center(seq){bounds(seq);return[40+16*(seq-1),128];}
function matchesGeometry(value){return value!==null&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).length===Object.keys(geometry).length&&Object.keys(geometry).every(k=>Object.prototype.hasOwnProperty.call(value,k)&&value[k]===geometry[k]);}
module.exports={geometry,sequence,bounds,center,matchesGeometry};
