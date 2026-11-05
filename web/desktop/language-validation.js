'use strict';
async function validateLanguageState(){
 const before=await rpc('get_status'),original=before.config.language;
 try{for(const language of ['en-US','zh-CN']){await rpc('set_config',{language});const after=await rpc('get_status');if(after.effective_language!==language||after.session!==before.session||after.monitoring!==before.monitoring||after.input_ready!==before.input_ready)throw new Error('Language switching restarted or changed monitoring');}}
 finally{await rpc('set_config',{language:original});}
}
async function validateCalendarLanguages(){
 if(page==='settings'||!rangeFields)return;const original=PulseI18n.locale;
 try{for(const language of ['zh-CN','en-US']){PulseI18n.use(language);rangeFields.from.click();await new Promise(r=>requestAnimationFrame(r));const picker=PulseDatePicker.current;if(!picker?.dialog.open)throw new Error('Localized calendar did not open');if(language==='en-US'&&/\p{Script=Han}/u.test(picker.dialog.textContent))throw new Error('Chinese text in English calendar');await rpc('selftest_snapshot',{phase:language==='en-US'?'calendar-en':'calendar-zh'});picker.close();}}
 finally{PulseDatePicker.close();PulseI18n.use(original);}
}
