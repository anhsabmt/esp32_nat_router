# Build and verification report

Date: 2026-10-14 (Asia/Bangkok).

## VPN UI repair and device restore

The user reported a stuck Loading state and rejected import/router actions. This was reproduced on the physical ESP32: no admin password was set, so the profile API returned 403 while the status label remained Loading. Revision .3 redirects unauthenticated VPN entry to password setup/login, displays expired-session/error states, bounds requests to 8 seconds, serializes polling/mutations, adds retry/startup-error feedback, and uses FileReader for imports. No API authentication bypass was added.

A temporary password was created for testing, then the real HTTP server was exercised with mobile WebKit: two-file import, list/status, metadata edit, delete, select/disconnect, explicit router mode, reconnect of the original tunnel and session-expiry feedback all passed. The complete original 2.4.20 flash snapshot was restored afterward, preserving the newly configured upstream Wi-Fi. Afterward the user requested installing the new revision: .3 was flashed again and the real-device web suite passed again. The device currently runs .3 with Imported VPN connected. The temporary test password was removed so the user can set their own password. Temporary credentials/test profiles were removed by restore. See docs/VPN_UI_REPAIR_VI.md and firmware_esp32c3/web-ui-hardware-result.json.

The final .3 USB smoke test passed eight same-server profile switches, 3/3 ICMP before/after, and selected/disconnected/protected state across reboot; the clone was deleted. Free heap was 62,228 bytes before and 65,140 after. The minimum observed before the smoke-test reboot was **4,500 bytes**, following the web tests; after reboot the minimum was 47,048. This small margin is an observed RAM constraint. No OOM/panic was observed, but sustained-load stability and the exact source of the peak allocation have not been established. See firmware_esp32c3/hardware-smoke-wgprofiles3.json.

## Source and target

- Upstream: `martin-ger/esp32_nat_router`, `master`, `c44331dd475455242a8a8164ebca798774111de6`.
- Other discovered upstream branches: `WT32-ETH01`, `esp32_wifi_repeater`. The customization uses the NAT-router branch.
- ESP-IDF: v5.5.4, revision `735507283d5b2f9fb363a1901172dbd9e847945d`.
- WireGuard: vendored `02a24fad29365db09e0088df2f8c4414632fe35a`, with local lifecycle/secret cleanup fixes described in `components/esp_wireguard/LOCAL_CHANGES.md`.
- Target: ESP32-C3, 160MHz, DIO 80MHz flash, 4MB, USB Serial/JTAG console. No PSRAM requirement.
- Firmware version: `2.4.20-wgprofiles.3`.
- C3 configuration explicitly disables IPv6 and enables lwIP thread-safety checks. Original AP NAT enable calls now use the ESP-IDF esp_netif_napt_enable API, avoiding timeout initialization from the wrong thread.
- Managed dependencies locked: led_strip 3.1.0~1, mDNS 1.14.0.

## Repository architecture assessment

Upstream is ESP-IDF C, with an existing single global WireGuard context and optional NAT VPN uplink. The previous import parser wrote individual `vpn_*` parameters to the router NVS namespace, then the web endpoint rebooted. The old VPN form sent credentials through GET query parameters and logged the query. This fork removes that form/endpoint from the web surface and replaces it with authenticated POST JSON operations.

Profiles are fixed-size versioned blobs in a dedicated 64KiB NVS partition; selection and desired connection state are one versioned blob. A stored profile is 372 bytes. Only one active profile is resident; list/API metadata copies have private/preshared keys wiped. A single 4KiB worker, a controller mutex, TCP/IP callbacks and generation cancellation serialize lifecycle operations. Secrets do not appear in metadata responses or logs. A stored selected profile remains protected on disconnect, errors, reboot and uplink loss; normal routing requires the explicit router action.

## Successful local build

- App `.bin`: **1,482,752 bytes** (1448.00 KiB).
- Each OTA app slot: **1,572,864 bytes** (1536 KiB).
- Remaining app-slot space: **90,112 bytes**, **5.73%**.
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
- Revision .3 WebKit regressions: mobile import without File.text/replaceChildren, auth failure, invalid JSON, stalled GET/POST, retry, serialized requests, startup script error.
- Compiled binary page matches the complete UTF-8 HTML source.
- `git diff --check` and generated page consistency checked before upload.

## Earlier lifecycle tests and remaining limits

A physical ESP32-C3 revision v0.4 with embedded XMC 4MB flash was identified on USB and flashed with hash verification. Boot completed without panic. Existing Wi-Fi and VPN credentials were retained; a complete legacy configuration was migrated to Imported VPN without exporting its keys. The USB hardware smoke test switched eight times between the original profile and a temporary clone of the same server configuration. All switches reached connected; ICMP checks received 3/3 before and after. Selected/disconnected/protected state survived restart. The original profile was restored and the temporary clone deleted. Measured free heap was 62,352 bytes before the smoke sequence and 60,284 afterward; the minimum during the sequence/restart was 25,172 bytes. CLI history and transient allocations affect these numbers; long-duration heap stability is not proven. See hardware-smoke-result.json.

The earlier USB lifecycle tests above ran on revision .2. Revision .3 real-device web testing now passes through STA/LAN with desktop WebKit. iPhone Safari through the AP, AP-client DNS/public-IP switching between different servers, upstream packet capture/leak checks, multiple Wi-Fi clients, OTA, throughput, and NAT/firewall/portmap regression tests remain unverified. The same-server clone proves profile lifecycle, not a change of exit location. The device was restored to original 2.4.20 after the first revision .3 test, then updated to .3 again at the user request and left connected on the original VPN.

The GitHub workflow is provided for future reproducible builds; its remote execution status is reported separately. Other ESP32 targets, PlatformIO and GitHub Pages deployment have not been tested/enabled.

Firmware hashes and exact flash offsets: `firmware_esp32c3/SHA256SUMS.txt` and `flash-layout.json`. Setup and device acceptance checklist: `docs/WIREGUARD_PROFILES_VI.md`.
