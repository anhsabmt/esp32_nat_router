"""USB test with an existing working profile. Creates/deletes only its own clone.
No private key is imported, echoed, returned or saved by this script.
The AP clients lose connectivity during switch/disconnect/reboot tests.
"""
import argparse,json,re,time
from pathlib import Path
import serial
p=argparse.ArgumentParser();p.add_argument('--port',required=True);p.add_argument('--profile',type=int,required=True);p.add_argument('--cycles',type=int,default=6);p.add_argument('--result',required=True)
args=p.parse_args();result={'profile':args.profile,'switches':[],'pings':[]};clone=None
def read_for(port,seconds):
    end=time.monotonic()+seconds;data=b''
    while time.monotonic()<end:data+=port.read(max(1,port.in_waiting))
    text=re.sub(r'\x1b\[[0-9;?]*[a-zA-Z]','',data.decode('utf-8',errors='replace'))
    if re.search(r'Guru Meditation|assert failed|abort\(\)|panic',text,re.I):raise RuntimeError('Device panic detected')
    return text
def command(port,text,seconds=1):
    port.write((text+'\r\n').encode());return read_for(port,seconds)
def selected(text):
    m=re.search(r'Selected profile: (-?\d+), state: ([a-z_]+), protected: (yes|no)',text)
    if not m:raise RuntimeError('No profile console response')
    return {'id':int(m[1]),'state':m[2],'protected':m[3]=='yes'}
def heap(port):
    m=re.search(r'Current heap size: (\d+) \(minimal: (\d+)\)',command(port,'heap'))
    if not m:raise RuntimeError('No heap response')
    return {'free':int(m[1]),'minimum':int(m[2])}
def connect(port,ident):
    response=command(port,f'vpn_profile connect {ident}',3)
    if 'Profile operation: OK' not in response:raise RuntimeError('Connect command failed')
    for _ in range(10):
        status=selected(command(port,'vpn_profile list'))
        if status['id']==ident and status['state']=='connected':return status
        time.sleep(1)
    raise RuntimeError('VPN did not establish a session')
def ping(port):
    text=command(port,'ping -c 3 -W 1500 1.1.1.1',5)
    m=re.search(r'(\d+) packets transmitted, (\d+) received',text)
    if not m:raise RuntimeError('No ping summary')
    return {'sent':int(m[1]),'received':int(m[2])}
port=serial.Serial();port.port=args.port;port.baudrate=115200;port.timeout=.1
port.dtr=False;port.rts=False;port.open()
with port:
    read_for(port,2);command(port,'')
    initial=selected(command(port,'vpn_profile list'))
    if initial['id']!=args.profile or initial['state']!='connected':raise RuntimeError('Requested profile must already be selected and connected')
    result['initial']=initial;result['heap_before']=heap(port)
    try:
        text=command(port,f'vpn_profile clone {args.profile} _Codex_HW_Test')
        m=re.search(r'Cloned profile ID: (\d+)',text)
        if not m:raise RuntimeError('No free slot for temporary test profile')
        clone=int(m[1]);result['clone_id']=clone
        result['pings'].append(ping(port));print('Initial tunnel ping completed',flush=True)
        for cycle in range(args.cycles):
            ident=clone if cycle%2==0 else args.profile
            status=connect(port,ident);memory=heap(port)
            result['switches'].append({'id':ident,'state':status['state'],'heap':memory})
            print(f'Switch {cycle+1}/{args.cycles}: connected; heap {memory["free"]}',flush=True)
        connect(port,clone);command(port,'vpn_profile disconnect')
        stopped=selected(command(port,'vpn_profile list'))
        if stopped['state']!='disconnected' or not stopped['protected']:raise RuntimeError('Disconnect did not preserve protection')
        result['disconnected']=stopped
        command(port,'restart',9);command(port,'')
        rebooted=selected(command(port,'vpn_profile list'))
        if rebooted['id']!=clone or rebooted['state']!='disconnected' or not rebooted['protected']:raise RuntimeError('Reboot did not preserve selected/disconnected state')
        result['reboot_disconnected']=rebooted;print('Reboot persistence confirmed',flush=True)
        connect(port,args.profile);result['pings'].append(ping(port))
    finally:
        connect(port,args.profile)
        if clone is not None:
            text=command(port,f'vpn_profile delete {clone}')
            if 'Profile operation: OK' not in text:raise RuntimeError('Temporary profile cleanup failed')
        result['final']=selected(command(port,'vpn_profile list'));result['heap_after']=heap(port)
        Path(args.result).write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps({'status':'completed','switches':len(result['switches']),'pings':result['pings'],'heap_before':result['heap_before'],'heap_after':result['heap_after']}),flush=True)
