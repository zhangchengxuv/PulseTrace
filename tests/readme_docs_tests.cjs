'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),read=f=>fs.readFileSync(path.join(root,f),'utf8');
const zh=read('README.md'),en=read('README_EN.md');let checks=0;
function check(value,label){assert.ok(value,label);checks++;}
for(const [file,text]of [['README.md',zh],['README_EN.md',en]]){
 check((text.match(/^# /gm)||[]).length===1&&(text.match(/^## /gm)||[]).length===6,'seven-section README '+file);
 check(text.split('\n').length<85,'concise README '+file);
 for(const [,target]of text.matchAll(/!?\[[^\]]*\]\(([^)]+)\)/g))if(!/^https:\/\//.test(target))check(fs.existsSync(path.join(root,target)),'relative link '+target);
 check(text.includes('Private Bytes')&&text.includes('Working Set')&&text.includes('12'),'explicit memory/CPU units');
 check(text.includes('OpenAI Codex')&&text.includes('GPT-6'),'tool and confirmed model kept distinct');
}
assert.deepEqual(zh.match(/\d+(?:\.\d+)?(?:%| MiB)/g),en.match(/\d+(?:\.\d+)?(?:%| MiB)/g),'bilingual performance figures agree');checks++;
const images=['dashboard','network','keyboard','mouse','usage','taskbar'];
for(const image of images){const name='docs/screenshots/'+image+'.png',bytes=fs.readFileSync(path.join(root,name));check(bytes.subarray(1,4).toString()==='PNG','valid PNG '+name);if(image!=='taskbar')check(bytes.readUInt32BE(16)===2000&&bytes.readUInt32BE(20)===1125,'uniform native page dimensions');
 for(let offset=8;offset+12<=bytes.length;){const length=bytes.readUInt32BE(offset),chunk=bytes.toString('ascii',offset+4,offset+8);check(!['tEXt','iTXt','zTXt','eXIf'].includes(chunk),'no metadata in screenshot');offset+=12+length;}
}
check(read('LICENSE').includes('MIT License')&&read('LICENSE').includes('PulseTrace contributors'),'MIT ownership');
check(read('tools/package.ps1').includes("Join-Path $root 'LICENSE'"),'portable license inclusion');
check(read('installer/PulseTrace.iss').includes('Source: "{#StageDir}\\LICENSE"'),'installer license inclusion');
for(const name of ['MinGW-w64-runtime-LICENSE.txt','MinGW-w64-LICENSE.txt','winpthreads-LICENSE.txt']){check(read('licenses/'+name).length>1000,'complete runtime notice');check(read('tools/package.ps1').includes(name),'runtime notice packaging');}
check(read('docs/performance.md').includes('0dd4b7157f8cec35f303b118c72584d38f81ed4788f56c7c63cd285d3c89f73d'),'measured binary provenance');
console.log(checks+' README links, parity, screenshot metadata and license checks passed');
