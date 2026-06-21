<div align="center">
  <img src="gui/assets/logo/256x256.png" width="150" alt="VNXKey Logo">
  <h1>VNXKey</h1>
  <p><b>Vietnamese Input Method Manager for Linux</b></p>
  <p><i>Hoạt động trên môi trường Wayland & X11 thông qua Evdev/Uinput</i></p>
</div>

---

## Giới thiệu

**VNXKey** là một bộ gõ tiếng Việt dành riêng cho hệ điều hành Linux. Thay vì sử dụng các framework bộ gõ phức tạp như IBus hay Fcitx (thường gặp lỗi trên Wayland hoặc các ứng dụng snap/flatpak), VNXKey can thiệp trực tiếp ở tầng Kernel (Evdev/Uinput) để xử lý phím. Nhờ đó, ứng dụng có thể gõ tiếng Việt mà không có phần gạch dưới như những bộ gõ khác trên **môi trường Linux**. Nhưng cũng có các hạn chế về tốc độ khi phải xử lý thông qua ảo hóa bàn phím.

Lõi xử lý tiếng Việt của VNXKey được kế thừa từ **Unikey Core** (do tác giả Phạm Kim Long phát triển).

## Tính năng nổi bật

- **Hiệu năng:** Chạy ngầm bằng C++ tối ưu hoá bộ nhớ, siêu nhẹ.
- **Tách biệt hoàn toàn (Decoupled Architecture):** Hệ thống chia làm 2 phần độc lập:
  - `daemon`: Service nền bằng C++, chạy bằng quyền root để ảo hoá bàn phím.
  - `gui`: Giao diện người dùng bằng Flutter, tuỳ chỉnh các thông số linh hoạt.
- **Tuỳ chỉnh phong phú:**
  - Hỗ trợ Telex / VNI.
  - Các bảng mã: Unicode, TCVN3, VNI Windows.
  - Sửa lỗi chính tả, tự động viết hoa sau dấu câu, cho phép dùng `f j w z` làm phụ âm.
  - Tắt tự động khi sử dụng layout bàn phím khác hệ US.
- **Kill-Switch (Dừng khẩn cấp):** Xử lý nhanh các tình huống phần mềm bị treo, lỗi, kẹt phím,....

---

## Cài đặt

VNXKey cung cấp sẵn các gói `.deb` thông qua hệ thống CI/CD (GitHub Actions). Để tải và cài đặt phiên bản mới nhất:

1. Truy cập trang **[Releases](../../releases/latest)** của Repository.
2. Tải về file `vnxkey_YY.MM.Build_Number_amd64.deb`.
3. Mở Terminal và chạy:

```bash
sudo dpkg -i vnxkey_*.deb
sudo apt-get install -f # Để cài đặt các dependencies (nếu thiếu)
```

**Khởi động ứng dụng:**
- **Daemon C++** sẽ tự động được đăng ký là một Systemd Service (`vnxkey.service`).
- Mở **VNXKey** từ Application Menu (App Drawer) của hệ thống. Bạn sẽ thấy biểu tượng chữ **V/E** xuất hiện dưới khay hệ thống (System Tray).

---

## Tài liệu kỹ thuật (Documentation)

- **[Hướng dẫn Build UI mới (Custom UI Guide)](docs/custom_ui_guide.md):** 
Nếu bạn muốn đóng góp hoặc thay thế giao diện Flutter hiện tại bằng một framework tối ưu khác (GTK, Qt, Rust Tauri...), hãy đọc tài liệu này. Daemon và GUI giao tiếp rất đơn giản thông qua file cấu hình JSON (`/var/tmp/vnxkey/config.json`).

---

## Build từ mã nguồn (Build from source)

**1. Yêu cầu hệ thống:**
- Ubuntu 22.04+ hoặc Debian 12+ (Kiến trúc `amd64`)
- `cmake`, `g++`, `libevdev-dev`, `pkg-config`, `dpkg-dev`
- **Flutter SDK**

**2. Các bước Build:**
```bash
# Clone repository
git clone https://github.com/kamedev02/vnxkey.git
cd vnxkey

# Chạy kịch bản tự động đóng gói
./scripts/build-release.sh
```
Sau khi tiến trình hoàn tất, file `.deb` sẽ nằm trong thư mục `dist/`.

---

## Đóng góp (Contributing)

Dự án tuân thủ chặt chẽ **Git Flow**. Các nhánh làm việc bao gồm:
- `main`: Chứa mã nguồn ổn định nhất.
- `release`: Nhánh dùng để trigger GitHub Actions tự động build bản cài đặt mới.
- `dev`: Nơi diễn ra các hoạt động phát triển chính.
- `features/*` / `fix/*`: Dành cho các tính năng mới hoặc vá lỗi.

Vui lòng tạo Pull Request vào nhánh `dev` hoặc `release`! 

---

## Giấy phép

- **Lõi xử lý tiếng Việt (Unikey Core):** Phát triển bởi tác giả Phạm Kim Long.
- **Linux Daemon (C++) & Flutter GUI:** Phát triển bởi [KameDev](https://github.com/kamedev02).

*Được xây dựng với ❤️ dành cho cộng đồng người dùng Linux Việt Nam.*
