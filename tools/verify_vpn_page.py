"""Verify the page compiled into the actual firmware matches its UTF-8 source."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--firmware',default='build_esp32c3/esp32_nat_router.bin')
args=p.parse_args()
root=Path(__file__).resolve().parents[1]
source=(root/'components/http_server/pages/vpn_profiles.html').read_text(encoding='utf-8').encode('utf-8')
firmware=(root/args.firmware).read_bytes()
assert source in firmware, 'Compiled firmware page differs from source or is incomplete'
print(f'Firmware embeds the complete matching UTF-8 VPN page ({len(source)} bytes).')
