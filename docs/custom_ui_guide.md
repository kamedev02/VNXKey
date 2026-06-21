# Hướng dẫn Phát triển / Thay thế Giao diện UI (Custom UI) cho VNXKey

VNXKey được thiết kế dựa trên triết lý **Tách biệt hoàn toàn (Decoupled)** giữa Backend (Core xử lý phím) và Frontend (Giao diện cấu hình). Hiện tại, giao diện mặc định đang được viết bằng **Flutter**. Tuy nhiên, nếu bạn muốn fork repo này để thay thế bằng một công nghệ khác (như GTK, Qt, Tauri, C++, Python, v.v.) nhằm tối ưu hoá **hiệu năng (performance)** hoặc **kích thước file (binary size)**, bạn hoàn toàn có thể làm được một cách rất dễ dàng.

Dưới đây là các tài liệu kỹ thuật để bạn phát triển một UI mới.

---

## 1. Cơ chế giao tiếp (IPC)

Backend Daemon (viết bằng C++, chạy bằng quyền root) và Frontend UI (chạy bằng quyền user thường) không giao tiếp qua Socket hay D-Bus phức tạp. Thay vào đó, chúng giao tiếp **Hoàn toàn qua một file JSON duy nhất**.

- **Đường dẫn file config:** `/var/tmp/vnxkey/config.json`
- **Cơ chế:** Daemon C++ sử dụng `inotify` (Linux kernel API) để lắng nghe sự thay đổi của file này. Mỗi khi UI của bạn ghi (save) nội dung mới vào file này, Daemon sẽ cập nhật trạng thái ngay lập tức (real-time) mà không cần restart.
- **Quyền truy cập:** Thư mục `/var/tmp/vnxkey/` được cấp quyền `0777`, nên bất kỳ UI nào chạy ở user space cũng có quyền Đọc/Ghi.

Do đó, **nhiệm vụ duy nhất của một ứng dụng UI mới** là:
1. Đọc nội dung từ `/var/tmp/vnxkey/config.json` lúc khởi động.
2. Cho phép người dùng tuỳ chỉnh.
3. Ghi đè lại nội dung vào file đó mỗi khi có thay đổi.

---

## 2. Cấu trúc dữ liệu JSON (`config.json`)

Định dạng file cấu hình là JSON. Dưới đây là các tham số (key) mà Daemon C++ hiểu và xử lý:

```json
{
  "enabled": true,
  "input_method": "telex",
  "charset": "unicode",
  "toggle_shortcut": "Ctrl+Shift",
  "emergency_stop": false,
  "allow_fjwz": true,
  "auto_cap": false,
  "standard_send_key": true,
  "spellcheck": false,
  "disable_non_us": false,
  "startup": false
}
```

### Giải thích các trường quan trọng (Daemon C++ parse):
- `enabled` *(boolean)*: Trạng thái bật/tắt gõ tiếng Việt hiện tại. (Có thể bị thay đổi bởi Daemon nếu người dùng bấm phím tắt chuyển E/V. UI nên liên tục polling hoặc dùng inotify để cập nhật lại giao diện Tray khi Daemon thay đổi field này).
- `input_method` *(string)*: Kiểu gõ. Các giá trị hợp lệ: `"telex"`, `"vni"`.
- `charset` *(string)*: Bảng mã đầu ra. Các giá trị hợp lệ: `"unicode"`, `"tcvn3"`, `"vni_win"`.
- `toggle_shortcut` *(string)*: Phím tắt chuyển đổi E/V. Hỗ trợ: `"Ctrl+Shift"`, `"Alt+Z"`, `"Ctrl+Space"`.
- `emergency_stop` *(boolean)*: Nút "Kill Switch". Khi giá trị này là `true`, C++ Daemon sẽ lập tức **nhả (ungrab) toàn bộ bàn phím vật lý**, dùng để thoát khỏi trạng thái kẹt phím hoặc lỗi engine. 

*(Các trường còn lại hiện tại mang tính chất lưu trữ cài đặt phía UI và có thể được tích hợp vào C++ daemon trong các bản nâng cấp sau).*

---

## 3. Tương tác với Systemd (Dịch vụ nền)

Backend Daemon chạy dưới dạng systemd service tên là `vnxkey.service`. 

Nếu UI của bạn có nút "Thoát hoàn toàn" (Quit & Stop Daemon), UI cần thực thi lệnh sau để tắt triệt để Daemon (yêu cầu quyền root, có thể sử dụng `pkexec` để hiện popup hỏi mật khẩu):

```bash
pkexec systemctl stop vnxkey
```

Nếu UI có nút tích hợp bật tính năng **"Khởi động cùng hệ thống"** (`startup`), bạn có thể gọi:
```bash
# Bật khởi động
pkexec systemctl enable vnxkey
# Tắt khởi động
pkexec systemctl disable vnxkey
```

---

## 4. Tích hợp System Tray

Một bộ gõ luôn cần System Tray. Do UI mới của bạn sẽ quản lý vòng đời của AppIndicator / Tray Icon, bạn cần theo dõi sự thay đổi của file `config.json` (do Daemon có thể đổi trạng thái `enabled` khi người dùng bấm phím tắt) để thay đổi Icon trên Khay hệ thống từ **V** sang **E** và ngược lại.

- Bạn có thể tham khảo logic File Polling mỗi 300ms hoặc dùng inotify-watch trên UI của bạn.
- Các file vector logo độ phân giải cao đã được chuẩn bị sẵn ở thư mục gốc: `gui/assets/logo/` và `gui/assets/icons/`.

---

## 5. Đóng gói (Packaging)

Khi bạn đã hoàn thành UI mới, hãy cập nhật script `scripts/build-release.sh`. 

1. Xoá hoặc comment lại bước **Bước 3: Build Flutter GUI**.
2. Thêm script build cho UI mới của bạn vào đó (ví dụ: `cargo build --release` cho Tauri, `cmake --build` cho Qt, v.v.).
3. Sao chép executable file của UI mới vào thư mục `/usr/lib/vnxkey/gui/` trong staging (`$STAGING_DIR`).
4. Hãy giữ lại Launcher bash script tại `/usr/bin/vnxkey-gui` trỏ đến executable của bạn để hệ thống tự động nhận diện phím tắt từ file `.desktop`.
5. Đảm bảo UI của bạn không vượt quá dung lượng mong muốn (Flutter thường chiếm ~25MB).

---
*Chúc bạn tạo ra một bản phân phối siêu nhẹ và siêu tốc cho VNXKey!*
