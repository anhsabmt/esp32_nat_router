# ESP32-C3 SuperMini 4MB: nhiều profile WireGuard

Phiên bản `2.4.20-wgprofiles.2`, tùy biến từ upstream `master` tại commit `c44331dd475455242a8a8164ebca798774111de6`.

## Tính năng

- Lưu tối đa 8 profile. Import một hoặc nhiều file `.conf`, hoặc dán nội dung.
- Đặt tên, liệt kê, chọn, kết nối/chuyển, ngắt, sửa metadata, thay file cấu hình và xóa.
- Chỉ một tunnel. Chuyển profile dừng tunnel cũ và xóa bảng NAT cũ trước khi kết nối tunnel mới.
- Không reboot khi chuyển. Lựa chọn profile và trạng thái muốn kết nối được lưu qua reboot.
- Ngắt VPN giữ kill-switch. Chọn **Dùng router thường** mới cho phép Internet trực tiếp.
- Khóa private/preshared không được trả về trang web, API metadata hoặc log. Sửa metadata giữ nguyên khóa; muốn đổi khóa, import lại `.conf`.
- Giao diện tiếng Việt cho điện thoại; nút ít nhất 46px, cập nhật trạng thái 3 giây/lần.
- NAT, Wi-Fi AP/STA, firewall, DHCP, port mapping, OTA và các trang quản trị hiện có được giữ trong source.

Profile đang được chọn được bảo vệ khỏi sửa/xóa/thay thế. Trước khi sửa hoặc xóa, chọn profile khác; nếu cần, chuyển sang router thường sau khi đã ngắt thiết bị khách. Điều này tránh sửa khóa hoặc địa chỉ mà tunnel hiện tại đang tham chiếu.

## Nạp firmware lần đầu

Firmware đã được nạp và xác minh hash trên thiết bị của bạn: ESP32-C3 revision v0.4, flash XMC 4MB, USB Serial/JTAG. Boot, tunnel cũ, chuyển profile bằng bản sao tạm và giữ trạng thái ngắt qua reboot đã kiểm tra. Xem báo cáo để phân biệt phần còn chưa xác minh.

Tải các file trong `firmware_esp32c3/`. Lần chuyển từ firmware upstream cần nạp **partition table mới**, không chỉ OTA file app. NVS router cũ giữ nguyên offset; partition VPN riêng được thêm vào phần flash trống. Nếu chọn erase toàn flash, Wi-Fi, mật khẩu và profile sẽ mất. Giữ bản gốc các file `.conf` ở máy tính.

1. Cài Python và esptool: `python -m pip install esptool==4.12.0`.
2. Cắm board qua USB, xác định cổng COM. Nếu cần vào chế độ download, giữ BOOT, nhấn RESET, rồi thả BOOT.
3. Trong thư mục repo, thay `COM5` bằng cổng thật và chạy:

```powershell
python -m esptool --chip esp32c3 --port COM5 --baud 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB 0x0 firmware_esp32c3/bootloader.bin 0x8000 firmware_esp32c3/partition-table.bin 0xf000 firmware_esp32c3/ota_data_initial.bin 0x20000 firmware_esp32c3/esp32_nat_router.bin
```

Hoặc nạp file ghép đầy đủ ở offset **0x0**:

```powershell
python -m esptool --chip esp32c3 --port COM5 --baud 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB 0x0 firmware_esp32c3/esp32c3-wgprofiles-full.bin
```

File ghép có vùng đệm `0xff` giữa các phần; nó sẽ ghi đè vùng NVS router cũ nằm giữa bootloader và app. Dùng lệnh nạp **4 file riêng** nếu cần giữ NVS cũ. Sau khi firmware mới và partition đã chạy, cập nhật sau qua trang OTA bằng **`esp32_nat_router.bin`**, không dùng file ghép cho OTA.

Nếu nạp không ổn định, thử baud 115200 và cáp USB khác. Không tự thay GPIO anten/LED trên SuperMini nếu chưa có sơ đồ board.

`installer-c3.html` là trang cài Web Serial chỉ chọn C3. Có thể phục vụ bằng `python -m http.server 8000` rồi mở `http://localhost:8000/installer-c3.html` trong Chrome/Edge trên máy tính. Trang cần HTTPS hoặc localhost; GitHub Pages chưa được bật tự động. Trang installer upstream vẫn cài firmware upstream.

## Cấu hình và sử dụng

1. Kết nối AP `ESP32_NAT_Router`, mở `http://192.168.4.1`.
2. Cấu hình Wi-Fi upstream và mật khẩu AP; đặt mật khẩu quản trị trên trang chủ, đăng nhập.
3. Mở **WireGuard VPN**, import các `.conf`, đặt tên nếu muốn.
4. Bấm **Kết nối / chuyển** ở profile cần dùng. **Chọn, chưa kết nối** lưu lựa chọn nhưng chặn Internet.
5. Kiểm tra trạng thái **VPN đã kết nối**. Chờ SNTP/handshake nếu router vừa khởi động.
6. Sau khi đổi DNS/profile, ngắt và kết nối lại Wi-Fi trên các thiết bị khách để nhận DNS DHCP mới. Kết nối TCP đang chạy có thể bị ngắt khi chuyển VPN.

Cấu hình VPN đơn cũ được chuyển thành profile **Imported VPN** khi đủ trường hợp lệ (gồm IPv4 DNS và full tunnel), chưa có lựa chọn/profile mới và không cần xuất private key. VPN cũ bị tắt sẽ được lưu thành profile nhưng không tự bật. Nếu không đủ trường, hãy import file `.conf` gốc. Một lựa chọn profile đã lưu có ưu tiên cao hơn các tham số VPN legacy. Factory reset bằng BOOT hoặc console xóa cả profile mới.

Qua USB console có thể dùng `vpn_profile list`, `connect ID`, `select ID`, `disconnect`, `router`, `delete ID`, `clone ID NAME`. Các lệnh không in khóa bí mật. Clone phục vụ sao chép/sửa cấu hình và kiểm thử vòng đời; hai profile cùng cấu hình không chứng minh đã chuyển sang VPN server/IP public khác.

## Cấu hình được hỗ trợ

- Một `[Interface]`, một `[Peer]`, IPv4 full tunnel `AllowedIPs = 0.0.0.0/0`.
- Có thể kèm `::/0`, địa chỉ/DNS IPv6 trong file dual-stack; chỉ phần IPv4 được sử dụng. Build C3 tắt IPv6; bộ lọc profile cũng chặn frame IPv6.
- Bắt buộc PrivateKey, PublicKey, IPv4 Address, IPv4 DNS, Endpoint có port và AllowedIPs.
- PresharedKey tùy chọn; Keepalive 0–65535; MTU 1280–1420. Tối đa 4096 byte cho một file `.conf`, tên tối đa 48 byte UTF-8.
- IPv6 endpoint, split tunnel, nhiều peer, PostUp/PostDown, Table, ListenPort và directive chưa hỗ trợ bị từ chối rõ ràng.
- Tránh trùng subnet VPN/AP/upstream. Lưu lượng có route ra STA thay vì WireGuard sẽ bị chặn khi dùng profile, kể cả khi tunnel báo connected.

## Chính sách route, DNS và khóa

- Bộ lọc AP chạy trên TCP/IP thread cùng với vòng đời WireGuard. Frame ngắn/lỗi/IPv6 bị chặn. ARP, DHCP và truy cập quản trị AP vẫn hoạt động khi VPN ngắt.
- Internet của thiết bị khách chỉ được nhận khi peer có session, default route trỏ WireGuard và route cụ thể cũng chọn WireGuard. Nếu DHCP upstream tạm thay default route, traffic bị chặn cho đến khi tunnel được đặt lại làm default.
- DNS của profile được quảng bá cho thiết bị khách. DNS cũ do client cache vẫn chịu cùng chính sách route; không được tự chuyển trực tiếp ra upstream. Việc client nhận DNS mới cần renew DHCP/reconnect Wi-Fi.
- Wi-Fi upstream, SNTP và tra hostname endpoint là traffic điều khiển của **router**, có thể đi upstream ngoài tunnel để dựng VPN. Chính sách bảo vệ ở đây áp dụng cho traffic được chuyển tiếp từ **thiết bị khách AP**, không tuyên bố mọi gói do ESP32 tạo đều nằm trong tunnel.
- API yêu cầu mật khẩu quản trị/session. POST cần CSRF hợp lệ và header `X-VPN-Request: 1`. GET không thay đổi cấu hình.
- NVS riêng tránh tham gia bản export JSON router. Giữ file `.conf` gốc để khôi phục; backup router hiện có không chứa profile mới.
- Flash/NVS của build này chưa bật mã hóa flash/NVS hoặc Secure Boot. Khóa không lộ qua UI/API/log nhưng không được bảo vệ khỏi người có thể đọc flash vật lý. UI quản trị là HTTP; chỉ import trên mạng AP tin cậy có mật khẩu.

## Build và kiểm thử

Dùng **ESP-IDF v5.5.4**, target **esp32c3**. Thư viện WireGuard được vendor trong `components/esp_wireguard`, revision gốc `02a24fad29365db09e0088df2f8c4414632fe35a`; xem `LOCAL_CHANGES.md`. `dependencies.lock` khóa led_strip và mDNS. Chưa kiểm chứng PlatformIO hoặc các target khác.

```bash
python tools/embed_vpn_page.py
python tools/run_tests.py --cc gcc --cjson "$IDF_PATH/components/json/cJSON"
idf.py -B build_esp32c3 -D SDKCONFIG=sdkconfig.esp32c3 -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32c3' set-target esp32c3
idf.py -B build_esp32c3 -D SDKCONFIG=sdkconfig.esp32c3 build size
python tools/package_firmware.py --build build_esp32c3
```

Workflow **ESP32-C3 WireGuard profiles** tự build/test khi push `master`/`feature/**`, có thể chạy bằng **Run workflow**. Firmware mới tải từ artifact của lần build. Workflow không tự commit firmware vào repo.

Các test host chạy source C thật với adapter RTOS/lwIP/NVS/HTTP giả lập: parser, 8 profile, lưu/reload selection, dữ liệu hỏng/lỗi ghi; một tunnel, switch/stop/reconnect, hủy DNS pending, cleanup lỗi; auth/CSRF/header, metadata không chứa khóa, update giữ private key; IPv6/malformed/fragmented-DHCP policy.

Kiểm tra trình duyệt dùng dữ liệu thử, gồm kết nối/chuyển/ngắt/sửa ở 390px, không tràn ngang và không lỗi JavaScript. Không coi dữ liệu RAM/trạng thái mẫu trong giao diện thử là số đo thiết bị.

Chi tiết số đo build, SHA256 và phần chưa kiểm chứng: `BUILD_REPORT.md`, `firmware_esp32c3/SHA256SUMS.txt`.

## Kiểm tra cần làm trên thiết bị thật

- Nạp, reboot, giữ đủ 8 profile và trạng thái chọn/ngắt.
- Kết nối hai VPN thực; xác nhận IP public/DNS đổi đúng sau khi chuyển.
- Tạo traffic TCP/UDP/DNS từ client, rút Wi-Fi upstream và làm VPN server mất kết nối; bắt gói upstream để xác nhận không có traffic client đi trực tiếp.
- DHCP renew/reconnect upstream khi VPN đang chạy; IPv6 leak test từ client.
- Chuyển nhiều lần trong lúc DNS/SNTP/handshake đang chờ; theo dõi heap tối thiểu và stack của worker/web server.
- Kiểm tra NAT, firewall, DHCP, port mapping và OTA với nhu cầu thực. Nên bắt đầu ít client và tắt PCAP/MQTT/OLED/remote console chưa dùng trên C3.

Đã test USB trên thiết bị thật với tunnel có sẵn: 8 lần chuyển bằng profile clone cùng server, ping 3/3 trước/sau, giữ trạng thái chọn/ngắt qua reboot; sau đó khôi phục profile gốc và xóa clone. Mức heap thấp nhất quan sát trong bài test là 25,172 byte. Chưa kiểm chứng đổi IP/DNS giữa hai server khác nhau, web UI trên thiết bị thật, leak bằng packet capture từ client Wi-Fi, tải nhiều client, tốc độ hoặc OTA.
