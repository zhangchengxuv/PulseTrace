'use strict';
async function validatePickerPersistence(){
 if(!state.self_test_picker)return;const check=(ok,message)=>{if(!ok)throw new Error('Picker: '+message);},wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
 rangeRoot.open=true;const controls=rangeRoot,committed={...range},original=rangeDraft.from;rangeFields.from.click();const picker=PulseDatePicker.current,dialog=picker.dialog,started=performance.now(),initialRefresh=window.pulseRefreshCount;
 check(dialog.open,'open');await rpc('selftest_snapshot',{phase:'top'});
 await wait(61000);check(performance.now()-started>=60000&&dialog.open&&controls===rangeRoot,'60s persistence');check(window.pulseRefreshCount>initialRefresh+5,'live refresh continued');
 dialog.querySelector('[aria-label="下个月"]').click();dialog.querySelector('[data-day="15"]').click();await refresh(true);check(dialog.open&&PulseDatePicker.current===picker,'month/date refresh');
 picker.hour.value='18';picker.hour.focus();await wait(3500);check(dialog.open&&picker.hour.value==='18','hour focus persisted');
 picker.minute.value='37';picker.minute.focus();await wait(3500);check(dialog.open&&picker.minute.value==='37','minute persisted');
 const initial=state.monitoring;try{await rpc('set_monitoring',{enabled:!initial});await refresh(true);check(dialog.open,'monitoring transition');}finally{await rpc('set_monitoring',{enabled:initial});}
 window.dispatchEvent(new Event('resize'));window.scrollTo({top:200,behavior:'instant'});await refresh(true);check(dialog.open&&controls===rangeRoot,'resize scroll refresh');
 check(range.from===committed.from&&range.to===committed.to,'query range unchanged during draft');picker.close();check(rangeDraft.from===original,'cancel restored');
 rangeFields.to.click();const end=PulseDatePicker.current;end.hour.value='12';end.minute.value='07';end.confirm();check(!PulseDatePicker.current&&rangeDirty,'confirm saves draft');check(range.to===committed.to,'draft is not queried automatically');
 rangeFields.from.click();PulseDatePicker.current.dialog.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}));check(!PulseDatePicker.current,'Escape');
 rangeFields.from.click();PulseDatePicker.current.dialog.dispatchEvent(new PointerEvent('pointerdown',{clientX:0,clientY:0,bubbles:true}));check(!PulseDatePicker.current,'outside');
 rangeDraft={...committed};rangeDirty=false;rangeFields.labels();window.scrollTo({top:0,behavior:'instant'});window.pulsePickerValidation={elapsed_ms:performance.now()-started,refreshes:window.pulseRefreshCount-initialRefresh,passed:true};console.info('Picker 60-second persistence and interactions passed');
}
