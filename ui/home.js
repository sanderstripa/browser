(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const invoke = (action, payload = null) => new Promise((resolve, reject) => window.cefQuery({request:JSON.stringify({action,payload}),persistent:false,onSuccess:v=>resolve(v?JSON.parse(v):null),onFailure:(_,message)=>reject(new Error(message))}));
  let state = {}, editIndex = -1, revision = 0;
  const el = (tag, text) => {const node=document.createElement(tag);if(text)node.textContent=text;return node;};
  const report = e => { $('error').textContent=e.message; };
  const set = async patch => {const next=await invoke('home.set',patch);apply(next);};
  const labels = {homeShowLogo:'Soulu',homeShowSearch:'Поиск / Search',homeShowShortcuts:'Быстрые ссылки / Shortcuts',homeShowWeather:'Погода / Weather',homeShowBackground:'Мягкий фон / Soft background'};
  for(const [key,label] of Object.entries(labels)){const row=el('label',label);row.className='toggle';const input=el('input');input.type='checkbox';input.dataset.key=key;input.onchange=()=>set({[key]:input.checked}).catch(report);row.append(input);$('toggles').append(row);}
  function openEditor(index) {editIndex=index;const row=(state.homeShortcuts||[])[index];$('shortcutName').value=row?.name||'';$('shortcutUrl').value=row?.url||'';$('editorError').textContent='';$('editor').showModal();$('shortcutName').focus();}
  function tool(text,label,fn){const button=el('button',text);button.type='button';button.setAttribute('aria-label',label);button.onclick=()=>Promise.resolve(fn()).catch(report);return button;}
  function apply(next){
    state=next;revision++;const english=state.language==='en';document.documentElement.lang=english?'en':'ru';
    document.body.dataset.theme=state.resolvedTheme||state.theme||'system';document.body.dataset.background=String(state.homeShowBackground!==false);
    $('logo').hidden=state.homeShowLogo===false;$('search').hidden=state.homeShowSearch===false;$('shortcuts').hidden=state.homeShowShortcuts===false;$('weather').hidden=state.homeShowWeather===false;
    $('query').placeholder=english?'Search or enter address':'Поиск или адрес';$('add').textContent=english?'+ Add shortcut':'+ Добавить ссылку';
    for(const input of document.querySelectorAll('[data-key]'))input.checked=state[input.dataset.key]!==false;
    if(document.activeElement!==$('city'))$('city').value=state.homeWeatherCity||'';
    $('links').replaceChildren();const rows=Array.isArray(state.homeShortcuts)?state.homeShortcuts:[];
    rows.slice(0,12).forEach((row,index)=>{
      let url;try{url=new URL(row.url);if(!['http:','https:'].includes(url.protocol))return;}catch{return;}
      const tile=el('div');tile.className='shortcut';const a=el('a');a.href=url.href;a.title=url.href;
      // Local fallback is immediate; only the chosen site receives an optional favicon request.
      const icon=el('img');icon.className='favicon';icon.alt='';const initial=(url.hostname[0]||'S').toUpperCase();
      const badge=document.createElement('span');badge.className='favicon shortcut-initial';badge.textContent=initial;
      icon.referrerPolicy='no-referrer';icon.onerror=()=>{icon.onerror=null;icon.replaceWith(badge);};
      icon.onload=()=>{badge.replaceWith(icon);};
      icon.src=url.origin+'/favicon.ico';
      a.append(badge,el('span',row.name));a.onclick=e=>{if(e.ctrlKey||e.metaKey||e.shiftKey||e.button!==0)return;e.preventDefault();invoke('home.navigate',url.href).catch(report);};
      const tools=el('div');tools.className='shortcut-tools';tools.append(tool('✎','Изменить / Edit',()=>openEditor(index)),tool('×','Удалить / Delete',()=>set({homeShortcuts:rows.filter((_,i)=>i!==index)})));
      const move=tool('←','Переместить влево / Move left',()=>{const next=rows.map(x=>({...x}));[next[index-1],next[index]]=[next[index],next[index-1]];return set({homeShortcuts:next});});move.disabled=index===0;tools.append(move);tile.append(a,tools);$('links').append(tile);
    });$('add').disabled=rows.length>=12;
    const token=revision;
    if(!state.homeShowWeather)return;
    $('weatherToggle').textContent=(state.homeWeatherCity?state.homeWeatherCity+' · ':'')+(english?'Weather unavailable':'Погода недоступна');
    invoke('home.weather').then(data=>{if(token!==revision)return;$('weatherMessage').textContent=data.reason==='provider-not-configured'?(english?'Weather provider is not connected yet. No location is requested.':'Погодный сервис пока не подключён. Геолокация не запрашивается.'):(english?'Weather unavailable':'Погода недоступна');}).catch(()=>{$('weatherMessage').textContent=english?'Weather unavailable':'Погода недоступна';});
  }
  window.souluHomeApply=apply;
  $('search').onsubmit=e=>{e.preventDefault();const value=$('query').value.trim();if(value)invoke('home.navigate',value).catch(report);};
  $('add').onclick=()=>openEditor(-1);$('cancel').onclick=()=>$('editor').close();
  $('shortcutForm').onsubmit=async e=>{e.preventDefault();const rows=(state.homeShortcuts||[]).map(x=>({...x}));const row={name:$('shortcutName').value.trim(),url:$('shortcutUrl').value.trim()};if(editIndex<0)rows.push(row);else rows[editIndex]=row;try{await set({homeShortcuts:rows});$('editor').close();}catch(error){$('editorError').textContent=error.message;}};
  $('weatherToggle').onclick=()=>{const expanded=$('weatherDetail').hidden;$('weatherDetail').hidden=!expanded;$('weatherToggle').setAttribute('aria-expanded',String(expanded));};
  $('saveCity').onclick=()=>set({homeWeatherCity:$('city').value.trim()}).catch(report);
  $('customize').onclick=()=>$('preferences').showModal();
  invoke('home.get').then(apply).catch(report);
})();
