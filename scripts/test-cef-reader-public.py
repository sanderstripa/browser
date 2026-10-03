"""Public article checks in the actual native reader; output only metadata/counts."""
import base64
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

spec=importlib.util.spec_from_file_location('storage',Path(__file__).with_name('test-cef-storage.py'))
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
articles=[
 ('https://blog.mozilla.org/en/firefox/firefox-ai/ai-link-previews-firefox/', 'link previews'),
 ('https://web.dev/articles/multi-device-content?hl=en', 'content'),
 ('https://developer.chrome.com/blog/inside-browser-part1?hl=en', 'browser'),
]
out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
rows=[]
def wait(fn,timeout=45):
 deadline=time.monotonic()+timeout
 while time.monotonic()<deadline:
  value=fn()
  if value:return value
  time.sleep(.2)
 raise AssertionError('Public article timeout')
with tempfile.TemporaryDirectory(prefix='soulu-reader-public-',ignore_cleanup_errors=True) as root:
 env=dict(os.environ,LOCALAPPDATA=root,SOULU_UI_TEST_PORT=str(s.DEBUG_PORT))
 process=subprocess.Popen([sys.argv[1],'--no-proxy-server'],env=env)
 sockets=[]
 try:
  t=wait(lambda:next((t for t in s.targets() if '/ui/index.html' in t.get('url','')),None))
  shell=s.websocket.create_connection(t['webSocketDebuggerUrl'],timeout=30,origin=s.BASE);sockets.append(shell)
  wait(lambda:s.evaluate(shell,"typeof window.browserShell?.siteAction==='function'"))
  for permission in ('geolocation','camera','microphone','notifications','popups'):
   s.evaluate(shell,'browserShell.setSiteRule('+json.dumps({'domain':'','permission':permission,'value':2})+')')
  page=s.page_socket();sockets.append(page)
  def action(name,values=None):
   site=s.evaluate(shell,'browserShell.getCurrentSite()')
   payload={k:site[k] for k in ('tabId','url','generation')};payload.update(values or {})
   return s.evaluate(shell,'browserShell.siteAction('+json.dumps(name)+','+json.dumps(payload)+')')
  for index,(url,titlePart) in enumerate(articles):
   s.evaluate(shell,'browserShell.navigate('+json.dumps(url)+')')
   try:
    wait(lambda:s.evaluate(shell,'browserShell.getCurrentSite().then(s=>s.url.startsWith('+json.dumps(url.split('?')[0])+')&&!s.mainLoading)'),60)
   except Exception:
    print(json.dumps({'requested':url,'nativePage':s.evaluate(shell,'browserShell.getState().then(s=>s.page)')},ensure_ascii=False),flush=True)
    raise
   assert not s.evaluate(page,'location.href').startswith('chrome-error:'),url
   result=action('reader.enter');article=result['article']
   assert titlePart.lower() in article['title'].lower(),article['title']
   assert article['author'].lower() not in ('x','twitter','facebook','linkedin'), 'Social label used as author'
   assert len(article['content'])>1000,'Insufficient public article content'
   wait(lambda:s.evaluate(shell,"!document.querySelector('.reader-view').hidden&&document.querySelector('.reader-body').textContent.length>500"))
   structure=s.evaluate(shell,"({paragraphs:document.querySelectorAll('.reader-body p').length,links:document.querySelectorAll('.reader-body a[href]').length,images:document.querySelectorAll('.reader-body img').length,unsafe:!!document.querySelector('.reader-body script,.reader-body iframe,.reader-body form,.reader-body [onclick]')})")
   assert not structure['unsafe'] and structure['paragraphs']>2,structure
   if index==0 and structure['images']:
    s.evaluate(shell,"document.querySelector('.reader-body img').scrollIntoView()")
    wait(lambda:s.evaluate(shell,"[...document.querySelectorAll('.reader-body img')].some(i=>i.complete&&i.naturalWidth>0)"),30)
    s.evaluate(shell,"document.querySelector('.reader-view').scrollTop=0")
   for theme in ('light','sepia','dark'):
    action('reader.preferences',{'preferences':{'theme':theme}})
    wait(lambda:s.evaluate(shell,"document.querySelector('.reader-view').dataset.theme")==theme)
    picture=s.command(shell,'Page.captureScreenshot',{'format':'png'})['data']
    (out/f'article-{index}-{theme}.png').write_bytes(base64.b64decode(picture))
   rows.append({'url':url,'title':article['title'],'author':article['author'],'date':article['date'],'htmlCharacters':len(article['content']),**structure,'passed':True})
   (out/'public-reader.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
   print(json.dumps(rows[-1],ensure_ascii=False),flush=True)
   action('reader.exit');assert s.evaluate(page,'location.href')==result['url']
  # Reported sites continue to navigate in the same CEF content target.
  for host in ('youtube.com','facebook.com','sanderstripa.com','apps.sanderstripa.com','google.com','ozon.ru'):
   s.evaluate(shell,'browserShell.navigate('+json.dumps('https://'+host)+')')
   wait(lambda:s.evaluate(shell,'browserShell.getState().then(s=>!s.page.loading&&s.page.url.includes('+json.dumps(host)+'))'))
   loaded=s.evaluate(page,'({url:location.href,title:document.title})')
   assert host in loaded['url'] and not loaded['url'].startswith('chrome-error:'),loaded
   availability='site_error_page' if host=='ozon.ru' and 'нет соединения' in loaded['title'].lower() else 'loaded'
   rows.append({'site':host,'navigation':loaded,'routingPassed':True,'contentAvailability':availability,'authenticatedSessionTest':False})
  (out/'public-reader.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
  print(json.dumps(rows,ensure_ascii=False))
 finally:
  for ws in sockets:
   try:ws.close()
   except Exception:pass
  if process.poll() is None:
   try:s.close_normally(process)
   except Exception:process.kill()
