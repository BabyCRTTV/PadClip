import ctypes, os, sys, subprocess, time, re, shutil, tempfile
from pathlib import Path
from zipfile import ZipFile
sys.path.insert(0,'tests')
from windows_update import UI, wait, dismiss
from ctypes import wintypes as w
from struct import pack_into,unpack_from
from tkinter import Tk,Canvas
def sample(proc):
    return list(map(float,subprocess.check_output(['powershell','-NoProfile','-Command',f'$p=Get-Process -Id {proc.pid}; $p.CPU; $p.WorkingSet64'],text=True).split()))
def label(hwnd):
    buf=ctypes.create_unicode_buffer(1024); UI.GetWindowTextW(hwnd,buf,1024); return buf.value
with tempfile.TemporaryDirectory() as d:
    with ZipFile('8BitTrade-PadClip-Windows.zip') as z:
        exe=z.extract('PadClip.exe',d)
        notes=z.read('release.txt').decode().splitlines()
    dm=ctypes.create_string_buffer(220);pack_into('H',dm,68,220)
    ok=UI.EnumDisplaySettingsW(None,-1,dm)
    print('Windows display mode:',unpack_from('II',dm,172),unpack_from('I',dm,184),'valid',ok,flush=True)
    p=subprocess.Popen([exe,'--updated'])
    try:
        dismiss("What's new in PadClip "+notes[0],notes[1])
        window=UI.FindWindowW('8BitTradePadClip',None)
        start=UI.GetDlgItem(window,3)
        a=sample(p); t=time.monotonic(); time.sleep(4); b=sample(p)
        print('Idle CPU, % of one logical core:',round(100*(b[0]-a[0])/(time.monotonic()-t),2),'working set MiB:',round(b[1]/1048576,1),flush=True)
        root=Tk(); root.geometry('640x480+0+0'); canvas=Canvas(root,width=640,height=480,bg='gray');canvas.pack()
        bar=canvas.create_rectangle(0,30,120,150,fill='blue');root.update()
        UI.PostMessageW(window,0x111,3,start)
        end=time.monotonic()+6; frame=0
        while time.monotonic()<end:
            x=frame*7%500;canvas.coords(bar,x,30,x+120,150);root.update();time.sleep(.014);frame+=1
        if label(start)=='Stop and save':
            UI.PostMessageW(window,0x111,3,start)
            wait(lambda:label(start)=='Start recording','save')
        root.destroy()
        child=None
        while child:=UI.FindWindowExW(window,child,'Static',None):
            print('Status:',label(child),flush=True)
        videos=Path(subprocess.check_output(['powershell','-NoProfile','-Command',"[Environment]::GetFolderPath('MyVideos')"],text=True).strip())/'PadClip'
        files=list(videos.glob('*.mp4'))
        for f in files:
            print('MP4',f.name,f.stat().st_size)
            if shutil.which('ffprobe'): subprocess.run(['ffprobe','-v','error','-select_streams','v:0','-show_entries','stream=width,height,r_frame_rate,avg_frame_rate,nb_frames,duration','-of','json',str(f)])
            if b'moov' in f.read_bytes():
                shutil.copyfile(f,'capture-sample.mp4')
    finally:
        if p.poll() is None:
            UI.PostMessageW(UI.FindWindowW('8BitTradePadClip',None),0x111,101,0);p.wait(timeout=10)
