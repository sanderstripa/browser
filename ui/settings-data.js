(() => {
  const api=window.browserShell,$=id=>document.getElementById(id);
  if(!api?.getSiteRules)return;
  let passwords=[],rules=null,domain="",profile="",loading=false;
  const names={geolocation:"Геолокация",camera:"Камера",microphone:"Микрофон",
    notifications:"Уведомления",sound:"Звук",popups:"Всплывающие окна",downloads:"Загрузки"};
  const message=text=>$("dataMessage").textContent=text;
  const element=(tag,text,cls)=>{const node=document.createElement(tag);if(text!==undefined)node.textContent=text;if(cls)node.className=cls;return node;};
  const button=(text,action)=>{const node=element("button",text);node.type="button";node.onclick=()=>Promise.resolve().then(action).catch(error=>message(error.message));return node;};
  function normalize(input){
    const url=new URL(input.includes("://")?input:"https://"+input);
    if(!["http:","https:"].includes(url.protocol)||!url.hostname)throw new Error("Укажите HTTP/HTTPS-сайт");
    return url.hostname.toLowerCase().replace(/\.$/,"");
  }
  function renderPasswords(){
    const query=$("passwordSearch").value.toLowerCase();$("passwordList").replaceChildren();
    for(const item of passwords.filter(row=>(row.origin+" "+row.username).toLowerCase().includes(query))){
      const row=element("div",undefined,"profile-row credential-row"),text=element("span");
      text.append(element("b",item.origin),element("small",item.username||"Без логина"));
      const secret=element("input");secret.type="password";secret.readOnly=true;secret.value="••••••••";secret.setAttribute("aria-label","Сохранённый пароль");
      let shown=false,timer=null;
      const reveal=button("Показать",async()=>{
        if(shown){clearTimeout(timer);secret.type="password";secret.value="••••••••";shown=false;reveal.textContent="Показать";return;}
        secret.value=await api.revealPassword(item.id);secret.type="text";shown=true;reveal.textContent="Скрыть";
        timer=setTimeout(()=>{secret.type="password";secret.value="••••••••";shown=false;reveal.textContent="Показать";},15000);
      });
      row.append(text,secret,reveal,button("Копировать",async()=>{await api.copyPassword(item.id,"password");message("Пароль скопирован. Буфер очистится через 30 секунд, если вы не скопируете другое содержимое.");}),
        button("Изменить",()=>{$("passwordOrigin").value=item.origin;$("passwordUsername").value=item.username;$("passwordSecret").value="";$("passwordSecret").focus();}),
        button("Удалить",async()=>{if(confirm("Удалить этот сохранённый пароль?")){passwords=await api.removePassword(item.id);renderPasswords();}}));
      $("passwordList").append(row);
    }
    if(!$("passwordList").children.length)$("passwordList").append(element("p","Записей нет","note"));
  }
  function permissionRows(container,site){
    container.replaceChildren();
    for(const [key,label] of Object.entries(names)){
      const row=element("label",undefined,"row"),select=element("select");select.setAttribute("aria-label",label);
      if(site)select.append(new Option("По умолчанию", "-1"));
      const labels=key==="sound"?["Разрешить","Приглушить","Блокировать"]:["Разрешить","Спрашивать","Запретить"];
      labels.forEach((text,i)=>select.append(new Option(text,String(i))));
      const value=site?rules.sites?.[site]?.[key]:rules.defaults[key];select.value=String(value??-1);
      select.onchange=async()=>{try{
        if(select.value==="-1"){
          // Native remove-one-permission preserves the domain's other rules.
          rules=await api.setSiteRule({domain:site,permission:key,value:-1});
        }else rules=await api.setSiteRule({domain:site||"",permission:key,value:Number(select.value)});
        renderRules();
      }catch(error){message(error.message);renderRules();}};
      row.append(element("span",label),select);container.append(row);
    }
  }
  function renderRules(){
    if(!rules)return;permissionRows($("permissionDefaults"),"");
    $("siteEditing").textContent=domain?"Правила: "+domain:"Выберите домен для изменения правил";
    if(domain)permissionRows($("sitePermissions"),domain);else $("sitePermissions").replaceChildren();
    $("contentBlocking").checked=Boolean(rules.blocking.enabled);
    $("siteBlocking").disabled=!domain;
    $("siteBlocking").value=rules.blocking.sites[domain]===undefined?"2":rules.blocking.sites[domain]?"1":"0";
    const query=$("siteSearch").value.toLowerCase();$("siteList").replaceChildren();
    const sites=new Set([...Object.keys(rules.sites),...Object.keys(rules.blocking.sites)]);
    for(const site of [...sites].sort().filter(site=>site.includes(query))){
      const row=element("div",undefined,"profile-row");
      row.append(element("span",site),button("Изменить",()=>{domain=site;$("siteDomain").value=site;renderRules();}),
        button("Сбросить разрешения",async()=>{rules=await api.resetSiteRules(site);renderRules();}));$("siteList").append(row);
    }
  }
  async function refresh(state){
    if(loading)return;loading=true;
    try{
      const current=state||await api.getState();profile=current.activeProfileId;
      const target=$("passwordTarget"),selected=target.value;target.replaceChildren();
      for(const item of current.profiles||[])target.append(new Option(item.name,item.id));
      target.value=(current.profiles||[]).some(p=>p.id===selected)?selected:profile;
      [passwords,rules]=await Promise.all([api.getPasswords(),api.getSiteRules()]);
      renderPasswords();renderRules();
    }catch(error){message(error.message);}finally{loading=false;}
  }
  $("passwordSearch").oninput=renderPasswords;$("siteSearch").oninput=renderRules;
  $("savePassword").onclick=async()=>{try{
    const origin=new URL($("passwordOrigin").value).origin,username=$("passwordUsername").value;
    if(passwords.some(p=>p.origin===origin&&p.username===username)&&!confirm("Обновить пароль существующей записи?"))return;
    passwords=await api.addPassword({origin,username,password:$("passwordSecret").value});
    $("passwordSecret").value="";renderPasswords();message("Сохранено");
  }catch(error){message(error.message);}};
  $("discoverPasswords").onclick=async()=>{try{
    const sources=await api.passwordSources();$("passwordSource").replaceChildren();
    for(const source of sources)$("passwordSource").append(new Option(source.browser+" — "+source.name,source.id));
    $("importReport").textContent=sources.length?"Найдено профилей: "+sources.length:"Локальные хранилища паролей не обнаружены";
  }catch(error){message(error.message);}};
  $("importPasswords").onclick=async()=>{
    const source=$("passwordSource").value,target=$("passwordTarget").value;if(!source||!target)return;
    if(!confirm("Импортировать локальные пароли из выбранного браузера в выбранный профиль Soulu?"))return;
    $("importPasswords").disabled=true;$("importReport").textContent="Импорт…";
    try{const result=await api.importPasswords({source,target});
      $("importReport").textContent=`Импортировано: ${result.imported}; пропущено: ${result.skipped}; ошибки: ${result.failed}; защищено / не поддерживается: ${result.protected}. ${result.message}`;
      await refresh();
    }catch(error){$("importReport").textContent=error.message;}finally{$("importPasswords").disabled=false;}
  };
  $("editSite").onclick=()=>{try{domain=normalize($("siteDomain").value.trim());renderRules();}catch(error){message(error.message);}};
  $("resetSite").onclick=async()=>{try{domain=normalize($("siteDomain").value.trim());rules=await api.resetSiteRules(domain);renderRules();}catch(error){message(error.message);}};
  $("resetAllSites").onclick=async()=>{try{rules=await api.resetSiteRules("");renderRules();}catch(error){message(error.message);}};
  $("contentBlocking").onchange=async event=>{try{rules=await api.setContentBlocking({domain:"",value:event.target.checked?1:0});renderRules();}catch(error){message(error.message);}};
  $("siteBlocking").onchange=async event=>{try{rules=await api.setContentBlocking({domain,value:Number(event.target.value)});renderRules();}catch(error){message(error.message);}};
  api.onState(state=>{if(state.activeProfileId!==profile)refresh(state);});
  window.addEventListener("blur",()=>{for(const input of $("passwordList").querySelectorAll("input")){input.type="password";input.value="••••••••";}});
  refresh();
})();
