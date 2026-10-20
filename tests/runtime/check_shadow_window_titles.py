#!/usr/bin/env python3
"""Observe real hidden Win32 gameplay/launcher captions with a debug port."""
import argparse,ctypes as C,json,os,shutil,socket,subprocess,time
from ctypes import wintypes as W
from pathlib import Path
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for k in ('exe','rom','out'):ap.add_argument('--'+k,required=True,type=lambda v:Path(v).resolve())
    a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=False);results=[];user=C.windll.user32
    user.GetWindowTextLengthW.argtypes=[W.HWND];user.GetWindowTextW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
    user.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
    callback_type=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    for launcher in (False,True):
        case=a.out/('launcher' if launcher else 'gameplay');rt=case/'runtime';rt.mkdir(parents=True)
        for f in (a.exe,a.exe.parent/'SDL2.dll'):shutil.copy2(f,rt/f.name)
        (rt/'settings.ini').write_text('[video]\nfullscreen=0\n[audio]\nvolume=0\n')
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        env={k:v for k,v in os.environ.items() if not k.startswith(('SDL_','LNG_','GENESIS_','TRILOGY_'))}
        env.update(SDL_VIDEODRIVER='windows',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',LNG_TEST_HIDDEN='1',LNG_SCRIPT='wait:6000;quit')
        found=[]
        with (case/'run.log').open('w') as log:
            proc=subprocess.Popen([str(rt/a.exe.name),str(a.rom),'--launcher' if launcher else '--no-launcher','--port',str(port)],
                cwd=rt,env=env,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                def collect(hwnd,param):
                    pid=W.DWORD();user.GetWindowThreadProcessId(hwnd,C.byref(pid))
                    if pid.value==proc.pid:
                        n=user.GetWindowTextLengthW(hwnd);buf=C.create_unicode_buffer(n+1);user.GetWindowTextW(hwnd,buf,n+1)
                        if buf.value:found.append(buf.value)
                    return True
                callback=callback_type(collect);end=time.monotonic()+30
                while time.monotonic()<end and proc.poll() is None:
                    found.clear();user.EnumWindows(callback,0)
                    if 'Sonic 3 & Knuckles' in found:break
                    time.sleep(.05)
                result=dict(launcher=launcher,port=port,captions=found.copy(),passed='Sonic 3 & Knuckles' in found and
                    all(not any(t in title for t in ('RECOMPILED','native','interpreter','Launcher',str(port))) for title in found))
                results.append(result);print(json.dumps(result),flush=True)
            finally:
                if proc.poll() is None:proc.terminate()
                proc.wait(timeout=15)
    (a.out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    return int(any(not r['passed'] for r in results))
if __name__=='__main__':raise SystemExit(main())
