# Sửa lỗi trang VPN và khôi phục bản gốc

Ngày 14/10/2026. Bản sửa: `2.4.20-wgprofiles.3`.

## Lỗi đã tái hiện

Trên ESP32 thật, chưa có mật khẩu quản trị. Trang `/vpn` vẫn mở nhưng API trả HTTP 403; trạng thái vẫn ghi “Đang tải”. Thêm profile và nút router thường cũng bị API từ chối. Test qua console trước đó không phát hiện luồng web này.

## Đã sửa

- Vào VPN khi chưa tạo mật khẩu/chưa đăng nhập: chuyển tới trang Router để tạo mật khẩu/đăng nhập. Giữ nguyên bảo vệ API; không bỏ yêu cầu đăng nhập.
- Phiên hết hạn: hiện “Cần đăng nhập quản trị”, không giữ trạng thái “Đang tải”.
- Yêu cầu có thời hạn 8 giây; lỗi kết nối có hướng dẫn và nút tải lại. Thao tác hết thời gian chờ cần kiểm tra trạng thái trước khi thử lại vì server có thể đã xử lý.
- Xếp hàng yêu cầu đọc/ghi để tránh polling chồng lên thao tác trên HTTP server nhỏ của ESP32.
- Đọc file bằng FileReader, không phụ thuộc File.text; có thông báo khi JavaScript không khởi chạy. Trang được gửi theo từng đoạn 1KiB.

## Kiểm tra đã chạy

- WebKit ở kích thước điện thoại 390px, truy cập HTTP server thật của ESP32 qua mạng LAN: tạo mật khẩu thử, đăng nhập, đọc trạng thái/profile, import hai file .conf, sửa/xóa, chọn/ngắt, chuyển sang router thường và kết nối lại tunnel gốc. Tất cả đạt; không lỗi JavaScript, không tràn ngang, API không trả khóa bí mật.
- WebKit fixture: phiên hết hạn, JSON lỗi, GET/POST treo, thử lại, JavaScript lỗi; import khi File.text/replaceChildren không có; xác nhận yêu cầu không chồng nhau.
- Toàn bộ test C parser/NVS/controller/API/packet/migration đạt. Build C3 4MB đạt; kiểm tra HTML trong binary khớp nguyên vẹn với source.

Sau test, đã nạp lại toàn bộ bản sao firmware gốc **2.4.20**, giữ Wi-Fi upstream mới do người dùng cấu hình. Kiểm tra boot, VPN gốc peer up và trang quản trị trả HTTP 200. Mật khẩu thử và profile thử không còn trên thiết bị. Sau đó, theo yêu cầu cập nhật mới của người dùng, đã nạp lại bản .3 và test web lần nữa. Thiết bị hiện chạy .3, tunnel gốc Imported VPN đã kết nối. Mật khẩu thử đã gỡ; người dùng tạo mật khẩu quản trị riêng tại trang Router.

Chưa kiểm chứng trực tiếp Safari trên iPhone qua Wi-Fi AP; WebKit desktop không thay thế hoàn toàn phép thử đó. IP/DNS khi chuyển giữa hai VPN thật và leak từ client AP bằng packet capture vẫn chưa được kiểm chứng.

## Kiểm tra sau lần cập nhật cuối

Tám lần chuyển giữa profile gốc và clone cùng server đều kết nối; ping 3/3 trước và sau. Chọn/ngắt/kill-switch giữ qua reboot, clone thử được xóa, tunnel gốc được chọn và kết nối. RAM trống trước/sau: 62.228 / 65.140 byte. Heap minimum trước reboot trong lượt kiểm tra này: **4.500 byte** (đã chạy test web); sau reboot: 47.048 byte. Mức thấp 4,5KB là giới hạn thực tế cần theo dõi dưới tải; không có panic/thiếu bộ nhớ được ghi nhận trong các thao tác đã test, nhưng không coi đây là thử tải dài hạn. Chưa chứng minh nguồn chính xác của đỉnh sử dụng RAM. Tránh bật tính năng phụ không cần thiết trên C3.
