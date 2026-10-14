# Build and verification report

Date: 2026-10-14 (Asia/Bangkok).

## Source and target

- Upstream: `martin-ger/esp32_nat_router`, `master`, `c44331dd475455242a8a8164ebca798774111de6`.
- Other discovered upstream branches: `WT32-ETH01`, `esp32_wifi_repeater`. The customization uses the NAT-router branch.
- ESP-IDF: v5.5.4, revision `735507283d5b2f9fb363a1901172dbd9e847945d`.
- WireGuard: vendored `02a24fad29365db09e0088df2f8c4414632fe35a`, with local lifecycle/secret cleanup fixes described in `components/esp_wireguard/LOCAL_CHANGES.md`.
- Target: ESP32-C3, 160MHz, DIO 80MHz flash, 4MB, USB Serial/JTAG console. No PSRAM requirement.
- Firmware version: `2.4.20-wgprofiles.1`.
- C3 configuration explicitly disables IPv6 and enables lwIP thread-safety checks.
- Managed dependencies locked: led_strip 3.1.0~1, mDNS 1.14.0.

## Repository architecture assessment

Upstream is ESP-IDF C, with an existing single global WireGuard context and optional NAT VPN uplink. The previous import parser wrote individual `vpn_*` parameters to the router NVS namespace, then the web endpoint rebooted. The old VPN form sent credentials through GET query parameters and logged the query. This fork removes that form/endpoint from the web surface and replaces it with authenticated POST JSON operations.

Profiles are fixed-size versioned blobs in a dedicated 64KiB NVS partition; selection and desired connection state are one versioned blob. A stored profile is 372 bytes. Only one active profile is resident; list/API metadata copies have private/preshared keys wiped. A single 4KiB worker, a controller mutex, TCP/IP callbacks and generation cancellation serialize lifecycle operations. Secrets do not appear in metadata responses or logs. A stored selected profile remains protected on disconnect, errors, reboot and uplink loss; normal routing requires the explicit router action.

## Successful local build

- App `.bin`: **1,478,352 bytes** (1443.70 KiB).
- Each OTA app slot: **1,572,864 bytes** (1536 KiB).
- Remaining app-slot space: **94,512 bytes**, **6.01%**.
- Bootloader: 17,552 bytes, within the 0x8000 partition-table offset.
- The ESP-IDF partition/app size check passed.
- Static memory from final `idf.py size`: shared C3 DRAM/IRAM **141876 bytes** used of **321296**, **179420 bytes** unallocated statically. This is **not runtime free heap**; task/Wi-Fi/lwIP/crypto allocations occur at runtime. See `firmware_esp32c3/size.txt`.
- Existing upstream compiler warnings remain in OLED unused helpers and WireGuard x25519/NaCl code; no warning originated in the new profile parser/controller/API in the successful build. The upstream x25519 warning was already suppressed from error status by its component build settings; cryptographic behavior is not claimed validated here.

## Flash layout

| Partition | Offset | Size | End |
|---|---:|---:|---:|
| Router NVS | 0x9000 | 24KiB | 0xf000 |
| OTA data | 0xf000 | 8KiB | 0x11000 |
| PHY | 0x11000 | 4KiB | 0x12000 |
| OTA 0 | 0x20000 | 1536KiB | 0x1a0000 |
| OTA 1 | 0x1a0000 | 1536KiB | 0x320000 |
| Existing FAT storage | 0x320000 | 320KiB | 0x370000 |
| VPN NVS | 0x370000 | 64KiB | 0x380000 |

Highest partition end 0x380000 is below 4MiB (0x400000); 512KiB remains unpartitioned. Old router NVS/FAT/OTA offsets are unchanged. The initial upgrade must update the partition table; an app-only OTA from upstream cannot create this partition.

## Tests actually run locally

- Host tests compiled and executed with TinyCC 0.9.27 Windows, using the actual C parser/storage/controller/API and upstream cJSON, with adapters for hardware-dependent APIs.
- Parser/NVS suite: 34 checks, including valid/dual-stack import, unsupported directives/peers/routes, missing/invalid keys, bad port/address/MTU, eight blobs, reload selection, disconnected persistence, injected storage write failure, corruption and deletion.
- Controller suite: start/switch/stop, at most one tunnel, metadata redaction, active edit/delete guard, route restoration after upstream route replacement, pending-DNS cancellation, partial-connect failure cleanup, uplink reconnect, explicit router mode, fail-closed storage error.
- HTTP suite: password/session requirement, CSRF/custom header, fractional/invalid IDs, fragmented body reads, metadata without private/preshared fields, active delete/edit rejection, stop/router semantics, key-preserving edit, malformed JSON and error messages without a secret canary.
- Packet-policy suite: IPv6/malformed frame rejection, ARP/AP access, DHCP when disconnected, fragments cannot use the DHCP exception, other destinations require routing/peer validation.
- Browser UI fixture: connect, switch, disconnect, edit/save at 390px; no horizontal overflow, 50px observed button heights, no JavaScript errors. The fixture is not the firmware backend or a real VPN session.
- `git diff --check` and generated page consistency checked before upload.

## Not verified

No ESP32 was flashed. No physical ESP32-C3 SuperMini or live WireGuard server was available. Real handshakes, reboot persistence, DNS renewal on real clients, sustained switching/heap behavior, NAT/firewall/portmap regressions, OTA, throughput, and leak prevention using upstream packet capture remain hardware acceptance tests. Code and host tests implement fail-closed AP forwarding; they do not prove an end-to-end absence of leaks on hardware.

The GitHub workflow is provided for future reproducible builds; its remote execution status is reported separately. Other ESP32 targets, PlatformIO and GitHub Pages deployment have not been tested/enabled.

Firmware hashes and exact flash offsets: `firmware_esp32c3/SHA256SUMS.txt` and `flash-layout.json`. Setup and device acceptance checklist: `docs/WIREGUARD_PROFILES_VI.md`.
