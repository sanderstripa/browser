"""Repository and bundled-face acceptance checks; no system font installation."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from fontTools.ttLib import TTFont

ROOT=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(ROOT/'scripts/generate-typography.py'),'--check'],check=True)
tokens=json.loads((ROOT/'typography.json').read_text())
manifest=json.loads((ROOT/'ui/fonts/manifest.json').read_text())
assert set(v[2] for v in tokens.values())=={400,500,600}
sample='Ёё Йй Жж Щщ Ыы Дд Лл Aa Gg Ii Ll Oo 0123456789 @ / : ; ( ) [ ] — + % «»– https://example.com/path?q=test 127.0.0.1:17890'
report={'tokens':tokens,'fonts':[], 'exceptions':[], 'checks':[]}
for weight,name in ((400,'Regular'),(500,'Medium'),(600,'SemiBold')):
    path=ROOT/f'ui/fonts/Onest-{name}.ttf';font=TTFont(path)
    assert font['OS/2'].usWeightClass==weight
    assert manifest['metrics']=={'unitsPerEm':font['head'].unitsPerEm,'ascent':font['hhea'].ascent,'descent':-font['hhea'].descent}
    assert hashlib.sha256(path.read_bytes()).hexdigest()==next(f['sha256'] for f in manifest['files'] if f['weight']==weight)
    assert 'Onest' in font['name'].getDebugName(1)
    assert not (set(map(ord,sample))-set(font.getBestCmap())),f'Missing glyph: {name}'
    report['fonts'].append({'file':str(path.relative_to(ROOT)),'weight':weight,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'glyphs':len(font.getBestCmap())})
license=(ROOT/'ui/fonts/OFL.txt').read_text(encoding='utf-8')
assert 'SIL OPEN FONT LICENSE' in license and 'Onest' in license
for html in (ROOT/'ui').rglob('*.html'):
    text=html.read_text(encoding='utf-8')
    if 'Content-Security-Policy' in text:assert "font-src 'self'" in text,html
    assert 'typography.css' in text and 'typography.js' in text,html
    assert text.count('as="font"')==3,html
    assert 'https://' not in ''.join(re.findall(r'<link[^>]*(?:as="font"|stylesheet)[^>]*>',text)),html
for css in (ROOT/'ui').rglob('*.css'):
    if css.name=='typography.css':continue
    for m in re.finditer(r'([^{}]+)\{([^{}]*)\}',css.read_text(encoding='utf-8')):
        selector,body=m.groups();selector=re.sub(r'/\*.*?\*/','',selector,flags=re.S).strip()
        values=re.findall(r'(?<![-\w])(?:font(?:-family|-size|-weight|-style)?|line-height|letter-spacing)\s*:[^;}]+',body)
        if not values:continue
        if 'reader-article' in selector or selector in ('.reader-meta','.reader-deck'):
            report['exceptions'].append({'file':str(css.relative_to(ROOT)),'selector':selector,'reason':'User-selected Reader article content','values':values});continue
        for value in values:
            assert 'var(--type-' in value or 'var(--glyph-' in value or value=='line-height:1',f'Unexplained typography: {css} {selector} {value}'
            for token in re.findall(r'--type-([\w]+)',value):assert token in tokens
        assert not re.search(r'Segoe|Inter|Arial|Montserrat|Roboto|Tahoma|Manrope',body),css
for p in (ROOT/'ui').rglob('*.js'):
    if p.parent.name=='third_party':continue
    assert not re.search(r'font-family=|fontSize\s*=|fontFamily\s*=',p.read_text(encoding='utf-8')),p
native=(ROOT/'installer/setup.cpp').read_text(encoding='utf-8')
assert 'PrivateFontCollection' in native and 'AddMemoryFont' in native
assert 'FontStyleBold' not in re.sub(r'//[^\n]*','',native)
assert not re.search(r'Text\(g,[^\n]*?,\d+,RectF',native)
build=(ROOT/'installer/build.ps1').read_text(encoding='utf-8')
for name in ('Regular','Medium','SemiBold'):assert f'Onest-{name}.ttf' in build
assert not (ROOT/'ui/vpn/assets/Montserrat.ttf').exists()
dialogs=(ROOT/'cef/soulu/typography_native.cc').read_text(encoding='utf-8')
assert 'AddFontMemResourceEx' in dialogs and 'RemoveFontMemResourceEx' in dialogs
assert 'L"Onest Medium"' in dialogs and 'L"Onest SemiBold"' in dialogs
assert 'typography::onestAscent' in dialogs and 'TA_BASELINE' in dialogs
for source in ('browser_window.cc','browser_client.cc'):
    assert 'MessageBoxW(' not in (ROOT/'cef/soulu'/source).read_text(encoding='utf-8')
report['checks']=['generated outputs current','actual 400/500/600','RU/EN/URL/symbol glyph coverage','bundled OFL attribution','every internal HTML preloads local fonts','all UI CSS uses semantic tokens','Reader article exceptions explicit','no JS-generated legacy text','private native faces without synthetic bold','installer embeds all faces']
if len(sys.argv)>1:Path(sys.argv[1]).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('PASS:', '; '.join(report['checks']))
