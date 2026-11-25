require('../web/desktop/locales.js');require('../web/desktop/i18n.js');PulseI18n.use('zh-CN');
const assert=require('node:assert/strict');require('../web/desktop/date-time.js');let n=0;const check=(x,m)=>{n++;assert.ok(x,m);};
for(const y of [1970,1999,2000,2024,2025,2100,2400])for(let m=1;m<=12;m++){const p={year:y,month:m,day:PulseDate.days(y,m),hour:23,minute:59};check(JSON.stringify(PulseDate.parts(PulseDate.parse(p)))===JSON.stringify(p),'local UTC minute roundtrip');}
check(PulseDate.days(2000,2)===29&&PulseDate.days(2100,2)===28,'Gregorian leap rules');
for(const p of [{year:2025,month:2,day:29,hour:0,minute:0},{year:2026,month:1,day:1,hour:24,minute:0},{year:2026,month:1,day:1,hour:0,minute:60}]){assert.throws(()=>PulseDate.parse(p));n++;}
check(PulseDate.shift({year:2024,month:1,day:31},1).day===29,'month clamping');check(PulseDate.shift({year:2025,month:12,day:31},1).year===2026,'cross year');
process.env.TZ='America/New_York';assert.throws(()=>PulseDate.parse({year:2026,month:3,day:8,hour:2,minute:30}));n++;
const minute=text=>Date.parse(text)/60000;
const spring=minute('2026-03-08T05:00:00Z'),next=minute('2026-03-09T04:00:00Z');
check(PulseDate.calendarStart(next-1,'today')===spring&&next-spring===23*60,'local spring-forward day');
check(PulseDate.hourStart(minute('2026-11-01T05:30:00Z'))===minute('2026-11-01T05:00:00Z')&&
      PulseDate.hourStart(minute('2026-11-01T06:30:00Z'))===minute('2026-11-01T06:00:00Z'),'repeated local hours stay separate');
const end=minute('2026-11-01T03:30:00Z')+1;
check(PulseDate.presetStart('month',end,'local')===minute('2026-10-01T04:00:00Z')&&
      PulseDate.presetStart('month',end,'UTC')===minute('2026-11-01T00:00:00Z'),'month shortcut uses selected timezone');
check(PulseDate.presetStart('10080',end,'local')===end-10080&&PulseDate.presetStart('all',end,'local')===0,'rolling seven days and all-data bounds');
process.env.TZ='Asia/Kathmandu';
check(PulseDate.hourStart(minute('2026-01-01T12:30:00Z'))===minute('2026-01-01T12:15:00Z'),'quarter-hour offset');
process.env.TZ='Australia/Lord_Howe';
check(PulseDate.hourStart(minute('2026-10-03T15:45:00Z'))===minute('2026-10-03T15:30:00Z'),'half-hour daylight transition');
console.log(n+' calendar/time checks passed');
