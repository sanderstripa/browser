(() => {
  'use strict';
  const $=id=>document.getElementById(id);
  const invoke=(action,payload={})=>new Promise((resolve,reject)=>window.cefQuery({request:JSON.stringify({action:'history.'+action,payload}),persistent:false,onSuccess:r=>{try{resolve(r?JSON.parse(r):null);}catch(e){reject(e);}},onFailure:(_c,m)=>reject(Error(m))}));
  let state,rows=[],more=false,epoch=0,busy=false,timer,lastSignature='',queryBounds;
  const t=(ru,en)=>state?.language==='en'?en:ru;
  const report=e=>{$('message').textContent=e.message||String(e);};
  const filters=[['all','Все записи','All visits'],['today','Сегодня','Today'],['yesterday','Вчера','Yesterday'],['week','Последние 7 дней','Last 7 days'],['older','Более старые записи','Older visits']];
  const ranges=[['15m','Последние 15 минут','Last 15 minutes'],['hour','Последний час','Last hour'],['day','Последние 24 часа','Last 24 hours'],['week','Последние 7 дней','Last 7 days'],['month','Последние 4 недели','Last 4 weeks'],['all','Всё время','All time']];
  const locale=()=>state?.language==='en'?'en':'ru';
  const midnight=(days=0)=>{const d=new Date();d.setHours(0,0,0,0);d.setDate(d.getDate()+days);return +d;};
  function bounds(){const kind=$('filter').value;return {begin:kind==='today'?midnight():kind==='yesterday'?midnight(-1):kind==='week'?Date.now()-7*864e5:0,end:kind==='yesterday'?midnight():kind==='older'?Date.now()-7*864e5:864e13};}
  function text(id,ru,en){$(id).textContent=t(ru,en);}
  function apply(next,initial=false){state=next;document.documentElement.dataset.theme=next.resolvedTheme;document.documentElement.lang=locale();document.title=t('История','History');
    text('heading','История','History');text('clearData','Очистить данные браузера…','Clear browsing data…');text('more','Показать ещё','Show more');
    $('profileLabel').textContent=state.incognito?t('ИНКОГНИТО','INCOGNITO'):'SOULU · '+(state.profileName||t('Текущий профиль','Current profile'));
    $('search').placeholder=t('Поиск по названию, адресу или домену','Search titles, URLs or domains');$('search').setAttribute('aria-label',$('search').placeholder);
    $('filter').setAttribute('aria-label',t('Период истории','History period'));$('visits').setAttribute('aria-label',t('Записи истории','History visits'));
    const filter=initial?state.filter:$('filter').value,range=$('range').value||'all';
    $('filter').replaceChildren(...filters.map(([v,ru,en])=>new Option(t(ru,en),v)));$('filter').value=filter||'all';
    $('range').replaceChildren(...ranges.map(([v,ru,en])=>new Option(t(ru,en),v)));$('range').value=range;
    text('clearTitle','Очистить данные браузера','Clear browsing data');
    text('clearDescription','Удаляются данные только этого профиля. Закладки, пароли и настройки сохраняются.','Only this profile’s data is removed. Bookmarks, passwords and settings are preserved.');
    text('rangeLabel','Диапазон истории','History time range');text('historyName','История браузера','Browsing history');text('historyHint','Посещения за выбранный период.','Visits in the selected time range.');
    text('sitesName','Файлы cookie и данные сайтов','Cookies and site data');text('sitesHint','За всё время. Включая хранилища сайтов; большинство сайтов выйдет из аккаунта.','All time. Includes site storage; most sites will sign you out.');
    text('cacheName','Кэшированные файлы и изображения','Cached files and images');text('cacheHint','За всё время. Временные файлы страниц; следующая загрузка может быть медленнее.','All time. Temporary page files; the next load may take longer.');
    text('autofillNote','Soulu пока не сохраняет автозаполнение форм. Сохранённые пароли не удаляются.','Soulu does not yet store form autofill data. Saved passwords are preserved.');
    text('allTimeText','Я понимаю, что выбранные cookies, данные сайтов и кэш удалятся за всё время, независимо от диапазона истории.','I understand that selected cookies, site data and cache will be removed for all time, regardless of the history range.');
    text('cancelClear','Отмена','Cancel');text('confirmClear','Очистить','Clear');
    $('search').disabled=$('filter').disabled=$('clearData').disabled=!!state.incognito;
  }
  async function load(reset=true){if(state.incognito)return draw();const token=++epoch;if(reset){rows=[];more=false;queryBounds=bounds();queryBounds.end=Math.min(queryBounds.end,Date.now()+1);}$('message').textContent=t('Загрузка…','Loading…');$('more').disabled=true;
    try{const result=await invoke('query',{search:$('search').value,...queryBounds,offset:rows.length});if(token!==epoch)return;rows.push(...result.rows);more=result.more;draw();$('message').textContent=state.saveHistory?'':t('Сохранение новых посещений выключено в настройках.','Recording new visits is turned off in Settings.');}catch(e){if(token===epoch){draw();report(e);}}finally{if(token===epoch)$('more').disabled=false;}}
  function dayLabel(time){const d=new Date(time);d.setHours(0,0,0,0);return +d===midnight()?t('Сегодня','Today'):+d===midnight(-1)?t('Вчера','Yesterday'):d.toLocaleDateString(locale(),{day:'numeric',month:'long',year:'numeric'});}
  function button(label,fn){const n=document.createElement('button');n.type='button';n.textContent=label;n.addEventListener('click',()=>Promise.resolve().then(fn).catch(report));return n;}
  function draw(){const list=$('visits');list.replaceChildren();let day='';for(const row of rows){const label=dayLabel(row.visited);if(state.groupDays&&label!==day){day=label;const h=document.createElement('h2');h.className='day';h.textContent=day;list.append(h);}
      const n=document.createElement('article');n.className='visit';n.dataset.visitId=row.id;const icon=document.createElement('img');icon.className='favicon';icon.alt='';icon.referrerPolicy='no-referrer';icon.loading='lazy';icon.src=/^(https?:|data:image\/)/.test(row.favicon)?row.favicon:'history-site.svg';icon.onerror=()=>{icon.onerror=null;icon.src='history-site.svg';};
      const link=button('',()=>invoke('open',{url:row.url,newTab:false}));link.className='visit-link';const title=document.createElement('strong'),url=document.createElement('small');title.textContent=row.title||row.url;url.textContent=row.url;link.title=row.url;link.append(title,url);
      const time=document.createElement('time');time.dateTime=new Date(row.visited).toISOString();time.textContent=new Date(row.visited).toLocaleTimeString(locale(),{hour:'2-digit',minute:'2-digit'});if(!state.groupDays)time.textContent=new Date(row.visited).toLocaleDateString(locale())+' '+time.textContent;time.title=new Date(row.visited).toLocaleString(locale());
      const actions=document.createElement('div');actions.className='row-actions';const open=button('↗',()=>invoke('open',{url:row.url,newTab:true}));open.setAttribute('aria-label',t('Открыть в новой вкладке: ','Open in a new tab: ')+(row.title||row.url));open.title=t('Открыть в новой вкладке','Open in a new tab');
      const remove=button('×',async()=>{const id=row.id;const index=rows.findIndex(x=>x.id===id);remove.disabled=true;try{await invoke('remove',{id});await load();list.querySelectorAll('.visit-link')[Math.min(index,rows.length-1)]?.focus();}finally{remove.disabled=false;}});remove.setAttribute('aria-label',t('Удалить запись: ','Remove visit: ')+(row.title||row.url));remove.title=t('Удалить запись','Remove visit');actions.append(open,remove);n.append(icon,link,time,actions);list.append(n);}
    $('empty').hidden=rows.length>0;$('more').hidden=!more;
    const search=!!$('search').value||$('filter').value!=='all';text('emptyTitle',state.incognito?'История недоступна в инкогнито':search?'Ничего не найдено':'История пока пуста',state.incognito?'History is unavailable in incognito':search?'No results found':'No history yet');
    text('emptyText',state.incognito?'Приватные посещения не сохраняются.':search?'Попробуйте другой запрос или период.':'Посещённые сайты появятся здесь.',state.incognito?'Private visits are never saved.':search?'Try another search or time range.':'Websites you visit will appear here.');}
  $('visits').addEventListener('keydown',e=>{const item=e.target.closest('.visit');if(!item)return;const visits=[...$('visits').querySelectorAll('.visit')],i=visits.indexOf(item);if(e.key==='ArrowDown'||e.key==='ArrowUp'){e.preventDefault();visits[i+(e.key==='ArrowDown'?1:-1)]?.querySelector('.visit-link').focus();}if(e.key==='Delete'&&e.target.matches('.visit-link')){e.preventDefault();item.querySelector('.row-actions button:last-child').click();}});
  $('search').addEventListener('input',()=>{clearTimeout(timer);++epoch;timer=setTimeout(()=>load(),160);});$('filter').onchange=()=>load();$('more').onclick=()=>load(false);
  function controls(){const web=$('sitesChoice').checked||$('cacheChoice').checked;$('allTimeLabel').hidden=!web;$('confirmClear').disabled=busy||!($('historyChoice').checked||web)||(web&&!$('allTimeAck').checked);}
  function openClear(){if(state.incognito||busy)return;$('clearMessage').textContent='';$('allTimeAck').checked=false;controls();if(!$('clearDialog').open)$('clearDialog').showModal();}
  window.souluHistoryClear=openClear;$('clearData').onclick=openClear;
  for(const id of ['historyChoice','sitesChoice','cacheChoice','allTimeAck'])$(id).onchange=controls;
  $('cancelClear').onclick=()=>{if(!busy)$('clearDialog').close();};$('clearDialog').addEventListener('cancel',e=>{if(busy)e.preventDefault();});
  document.addEventListener('keydown',e=>{if(e.key==='Escape'&&$('clearDialog').open){e.preventDefault();if(!busy)$('clearDialog').close();}});
  $('clearDialog').addEventListener('close',()=>$('clearData').focus());
  $('clearForm').onsubmit=async e=>{e.preventDefault();if($('confirmClear').disabled)return;busy=true;controls();$('cancelClear').disabled=true;for(const id of ['historyChoice','sitesChoice','cacheChoice','range','allTimeAck'])$(id).disabled=true;
    $('clearMessage').textContent=t('Очистка…','Clearing…');try{const result=await invoke('clear',{history:$('historyChoice').checked,sites:$('sitesChoice').checked,cache:$('cacheChoice').checked,range:$('range').value,webAllTimeAcknowledged:$('allTimeAck').checked});await load();if(!result.ok){$('clearMessage').textContent=result.error;return;}$('clearDialog').close();$('message').textContent=t('Выбранные данные очищены.','Selected data cleared.');}catch(error){$('clearMessage').textContent=error.message;}finally{busy=false;$('cancelClear').disabled=false;for(const id of ['historyChoice','sitesChoice','cacheChoice','range','allTimeAck'])$(id).disabled=false;controls();}};
  async function init(){try{const next=await invoke('state');apply(next,true);lastSignature=JSON.stringify(next);await load();if(next.openClear)openClear();setInterval(async()=>{if(busy)return;try{const next=await invoke('state');const signature=JSON.stringify(next);if(signature!==lastSignature){lastSignature=signature;apply(next);draw();}}catch(e){report(e);}},1500);}catch(e){report(e);}}
  window.addEventListener('focus',()=>{if(state&&!busy&&!$('clearDialog').open)load();});init();
})();
