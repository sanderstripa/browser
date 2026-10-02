"""Verify transparent exports and actual Windows executable icon resources."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
from io import BytesIO
import json
from pathlib import Path
import struct
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SIZES = {16, 20, 24, 32, 40, 48, 64, 128, 256}

def frames(data):
    reserved, kind, count = struct.unpack_from('<HHH', data)
    assert (reserved, kind) == (0, 1), 'Invalid ICO header'
    result = {}
    for i in range(count):
        w, h, _, _, planes, bits, length, offset = struct.unpack_from('<BBBBHHII', data, 6 + 16*i)
        size = w or 256
        assert size == (h or 256) and planes == 1 and bits == 32
        assert offset + length <= len(data), 'Truncated ICO'
        result[size] = data[offset:offset+length]
    assert set(result) == SIZES, f'Missing system sizes: {set(result)}'
    return result

def verify_assets():
    manifest = json.loads((ROOT/'ui/branding/manifest.json').read_text())
    expected = (ROOT/'ui/soulu-icon.ico').read_bytes()
    assert hashlib.sha256(expected).hexdigest() == manifest['ico_sha256']
    for alias in manifest['aliases']:
        data = (ROOT/'ui'/alias).read_bytes()
        if alias.endswith('.ico'):
            assert data == expected, f'Stale icon alias: {alias}'
        else:
            assert data == (ROOT/'ui/branding/soulu-1024.png').read_bytes(), f'Stale PNG alias: {alias}'
    for size, data in frames(expected).items():
        im = Image.open(BytesIO(data)).convert('RGBA')
        assert im.size == (size, size)
        assert im.tobytes() == Image.open(ROOT/f'ui/branding/soulu-{size}.png').convert('RGBA').tobytes()
        alpha = im.getchannel('A')
        x0, y0, x1, y1 = alpha.getbbox()
        assert x0 >= 1 and y0 >= 1 and x1 < size and y1 < size, f'Unsafe edge at {size}'
        assert (x1-x0)/size >= (.85 if size <= 24 else .90), f'Symbol too small at {size}'
        assert im.getpixel((0,0))[3] == 0
        # A white/black perimeter stroke or colored specks is never intentional.
        assert not any(a > 32 and max(r,g,b) < 20 for r,g,b,a in im.getdata()), 'Black pixels'
        assert not any(a > 32 and min(r,g,b) > 245 for r,g,b,a in im.getdata()), 'White pixels'
    assert not (ROOT/'ui/soulu-icon.jpg').exists(), 'Opaque legacy logo remains'
    svg = (ROOT/'ui/branding/soulu-master.svg').read_text()
    assert '<image' not in svg and 'stroke=' not in svg and '<filter' not in svg
    return expected

def verify_binary(path, expected):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
    kernel.LoadLibraryExW.restype = wintypes.HMODULE
    kernel.FindResourceW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p]
    kernel.FindResourceW.restype = ctypes.c_void_p
    kernel.SizeofResource.argtypes = [wintypes.HMODULE, ctypes.c_void_p]
    kernel.SizeofResource.restype = wintypes.DWORD
    kernel.LoadResource.argtypes = [wintypes.HMODULE, ctypes.c_void_p]
    kernel.LoadResource.restype = ctypes.c_void_p
    kernel.LockResource.argtypes = [ctypes.c_void_p]
    kernel.LockResource.restype = ctypes.c_void_p
    kernel.FreeLibrary.argtypes = [wintypes.HMODULE]
    module = kernel.LoadLibraryExW(str(Path(path).resolve()), None, 0x22)
    assert module, f'Cannot load resources: {path}'
    def resource(name, kind):
        found = kernel.FindResourceW(module, name, kind)
        assert found, f'Missing resource {kind}:{name} in {path}'
        loaded = kernel.LoadResource(module, found)
        return ctypes.string_at(kernel.LockResource(loaded), kernel.SizeofResource(module, found))
    groups = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ssize_t)
    @callback_type
    def callback(mod, kind, name, param):
        groups.append(name)
        return True
    kernel.EnumResourceNamesW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, callback_type, ctypes.c_ssize_t]
    try:
        assert kernel.EnumResourceNamesW(module, 14, callback, 0) and groups, f'No executable icon: {path}'
        expected_frames = frames(expected)
        # Check every icon group so a stale fallback cannot remain embedded.
        for group in groups:
            raw = resource(group, 14)
            _, kind, count = struct.unpack_from('<HHH', raw)
            assert kind == 1
            observed = set()
            for i in range(count):
                w,h,colors,reserved,planes,bits,length,icon_id = struct.unpack_from('<BBBBHHIH', raw, 6+14*i)
                size = w or 256
                data = resource(icon_id, 3)
                assert len(data) == length
                observed.add(size)
                # Resource compiler / NSIS may preserve PNG or convert to DIB.
                entry = struct.pack('<BBBBHHII',w,h,colors,reserved,planes,bits,len(data),22)
                ico = struct.pack('<HHH',0,1,1)+entry+data
                actual = Image.open(BytesIO(ico)).convert('RGBA')
                target = Image.open(BytesIO(expected_frames[size])).convert('RGBA')
                assert actual.size == target.size and actual.tobytes() == target.tobytes(), f'Stale resource {size}px in {path}'
            assert observed == SIZES, f'Incomplete executable icon sizes in {path}: {observed}'
    finally:
        kernel.FreeLibrary(module)


def verify_hicon(handle, expected, label, shortcut_overlay=False):
    """Read the icon Windows actually supplied, including its native alpha."""
    from PIL import ImageChops
    user = ctypes.WinDLL('user32', use_last_error=True)
    gdi = ctypes.WinDLL('gdi32', use_last_error=True)
    class IconInfo(ctypes.Structure):
        _fields_ = [('icon',wintypes.BOOL),('x',wintypes.DWORD),('y',wintypes.DWORD),
                    ('mask',wintypes.HBITMAP),('color',wintypes.HBITMAP)]
    class Bitmap(ctypes.Structure):
        _fields_ = [('kind',wintypes.LONG),('width',wintypes.LONG),('height',wintypes.LONG),
                    ('stride',wintypes.LONG),('planes',wintypes.WORD),('bits',wintypes.WORD),
                    ('data',ctypes.c_void_p)]
    class Header(ctypes.Structure):
        _fields_ = [('size',wintypes.DWORD),('width',wintypes.LONG),('height',wintypes.LONG),
                    ('planes',wintypes.WORD),('bits',wintypes.WORD),('compression',wintypes.DWORD),
                    ('bytes',wintypes.DWORD),('x',wintypes.LONG),('y',wintypes.LONG),
                    ('used',wintypes.DWORD),('important',wintypes.DWORD)]
    user.GetIconInfo.argtypes = [wintypes.HICON,ctypes.POINTER(IconInfo)]
    user.GetDC.argtypes = [wintypes.HWND]
    user.GetDC.restype = wintypes.HDC
    user.ReleaseDC.argtypes = [wintypes.HWND,wintypes.HDC]
    gdi.GetObjectW.argtypes = [wintypes.HANDLE,ctypes.c_int,ctypes.c_void_p]
    gdi.GetDIBits.argtypes = [wintypes.HDC,wintypes.HBITMAP,wintypes.UINT,wintypes.UINT,
                             ctypes.c_void_p,ctypes.c_void_p,wintypes.UINT]
    gdi.DeleteObject.argtypes = [wintypes.HANDLE]
    info = IconInfo()
    assert handle and user.GetIconInfo(handle,ctypes.byref(info)), f'No native icon: {label}'
    dc = user.GetDC(None)
    try:
        bitmap = Bitmap()
        assert info.color and gdi.GetObjectW(info.color,ctypes.sizeof(bitmap),ctypes.byref(bitmap))
        assert bitmap.width == bitmap.height and bitmap.width in SIZES, f'Wrong shell size: {label}'
        size = bitmap.width
        header = Header(ctypes.sizeof(Header),size,-size,1,32,0,size*size*4,0,0,0,0)
        pixels = ctypes.create_string_buffer(size*size*4)
        assert gdi.GetDIBits(dc,info.color,0,size,pixels,ctypes.byref(header),0) == size
        actual = Image.frombytes('RGBA',(size,size),pixels.raw,'raw','BGRA')
        target = Image.open(BytesIO(frames(expected)[size])).convert('RGBA')
        # Windows decoders can expose straight or premultiplied color channels.
        # Accept only the same pixels, allowing at most two rounding levels.
        differences = []
        for reference in (target,Image.frombytes('RGBA',target.size,target.convert('RGBa').tobytes())):
            difference = ImageChops.difference(actual,reference)
            if shortcut_overlay:
                # Explorer itself adds the Windows shortcut arrow in this
                # quadrant. Verify the mark outside that system-owned overlay.
                difference.paste((0,0,0,0),(0,size//2,(size+1)//2,size))
            differences.append(difference.getextrema())
        assert any(max(high for low,high in diff) <= 2 for diff in differences), f'Stale shell/window icon: {label}'
    finally:
        user.ReleaseDC(None,dc)
        if info.mask: gdi.DeleteObject(info.mask)
        if info.color: gdi.DeleteObject(info.color)

def verify_shortcut(path, expected):
    shell = ctypes.WinDLL('shell32',use_last_error=True)
    user = ctypes.WinDLL('user32',use_last_error=True)
    class ShellInfo(ctypes.Structure):
        _fields_ = [('icon',wintypes.HICON),('index',ctypes.c_int),('attributes',wintypes.DWORD),
                    ('display',wintypes.WCHAR*260),('kind',wintypes.WCHAR*80)]
    shell.SHGetFileInfoW.argtypes = [wintypes.LPCWSTR,wintypes.DWORD,ctypes.POINTER(ShellInfo),
                                     wintypes.UINT,wintypes.UINT]
    shell.SHGetFileInfoW.restype = ctypes.c_size_t
    user.DestroyIcon.argtypes = [wintypes.HICON]
    for flag in (0,1):  # Explorer's large and small shell presentations.
        info = ShellInfo()
        assert shell.SHGetFileInfoW(str(Path(path).resolve()),0,ctypes.byref(info),ctypes.sizeof(info),0x100|flag)
        try: verify_hicon(info.icon,expected,str(path),shortcut_overlay=True)
        finally: user.DestroyIcon(info.icon)

def verify_window(handle, expected):
    user = ctypes.WinDLL('user32',use_last_error=True)
    user.SendMessageW.argtypes = [wintypes.HWND,wintypes.UINT,ctypes.c_size_t,ctypes.c_ssize_t]
    user.SendMessageW.restype = ctypes.c_ssize_t
    user.GetClassLongPtrW.argtypes = [wintypes.HWND,ctypes.c_int]
    user.GetClassLongPtrW.restype = ctypes.c_size_t
    for kind,index in ((1,-14),(2,-34)):
        icon = user.SendMessageW(handle,0x7f,kind,0) or user.GetClassLongPtrW(handle,index)
        verify_hicon(icon,expected,f'window {handle}, icon {kind}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', action='append', default=[])
    parser.add_argument('--shortcut', action='append', default=[])
    parser.add_argument('--window', type=int)
    args = parser.parse_args()
    expected = verify_assets()
    for path in args.binary:
        verify_binary(path, expected)
    for path in args.shortcut:
        verify_shortcut(path, expected)
    if args.window:
        verify_window(args.window, expected)
    print(json.dumps({'passed': True, 'sizes': sorted(SIZES), 'binaries': args.binary,
                      'shortcuts': args.shortcut, 'window': args.window}))

