// Shared by ordinary Settings and first-run; native helper validates on save.
(() => {
  'use strict';
  window.souluParseVpnKey = (value, selectedProtocol) => {
    const link=value.trim();
    if(!link||link.length>16384)throw Error('Вставь ключ подключения');
    const protocol=selectedProtocol||(link.startsWith('sudoku://')?'sudoku':'vless');
    let address='',region='';
    if(protocol==='vless') {
      if(!link.startsWith('vless://'))throw Error('Поддерживаются ссылки vless:// и sudoku://');
      const url=new URL(link);
      if(!url.hostname||!url.username)throw Error('В ключе отсутствует сервер или идентификатор');
      address=url.hostname+(url.port?':'+url.port:'');
      region=decodeURIComponent(url.hash.slice(1))||url.searchParams.get('sni')||'Не указан';
    } else {
      if(!link.startsWith('sudoku://'))throw Error('Ожидается ссылка sudoku://');
      const encoded=link.slice(9).replace(/-/g,'+').replace(/_/g,'/');
      const data=JSON.parse(decodeURIComponent([...atob(encoded.padEnd(Math.ceil(encoded.length/4)*4,'='))].map(c=>'%'+c.charCodeAt(0).toString(16).padStart(2,'0')).join('')));
      if(!data.h||!data.p||!data.k)throw Error('В Sudoku-ключе отсутствуют сервер, порт или ключ');
      address=String(data.h)+':'+data.p;region=String(data.r||data.n||'Не указан');
    }
    return {protocol,link,address,region};
  };
})();
