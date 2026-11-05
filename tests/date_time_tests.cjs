require('../web/desktop/locales.js');require('../web/desktop/i18n.js');PulseI18n.use('zh-CN');
const assert=require('node:assert/strict');require('../web/desktop/date-time.js');let n=0;const check=(x,m)=>{n++;assert.ok(x,m);};
for(const y of [1970,1999,2000,2024,2025,2100,2400])for(let m=1;m<=12;m++){const p={year:y,month:m,day:PulseDate.days(y,m),hour:23,minute:59};check(JSON.stringify(PulseDate.parts(PulseDate.parse(p)))===JSON.stringify(p),'local UTC minute roundtrip');}
check(PulseDate.days(2000,2)===29&&PulseDate.days(2100,2)===28,'Gregorian leap rules');
for(const p of [{year:2025,month:2,day:29,hour:0,minute:0},{year:2026,month:1,day:1,hour:24,minute:0},{year:2026,month:1,day:1,hour:0,minute:60}]){assert.throws(()=>PulseDate.parse(p));n++;}
check(PulseDate.shift({year:2024,month:1,day:31},1).day===29,'month clamping');check(PulseDate.shift({year:2025,month:12,day:31},1).year===2026,'cross year');
process.env.TZ='America/New_York';assert.throws(()=>PulseDate.parse({year:2026,month:3,day:8,hour:2,minute:30}));n++;
console.log(n+' calendar/time checks passed');
