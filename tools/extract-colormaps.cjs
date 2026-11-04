// Exact, complete 256-entry tables from the pinned official Matplotlib release.
const fs = require('node:fs'), crypto = require('node:crypto');
const source = fs.readFileSync('build/ui-sources/_cm_listed.py','utf8');
const hash = crypto.createHash('sha256').update(source).digest('hex');
if (hash !== 'ddad3698f5129ceb1792a445371286c08bc9080298e657b3054aea19c9659ef9') throw Error('Source hash mismatch');
const maps = {};
for (const name of ['viridis','inferno','magma','cividis']) {
    const match = source.match(new RegExp('_'+name+'_data\\s*=\\s*(\\[\\[[\\s\\S]*?\\]\\])'));
    if (!match) throw Error('Missing '+name);
    maps[name] = JSON.parse(match[1]);
    if(maps[name].length !== 256 || maps[name].some(r=>r.length!==3 || r.some(c=>c<0||c>1))) throw Error('Invalid table');
}
fs.writeFileSync('web/desktop/colormaps.js',`// Matplotlib v3.10.0 _cm_listed.py, SHA256 ${hash}\n// Official complete LUTs. See assets/Matplotlib-LICENSE.txt and docs/ui-design.md.\n'use strict';\nglobalThis.PulseColorMaps = ${JSON.stringify(maps)};\n`);
fs.copyFileSync('build/ui-sources/Matplotlib-LICENSE.txt','web/desktop/assets/Matplotlib-LICENSE.txt');
console.log('Extracted four complete 256-entry official colour tables.');
