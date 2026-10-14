"""Package ONLY a successful matching build, with offsets from flasher_args.json."""
import argparse, hashlib, json, shutil, subprocess, sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--build',default='build_esp32c3');args=p.parse_args()
root=Path(__file__).resolve().parents[1];build=root/args.build;dest=root/'firmware_esp32c3'
meta=json.loads((build/'flasher_args.json').read_text())
if meta['extra_esptool_args']['chip']!='esp32c3':raise SystemExit('Not an ESP32-C3 build')
dest.mkdir(exist_ok=True)
parts=[]
for offset,relative in meta['flash_files'].items():
    source=build/relative
    if not source.is_file():raise SystemExit(f'Missing build file: {relative}')
    shutil.copy2(source,dest/source.name);parts.extend([offset,str(dest/source.name)])
subprocess.run([sys.executable,'-m','esptool','--chip','esp32c3','merge_bin','--flash_mode','dio','--flash_freq','80m','--flash_size','4MB','-o',str(dest/'esp32c3-wgprofiles-full.bin'),*parts],check=True)
checksums={file.name:hashlib.sha256(file.read_bytes()).hexdigest() for file in sorted(dest.glob('*.bin'))}
(dest/'SHA256SUMS.txt').write_text(''.join(f'{digest}  {file}\n' for file,digest in checksums.items()),encoding='utf-8')
(dest/'flash-layout.json').write_text(json.dumps(dict(chip='esp32c3',flash_size='4MB',flash_files={offset:Path(relative).name for offset,relative in meta['flash_files'].items()},sha256=checksums),indent=2)+'\n',encoding='utf-8')
print(f'Packaged firmware: {dest}')
