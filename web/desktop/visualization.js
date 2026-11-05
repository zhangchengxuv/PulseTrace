'use strict';
// Pure colour/aggregation helpers are also exercised by tests/frontend_tests.cjs.
globalThis.PulseViz = (() => {
    const maps = globalThis.PulseColorMaps;
    const clamp = n => Math.max(0, Math.min(1, n));
    const arcticStops=['#17364c','#245b81','#3498db','#72c7db','#e1bd77','#f4d48d'];
    const linear = v => v <= .04045 ? v/12.92 : ((v+.055)/1.055)**2.4;
    const gamma = v => v <= .0031308 ? 12.92*v : 1.055*v**(1/2.4)-.055;
    function toLab(rgb) {
        const [r,g,b]=rgb.map(v=>linear(v/255));
        const l=Math.cbrt(.4122214708*r+.5363325363*g+.0514459929*b),m=Math.cbrt(.2119034982*r+.6806995451*g+.1073969566*b),s=Math.cbrt(.0883024619*r+.2817188376*g+.6299787005*b);
        return [.2104542553*l+.793617785*m-.0040720468*s,1.9779984951*l-2.428592205*m+.4505937099*s,.0259040371*l+.7827717662*m-.808675766*s];
    }
    function fromLab([L,a,b]) {
        const l=(L+.3963377774*a+.2158037573*b)**3,m=(L-.1055613458*a-.0638541728*b)**3,s=(L-.0894841775*a-1.291485548*b)**3;
        return [4.0767416621*l-3.3077115913*m+.2309699292*s,-1.2684380046*l+2.6097574011*m-.3413193965*s,-.0041960863*l-.7034186147*m+1.707614701*s].map(v=>clamp(gamma(v))*255);
    }
    const labs=arcticStops.map(hex=>toLab(hex.slice(1).match(/../g).map(v=>parseInt(v,16))));
    function arctic(t) {
        if(!Number.isFinite(t))throw new Error('Invalid Arctic coordinate');
        const scaled=clamp(t)*5,index=Math.min(4,Math.floor(scaled)),f=scaled-index;
        return fromLab(labs[index].map((v,i)=>v+(labs[index+1][i]-v)*f));
    }
    function normalized(value,max,mode='sqrt') {
        if(!Number.isFinite(value)||!Number.isFinite(max)||value<0||max<0)throw new Error('Invalid heatmap value');
        const t=max>0?clamp(value/max):0;
        if(mode!=='sqrt'&&mode!=='linear')throw new Error('Unknown normalization');
        return mode==='sqrt'?Math.sqrt(t):t;
    }
    function luminance(rgb) {
        const channels = rgb.map(v => v / 255).map(v => v <= .04045 ? v / 12.92 : ((v + .055) / 1.055) ** 2.4);
        return .2126 * channels[0] + .7152 * channels[1] + .0722 * channels[2];
    }
    function contrast(a, b) {
        const x = luminance(a), y = luminance(b);
        return (Math.max(x, y) + .05) / (Math.min(x, y) + .05);
    }
    function color(name, value, max) {
        if(name==='arctic')return arctic(normalized(value,max)).map(Math.round);
        if (!maps[name]) throw new Error('Unknown colour map');
        if (!Number.isFinite(value) || value < 0 || !Number.isFinite(max) || max < 0)
            throw new Error('Invalid heatmap value');
        // Matplotlib ListedColormap convention: floor(t*N), endpoint clamped.
        const index = Math.min(255, Math.floor(clamp(max > 0 ? value / max : 0) * 256));
        return maps[name][index].map(c => Math.round(c * 255));
    }
    const css = rgb => `rgb(${rgb.join(' ')})`;
    const foreground = rgb => contrast(rgb, [255,255,255]) >= contrast(rgb, [0,0,0]) ? '#ffffff' : '#000000';
    function binWidth(points, limit = 240, bucket = 1) {
        if (!Number.isInteger(limit) || limit < 1) throw new Error('Invalid point limit');
        if (!Number.isInteger(bucket) || bucket < 1) throw new Error('Invalid bucket width');
        if (!points.length) return bucket;
        return Math.max(1, Math.ceil(((points.at(-1)[0] - points[0][0]) / bucket + 1) / limit)) * bucket;
    }
    function aggregate(points, limit = 240, bucket = 1) {
        const width = binWidth(points, limit, bucket), bins = new Map(), first = points[0]?.[0] || 0;
        for (const [minute, value] of points) {
            const start = first + Math.floor((minute - first) / width) * width;
            bins.set(start, (bins.get(start) || 0) + value);
        }
        return [...bins];
    }
    function heatState(hasCoverage, value) {
        return !hasCoverage ? 'missing' : value === 0 ? 'zero' : 'positive';
    }
    return {color, css, foreground, contrast, aggregate, binWidth, heatState, arctic, normalized, toLab, fromLab, arcticStops};
})();

// One shared default for all statistic pages; data refresh never changes a draft.
globalThis.PulseRange={bucket(span){return span<=240?1:span<=14400?60:1440;}};
