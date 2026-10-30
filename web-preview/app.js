'use strict';
const $=s=>document.querySelector(s);
const $$=s=>Array.from(document.querySelectorAll(s));
const designs={minimal:{title:'精致极简',label:'01 / MINIMAL',description:'细环、主时间、小百分比，信息最克制。'},instrument:{title:'精密仪表',label:'02 / INSTRUMENT',description:'低亮刻度、上下分区，更有仪表的秩序感。'},soft:{title:'柔和层次',label:'03 / SOFT DEPTH',description:'深色底盘与轻微晕光，更柔和、更有层次。'}};
const order=Object.keys(designs);
let mode='h',rotation=0,paused=false,selected=null,detailStyle='minimal';
let seconds=3*3600+37*60+45;
let weeklySeconds=3*86400+19*3600+7*60+45;
let last=performance.now();
let fraction=0;
function format(total){const pad=n=>String(n).padStart(2,'0');const h=Math.floor(total/3600),m=Math.floor(total/60)%60,s=total%60;return mode==='w'?`${pad(Math.floor(total/86400))}:${pad(h%24)}:${pad(m)}:${pad(s)}`:`${pad(h)}:${pad(m)}:${pad(s)}`}
function paintTime(){const text=format(mode==='w'?weeklySeconds:seconds);$$('.countdown').forEach(el=>{if(el.textContent!==text)el.textContent=text})}
function paintQuota(){const pct=Number($('#quota').value);$('#quotaValue').textContent=`${pct}%`;$$('.percentage').forEach(el=>{el.innerHTML=`${pct}<span>%</span>`});$$('.progress').forEach(el=>{el.style.strokeDasharray=`${pct} 100`;el.style.opacity=pct===0?'0':'1'})}
function selectStyle(style){selected=style;$$('.study').forEach(el=>el.classList.toggle('selected',el.dataset.style===style));$$('[data-choose]').forEach(btn=>{const chosen=btn.dataset.choose===style;btn.innerHTML=chosen?'已选择 <span>✓</span>':'选择这版 <span>↗</span>';btn.setAttribute('aria-pressed',String(chosen))});$('#selectionText').textContent=`已选择 ${order.indexOf(style)+1<10?'0':''}${order.indexOf(style)+1} · ${designs[style].title}`;$('#selectionDetail').textContent=designs[style].description;try{localStorage.setItem('reset-ring-design',style)}catch{}}
function makeTicks(){const ns='http://www.w3.org/2000/svg';$$('.ticks').forEach(group=>{if(group.children.length)return;for(let i=0;i<60;i++){const a=(i*6-90)*Math.PI/180;const major=i%5===0;const line=document.createElementNS(ns,'line');const inner=major?174:179;line.setAttribute('x1',233+Math.cos(a)*inner);line.setAttribute('y1',233+Math.sin(a)*inner);line.setAttribute('x2',233+Math.cos(a)*184);line.setAttribute('y2',233+Math.sin(a)*184);if(major)line.classList.add('major');group.append(line)}})}
function setMode(next){mode=next;document.body.classList.toggle('weekly',mode==='w');document.documentElement.style.setProperty('--dial-color',mode==='w'?'#aa9874':'#7d9d89');document.documentElement.style.setProperty('--dial-rgb',mode==='w'?'170,152,116':'125,157,137');$$('[data-mode]').forEach(btn=>{const active=btn.dataset.mode===mode;btn.classList.toggle('active',active);btn.setAttribute('aria-pressed',String(active))});paintTime()}
function openDetail(style){detailStyle=style;$('#detailIndex').textContent=designs[style].label;$('#detailTitle').textContent=designs[style].title;const bezel=$(`.study[data-style="${style}"] .watch-bezel`).cloneNode(true);$('#largeDial').replaceChildren(bezel);paintTime();paintQuota();if(!$('#detail').open)$('#detail').showModal()}
$$('[data-mode]').forEach(btn=>btn.addEventListener('click',()=>setMode(btn.dataset.mode)));
$('#quota').addEventListener('input',paintQuota);
$('#brightness').addEventListener('input',()=>{$('#brightnessValue').textContent=`${$('#brightness').value}%`;document.documentElement.style.setProperty('--brightness',Number($('#brightness').value)/100)});
$('#rotate').addEventListener('click',()=>{rotation+=90;document.documentElement.style.setProperty('--rotation',`${rotation}deg`);$('#rotate span').textContent=`旋转 ${rotation%360}°`});
$('#pause').addEventListener('click',()=>{paused=!paused;$('#pause').setAttribute('aria-pressed',String(paused));$('#pause span').textContent=paused?'继续':'暂停';$('#pause svg').innerHTML=paused?'<path d="m8 5 11 7-11 7Z"/>':'<path d="M9 5v14M15 5v14"/>';last=performance.now()});
$$('[data-enlarge]').forEach(btn=>btn.addEventListener('click',()=>openDetail(btn.dataset.enlarge)));
$$('[data-choose]').forEach(btn=>btn.addEventListener('click',()=>selectStyle(btn.dataset.choose)));
$('#closeDetail').addEventListener('click',()=>$('#detail').close());
$('#detail').addEventListener('click',e=>{if(e.target===$('#detail')){const b=e.target.getBoundingClientRect();if(e.clientX<b.left||e.clientX>b.right||e.clientY<b.top||e.clientY>b.bottom)e.target.close()}});
$('#detailChoose').addEventListener('click',()=>{selectStyle(detailStyle);$('#detail').close()});
function cycle(step){openDetail(order[(order.indexOf(detailStyle)+step+order.length)%order.length])}
$('#prevDesign').addEventListener('click',()=>cycle(-1));$('#nextDesign').addEventListener('click',()=>cycle(1));
document.addEventListener('keydown',e=>{if(!$('#detail').open)return;if(e.key==='ArrowLeft')cycle(-1);if(e.key==='ArrowRight')cycle(1)});
makeTicks();paintQuota();paintTime();
try{const saved=localStorage.getItem('reset-ring-design');if(designs[saved])selectStyle(saved)}catch{}
setInterval(()=>{const now=performance.now();if(!paused){fraction+=(now-last)/1000;const elapsed=Math.floor(fraction);if(elapsed>0){seconds=Math.max(0,seconds-elapsed);weeklySeconds=Math.max(0,weeklySeconds-elapsed);fraction-=elapsed;paintTime()}}last=now},100);
