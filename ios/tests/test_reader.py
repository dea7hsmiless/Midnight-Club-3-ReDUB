"""Native ISO reader tests using a real supported XBE inside synthetic XDVDFS images."""
import ctypes, struct, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
import os, subprocess
WORK=Path(tempfile.gettempdir())/'redub-ios-reader-tests'
WORK.mkdir(exist_ok=True)
subprocess.run(['gcc','-shared','-O2','-I'+str(ROOT/'src'),'-I'+str(ROOT/'config/remix'),str(ROOT/'src/ios_xdvdfs.c'),'-o',str(WORK/'xdvdfs.dll')],check=True)
lib=ctypes.CDLL(str(WORK/'xdvdfs.dll'))
lib.ios_disc_extract.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_char_p,ctypes.c_size_t]
lib.ios_disc_verify.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_size_t]
xbe=(ROOT/'game-remix/default.xbe').read_bytes()
magic=b'MICROSOFT*XBOX*MEDIA'
def entry(name,sector,size,attr=0,left=0,right=0):return struct.pack('<HHIIBB',left,right,sector,size,attr,len(name))+name

def image(table=None,payload=xbe):
    data=bytearray(40*2048+len(payload));h=32*2048;data[h:h+20]=magic;data[h+2028:h+2048]=magic
    table=table if table is not None else entry(b'default.xbe',40,len(payload))
    struct.pack_into('<II',data,h+20,33,len(table));data[33*2048:33*2048+len(table)]=table;data[40*2048:]=payload
    return data
passed=0
with tempfile.TemporaryDirectory(dir=WORK) as temp:
    base=Path(temp)
    def check(name,data,expected,offset=0):
        global passed
        iso=base/(name+'.iso')
        with iso.open('wb') as f:f.seek(offset);f.write(data)
        dest=base/name;dest.mkdir();err=ctypes.create_string_buffer(512)
        result=lib.ios_disc_extract(str(iso).encode(),str(dest).encode(),None,None,err,512)
        assert bool(result)==expected,(name,result,err.value)
        if expected:assert (dest/'default.xbe').read_bytes()==xbe
        print('PASS',name,err.value.decode());passed+=1
    check('supported',image(),True)
    check('uppercase',image(entry(b'DEFAULT.XBE',40,len(xbe))),True)
    check('full-disc-offset-2080000',image(),True,0x2080000)
    check('full-disc-offset-fd90000',image(),True,0xfd90000)
    check('scanned-nonstandard-offset',image(),True,0x3456000)
    check('bad-magic',bytes(80*2048),False)
    check('truncated',image()[:40*2048+30],False)
    check('parent-traversal',image(entry(b'../outside',40,len(xbe))),False)
    check('embedded-nul',image(entry(b'default.xbe\x00evil',40,len(xbe))),False)
    check('unsupported-xbe',image(payload=b'XBEH-not-supported'),False)
    check('child-cycle',image(entry(b'default.xbe',40,len(xbe),left=1)),False)
    check('directory-cycle',image(entry(b'loop',33,18,attr=0x10)),False)
    check('outside-image',image(entry(b'default.xbe',0xffffffff,len(xbe))),False)
    # A valid nested directory and a zero-byte regular file must extract correctly.
    root=entry(b'default.xbe',40,len(xbe),right=8)+bytes(7)+entry(b'assets',34,19,attr=0x10)
    data=image(root);child=entry(b'empty',39,0);data[34*2048:34*2048+len(child)]=child
    check('nested-directory',data,True)
    assert (base/'nested-directory/assets/empty').exists()
    root=entry(b'default.xbe',40,len(xbe),right=8)+bytes(7)+entry(b'default.xbe',40,len(xbe))
    check('duplicate-file',image(root),False)
    check('invalid-child-offset',image(entry(b'default.xbe',40,len(xbe),right=65535)),False)
print(passed,'tests passed')
