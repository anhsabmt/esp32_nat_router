"""Run platform-independent C tests with cc/gcc/clang (or Windows TinyCC)."""
import argparse, os, subprocess
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument('--cc',default=os.environ.get('CC','cc')); p.add_argument('--cjson',required=True)
args=p.parse_args(); root=Path(__file__).resolve().parents[1]; build=root/'build_host'; build.mkdir(exist_ok=True)
common=['-Itests/stubs','-Iinclude','-Icomponents/esp_wireguard/include']
sources=['tests/mock_nvs.c','main/vpn_profile_parser.c','main/vpn_profiles.c']
cjson=Path(args.cjson).resolve()
for test in ['test_profiles','test_controller','test_api','test_packet_policy']:
    output=build/(test+('.exe' if os.name=='nt' else ''))
    extra=[str(cjson/'cJSON.c'),'-I'+str(cjson)] if test=='test_api' else []
    subprocess.run([args.cc,*common,'-o',str(output),f'tests/{test}.c',*sources,*extra],cwd=root,check=True)
    subprocess.run([str(output)],cwd=root,check=True)
