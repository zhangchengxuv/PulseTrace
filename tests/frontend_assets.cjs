'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.join(__dirname,'../web/desktop'),html=fs.readFileSync(path.join(root,'index.html'),'utf8');
const scripts=[...html.matchAll(/<script\b([^>]*)>([\s\S]*?)<\/script>/g)];
assert.equal(scripts.length,(html.match(/<script\b/g)||[]).length,'Unclosed/nested script tag');
for(const [,attrs,body]of scripts){const source=attrs.match(/\bsrc="([^"]+)"/);assert(source,'Only external local scripts are permitted');assert.equal(body.trim(),'','External script contains nested markup');assert(!source[1].includes('..')&&!source[1].includes(':'),'Unsafe asset path');new vm.Script(fs.readFileSync(path.join(root,source[1]),'utf8'),{filename:source[1]});}
for(const [,file]of html.matchAll(/<link\b[^>]*href="([^"]+)"/g))assert(fs.existsSync(path.join(root,file)),'Missing local CSS/image: '+file);
console.log(`${scripts.length} local scripts and linked styles/images validated`);
