(() => {
  'use strict';
  const $=id=>document.getElementById(id);
  const invoke=(action,payload=null)=>new Promise((resolve,reject)=>window.cefQuery({request:JSON.stringify({action:'onboarding.'+action,payload}),persistent:false,onSuccess:v=>resolve(v?JSON.parse(v):null),onFailure:(_,m)=>reject(Error(m))}));
  let flow={},sources=[],busy=false,vpnDraft='';
  const el=(tag,text,className)=>{const n=document.createElement(tag);if(text)n.textContent=text;if(className)n.className=className;return n;};
  const image=(src,className)=>{const n=el('img',null,className);n.src=src;n.alt='';return n;};
  const report=e=>{$('message').textContent=e.message||String(e);};
  async function run(fn){if(busy)return;busy=true;document.querySelector('main').setAttribute('aria-busy','true');disable();try{await fn();}catch(e){report(e);}finally{busy=false;document.querySelector('main').setAttribute('aria-busy','false');disable();}}
  function disable(){document.querySelectorAll('button,input').forEach(n=>{n.disabled=busy||n.dataset.unavailable==='true';});}
  const persist=async patch=>{flow=await invoke('progress',{...patch,step:patch.step||flow.step||1});};
  const go=async step=>{await persist({step});render();$('title').focus();};
  function button(text,fn,kind=''){const n=el('button',text,kind);n.type='button';n.onclick=()=>run(fn);return n;}
  function note(text){$('controls').append(el('p',text,'note'));}
  function check(parent,text,checked,onChange,options={}){
    const row=el('label',null,options.className||'row');
    if(options.icon)row.append(image(options.icon));
    if(options.symbol)row.append(el('span',options.symbol,'symbol'));
    const copy=el('span',null,'copy');copy.append(el('strong',text));if(options.detail)copy.append(el('small',options.detail));
    const input=el('input');input.type=options.radio?'radio':'checkbox';input.checked=checked;
    input.setAttribute('aria-label',text);if(options.radio)input.name='source';
    input.dataset.unavailable=String(!!options.disabled);input.disabled=!!options.disabled;
    input.onchange=()=>run(async()=>{await onChange(input.checked);render(false);});
    row.append(copy,input);parent.append(row);
  }
  function art(step){const container=$('art');container.replaceChildren();
    if(step===4){container.append(el('div',null,'glass-shield'),el('div',null,'orbit'));return;}
    if(step===5){const tunnel=el('div',null,'tunnel');tunnel.append(el('div',null,'glass-shield'));container.append(tunnel);return;}
    container.append(image('branding/soulu-512.png','hero-logo'));
    if(step===2||step===3){const data=el('div',null,'data-art');for(const symbol of ['♧','◷','⚿'])data.append(el('span',symbol));container.append(data);}
  }
  function summary(){const ul=el('ul',null,'summary');const r=flow.importReport;
    const values=[r?(r.imported>0?`Импортировано паролей: ${r.imported}${r.status!=='ok'?' · частично':''}`:'Пароли не импортированы'):(flow.importStarted?'Импорт прерван — проверь менеджер паролей':'Импорт пропущен'),flow.privacyApplied?'Приватность настроена':'Приватность: текущие настройки сохранены',flow.vpnAdded?'VPN добавлен':'VPN можно добавить позже'];
    const successes=[!!r&&r.imported>0,flow.privacyApplied,flow.vpnAdded];
    values.forEach((text,i)=>{const li=el('li',null,successes[i]?'':'pending');li.append(el('span',successes[i]?'✓':'—'),document.createTextNode(text));ul.append(li);});$('controls').append(ul);
  }
  function render(clearMessage=true){
    const focused=clearMessage?null:document.activeElement?.getAttribute('aria-label');
    const step=flow.step||1;art(step);$('counter').textContent=`${step} из 6`;
    $('controls').replaceChildren();$('actions').replaceChildren();if(clearMessage)$('message').textContent='';
    const titles=['Добро пожаловать в Soulu','Импортировать данные','Что перенести?','Приватность и защита','Встроенный VPN','Всё готово — добро пожаловать в Soulu'];
    const source=sources.find(s=>s.id===flow.source);
    const descriptions=['Спокойный браузер с чистым интерфейсом, вкладками, закладками и приватностью под твоим контролем.',sources.length?'Найдены браузеры на этом устройстве. Выбери, откуда перенести данные в Soulu.':'Поддерживаемые браузеры для импорта не найдены.',`Выбери, какие данные импортировать из ${source?.browser||'выбранного браузера'}.`,'Базовые настройки, которые можно изменить позже.','Добавь ключ подключения сейчас или настрой позже.','Базовые параметры настроены. Остальное можно изменить в настройках в любой момент.'];
    $('title').textContent=titles[step-1];$('description').textContent=descriptions[step-1];
    const actions=$('actions'),controls=$('controls');
    const finish=skip=>invoke('finish',{skip});
    if(step===1){note('Первичная настройка займёт пару минут.');actions.append(button('Начать',()=>go(2),'primary'),button('Не сейчас',()=>finish(true)));}
    if(step===2){
      for(const s of sources)check(controls,`${s.browser} · ${s.name}`,flow.source===s.id,()=>persist({source:s.id}),{icon:s.icon,radio:true});
      controls.append(button('Начать с чистого листа',()=>go(4),'link'));
      actions.append(button('Назад',()=>go(1)),button('Пропустить',()=>go(4)),button(sources.length?'Далее':'Начать с чистого листа',()=>go(source?3:4),'primary'));
    }
    if(step===3){
      const chip=el('div',null,'source-chip');if(source?.icon)chip.append(image(source.icon));chip.append(document.createTextNode(`Источник: ${source?.browser||'недоступен'}${source?' · '+source.name:''}`));controls.append(chip);
      const types=el('div',null,'types');controls.append(types);
      check(types,'Закладки',false,()=>{}, {className:'type',symbol:'♧',detail:'Пока не поддерживается',disabled:true});
      check(types,'История',false,()=>{}, {className:'type',symbol:'◷',detail:'Пока не поддерживается',disabled:true});
      check(types,'Логины и пароли',flow.passwords!==false,v=>persist({passwords:v}),{className:'type',symbol:'⚿',disabled:!source||!!flow.importStarted});
      note('Импорт выполняется локально. Защищённые и неподдерживаемые записи не обходятся. Дополнительный импорт доступен в настройках.');
      if(flow.importReport){const r=flow.importReport;$('message').textContent=`Импортировано: ${r.imported}. Уже есть / пропущено: ${r.skipped}. Защищено: ${r.protected}. Ошибки: ${r.failed}.\n${r.status==='error'?r.message:'Можно продолжить настройку.'}`;}
      else if(flow.importStarted)$('message').textContent='Импорт мог завершиться до закрытия браузера. Повторный импорт автоматически не запускается; проверь пароли в настройках.';
      actions.append(button('Назад',()=>go(2)),button('Пропустить',()=>go(4)));
      const primary=button(flow.importStarted?'Продолжить':'Импортировать',async()=>{if(flow.importStarted){await go(4);return;}await persist({passwords:flow.passwords!==false});await invoke('import');await reload();if(flow.importReport?.status==='ok'&&flow.importReport.imported>0)await go(4);else render();},'primary');
      primary.dataset.unavailable=String(!flow.importStarted&&(!source||flow.passwords===false));actions.append(primary);
    }
    if(step===4){controls.className='privacy';
      check(controls,'Блокировать рекламу',flow.adblock,v=>persist({adblock:v}),{symbol:'♢',detail:'Скрывать рекламные элементы на сайтах.'});
      check(controls,'Проверять разрешения сайтов',flow.askPermissions,v=>persist({askPermissions:v}),{symbol:'♙',detail:'Показывать запросы на доступ к камере, микрофону и геолокации. Если выключено — запрещать доступ.'});
      note('Расширенные правила и исключения доступны в настройках.');
      actions.append(button('Назад',()=>go(source?3:2)),button('Пропустить',()=>go(5)),button('Продолжить',async()=>{await invoke('privacy');flow.privacyApplied=true;await go(5);},'primary'));
    }else controls.className='';
    if(step===5){const input=el('input',null,'vpn-input');input.id='vpnKey';input.type='password';input.autocomplete='off';input.placeholder='Вставь ключ или ссылку подключения';input.setAttribute('aria-label','Ключ подключения VPN');input.value=vpnDraft;input.maxLength=16384;input.oninput=()=>{vpnDraft=input.value;$('message').textContent='';};controls.append(input);
      note('Поддерживаются протоколы: Xray (VLESS), Sudoku');
      controls.append(button('Проверить ключ',()=>{window.souluParseVpnKey(vpnDraft);$('message').textContent='Формат ключа распознан. При добавлении его проверит VPN backend. Подключение не запускается.';},'link'));
      actions.append(button('Назад',()=>go(4)),button('Пропустить',()=>go(6)),button('Добавить',async()=>{const parsed=window.souluParseVpnKey(vpnDraft);const result=await invoke('vpn',parsed);if(result?.ok===false)throw Error(result.error||'Ключ не сохранён');flow.vpnAdded=true;vpnDraft='';await go(6);},'primary'));
    }
    if(step===6){summary();note('Если хочешь, можно сразу сделать Soulu браузером по умолчанию.');controls.lastChild.classList.add('divider');
      actions.append(button('Не сейчас',()=>finish(false)),button('Сделать браузером по умолчанию',async()=>{const result=await invoke('default');if(!result.ok)throw Error('Не удалось открыть стандартные приложения Windows');$('message').textContent=result.message;},'primary'),button('Открыть Soulu',()=>finish(false),'link'));
    }
    disable();
    if(focused)for(const n of document.querySelectorAll('input'))if(n.getAttribute('aria-label')===focused)n.focus();
  }
  async function reload(){const state=await invoke('get');flow=state.flow;sources=state.sources;
    if(!flow.source&&sources.length)flow.source=sources[0].id;
    if(flow.passwords===undefined)flow.passwords=true;
    if(flow.adblock===undefined)flow.adblock=state.policy.blocking.enabled;
    if(flow.askPermissions===undefined)flow.askPermissions=['camera','microphone','geolocation','notifications'].every(k=>state.policy.defaults[k]===1);
  }
  document.addEventListener('keydown',e=>{if(e.key==='Escape'){e.preventDefault();return;}if(e.key==='Enter'&&e.target.tagName!=='BUTTON'&&!['checkbox','radio'].includes(e.target.type)){const primary=$('actions').querySelector('.primary');if(primary&&!primary.disabled){e.preventDefault();primary.click();}}});
  run(async()=>{await reload();await persist({source:flow.source||'',passwords:flow.passwords,adblock:flow.adblock,askPermissions:flow.askPermissions});render();}).catch(report);
})();
