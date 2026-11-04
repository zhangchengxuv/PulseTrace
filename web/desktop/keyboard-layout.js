'use strict';
// ANSI 104, Set-1 scan code + 256*E0 / 512*E1, matching KeyboardMonitor.
globalThis.PulseKeyboard = (() => {
    const keys = [];
    const add = (id, label, x, y, w=1, h=1, group='main') => keys.push({id,label,x,y,w,h,group});
    const row = (y, entries) => {let x=0;for(const [id,label,w=1] of entries){add(id,label,x,y,w);x+=w;}};
    add(1,'Esc',0,0,1,1,'function');
    for(let i=0;i<12;i++)add(i<10?59+i:87+i-10,'F'+(i+1),2+i+Math.floor(i/4)*.5,0,1,1,'function');
    row(1,[[41,'` ~'],[2,'1 !'],[3,'2 @'],[4,'3 #'],[5,'4 $'],[6,'5 %'],[7,'6 ^'],[8,'7 &'],[9,'8 *'],[10,'9 ('],[11,'0 )'],[12,'- _'],[13,'= +'],[14,'Backspace',2]]);
    row(2,[[15,'Tab',1.5],...[...'QWERTYUIOP'].map((c,i)=>[16+i,c]),[26,'[ {'],[27,'] }'],[43,'\\ |',1.5]]);
    row(3,[[58,'Caps Lock',1.75],...[...'ASDFGHJKL'].map((c,i)=>[30+i,c]),[39,'; :'],[40,"' \""],[28,'Enter',2.25]]);
    row(4,[[42,'Shift 左',2.25],...[...'ZXCVBNM'].map((c,i)=>[44+i,c]),[51,', <'],[52,'. >'],[53,'/ ?'],[54,'Shift 右',2.75]]);
    row(5,[[29,'Ctrl 左',1.25],[347,'Win 左',1.25],[56,'Alt 左',1.25],[57,'Space',6.25],[312,'Alt 右',1.25],[348,'Win 右',1.25],[349,'Menu',1.25],[285,'Ctrl 右',1.25]]);
    const nav=15.35,num=18.7;
    [[311,'Print Screen'],[70,'Scroll Lock'],[581,'Pause']].forEach(([id,label],i)=>add(id,label,nav+i,0,1,1,'navigation'));
    [[338,'Insert'],[327,'Home'],[329,'Page Up'],[339,'Delete'],[335,'End'],[337,'Page Down']].forEach(([id,label],i)=>add(id,label,nav+i%3,1+Math.floor(i/3),1,1,'navigation'));
    add(328,'↑',nav+1,4,1,1,'arrows');
    [[331,'←'],[336,'↓'],[333,'→']].forEach(([id,label],i)=>add(id,label,nav+i,5,1,1,'arrows'));
    [[69,'Num Lock'],[309,'Num /'],[55,'Num *'],[74,'Num −']].forEach(([id,label],i)=>add(id,label,num+i,1,1,1,'numpad'));
    [[71,'Num 7'],[72,'Num 8'],[73,'Num 9'],[75,'Num 4'],[76,'Num 5'],[77,'Num 6'],[79,'Num 1'],[80,'Num 2'],[81,'Num 3']].forEach(([id,label],i)=>add(id,label,num+i%3,2+Math.floor(i/3),1,1,'numpad'));
    add(78,'Num +',num+3,2,1,2,'numpad');add(284,'Num Enter',num+3,4,1,2,'numpad');
    add(82,'Num 0',num,5,2,1,'numpad');add(83,'Num .',num+2,5,1,1,'numpad');
    return {keys:keys.map(Object.freeze),width:22.7,height:6};
})();
