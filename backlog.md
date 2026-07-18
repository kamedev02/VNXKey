# VNXKey — Backlog & Known Issues

> Tài liệu này tổng hợp toàn bộ **lỗi tiềm ẩn, thiếu sót, và cải tiến cần làm** sau khi review kỹ toàn bộ codebase.
> Cập nhật lần cuối: 2026-07-18

---

## 🔴 Mức Nghiêm Trọng — Có Thể Gây Lỗi Trực Tiếp Cho Người Dùng

### 1. CapsLock + Shift cùng lúc chưa được xử lý đúng
- **File:** `daemon/src/main.cpp:479`, `daemon/src/engine/viet_engine.cpp:80`
- **Mô tả:** Hiện tại `keycode_to_char()` dùng biến `shift_pressed` để quyết định chữ hoa/thường, rồi truyền `is_capslock_on` riêng cho Unikey. Nhưng khi **CapsLock BẬT + Shift đè** (để gõ chữ thường), ký tự `ch` truyền vào engine đã là chữ HOA (do `shift_pressed=true`), trong khi thực tế người dùng muốn chữ thường. Unikey nhận `CapsLockOn=1` nhưng `ch` đã là uppercase → kết quả sai.
- **Tác động:** Gõ sai chữ khi CapsLock bật + giữ Shift.
- **Giải pháp:** Cần XOR logic: `effective_upper = shift_pressed ^ capslock_on`, rồi truyền `ch` đã tính toán đúng cho engine, và set `UnikeySetCapsState(0, 0)` vì ta đã xử lý casing xong rồi.

### 2. Đọc `/dev/shm/vnxkey_excluded` mỗi phím bấm — nghẽn I/O
- **File:** `daemon/src/main.cpp:488-496`
- **Mô tả:** Mỗi khi người dùng bấm một phím, daemon mở file `/dev/shm/vnxkey_excluded`, đọc nội dung, rồi đóng. Dù `/dev/shm` là RAM-backed (tmpfs), việc `open() → read() → close()` mỗi keystroke vẫn tốn ~2-5μs syscall overhead. Khi gõ nhanh (>120 WPM), overhead này cộng dồn.
- **Tác động:** Tăng độ trễ input, đặc biệt khi gõ nhanh.
- **Giải pháp:** Cache giá trị excluded, chỉ đọc lại khi nhận thông báo từ inotify hoặc timer 500ms (đã có sẵn ở GUI side).

### 3. `physical_pressed_keys` không được đồng bộ khi Emergency Stop
- **File:** `daemon/src/main.cpp:320-337`
- **Mô tả:** Khi Emergency Stop kích hoạt, daemon ungrab tất cả bàn phím và discard pending events. Nhưng `physical_pressed_keys` set và `shift_pressed`/`ctrl_pressed`/`alt_pressed` booleans **không được reset**. Khi Emergency Stop tắt và re-grab, daemon vẫn tưởng Shift/Ctrl đang bị đè → gõ bị lệch.
- **Tác động:** Sau khi bật/tắt Emergency Stop, modifier keys bị "kẹt" trong trạng thái cũ.
- **Giải pháp:** Reset toàn bộ `physical_pressed_keys.clear()`, `shift_pressed = ctrl_pressed = alt_pressed = false`, và `engine.reset()` khi kích hoạt Emergency Stop.

### 4. Key Repeat (value=2) bị xử lý như Key Press
- **File:** `daemon/src/main.cpp:397`
- **Mô tả:** Khi người dùng giữ phím (repeat), evdev gửi events với `value=2`. Code hiện tại insert keycode vào `physical_pressed_keys` và chạy toàn bộ logic xử lý tiếng Việt giống như phím press mới. Với mỗi repeat event, engine lại xử lý thêm → có thể sinh ra ký tự lặp không mong muốn.
- **Tác động:** Giữ phím có thể gây lặp ký tự tiếng Việt sai hoặc văng hex code.
- **Giải pháp:** Phân biệt `value==1` (press) và `value==2` (repeat). Với repeat: nếu ký tự ASCII thường thì passthrough trực tiếp, không đẩy vào engine.

### 5. Surrogate pairs (Emoji, CJK Extension B) không được xử lý
- **File:** `daemon/src/engine/viet_engine.cpp:8-23`
- **Mô tả:** Hàm `utf16_to_utf8()` xử lý từng `uint16_t` riêng lẻ mà **không kiểm tra surrogate pairs** (U+10000 trở lên cần 2 uint16_t liên tiếp). Nếu Unikey bao giờ sinh ra ký tự ngoài BMP, hàm này sẽ tạo UTF-8 sai.
- **Tác động:** Thấp (tiếng Việt nằm trong BMP), nhưng nếu mở rộng hỗ trợ emoji hoặc ký tự đặc biệt thì sẽ crash hoặc sinh ký tự rác.
- **Giải pháp:** Thêm kiểm tra `0xD800-0xDBFF` (high surrogate) và ghép với `0xDC00-0xDFFF` (low surrogate).

---

## 🟠 Mức Trung Bình — Ảnh Hưởng Trải Nghiệm Nhưng Không Crash

### 6. `keycode_to_char()` thiếu nhiều phím dấu câu
- **File:** `daemon/src/main.cpp:131-178`
- **Mô tả:** Hàm chỉ map được `SPACE`, `ENTER`, `TAB`, `COMMA`, `DOT`, và chữ/số. Thiếu hoàn toàn: `KEY_SEMICOLON`, `KEY_APOSTROPHE`, `KEY_SLASH`, `KEY_BACKSLASH`, `KEY_LEFTBRACE`, `KEY_RIGHTBRACE`, `KEY_MINUS`, `KEY_EQUAL`, `KEY_GRAVE`. Khi gõ các phím này, `ch=0` → engine không nhận → buffer engine không flush → từ tiếng Việt trước đó không được commit.
- **Tác động:** Gõ dấu câu (`;` `'` `/` `-` `=` `` ` ``) không trigger flush buffer, gây ra ký tự lạ khi gõ tiếp.
- **Giải pháp:** Mở rộng `keycode_to_char()` cho tất cả phím trên bàn phím US standard, hoặc passthrough + `engine.reset()` cho các phím non-alpha.

### 7. Daemon write_config() chỉ update `enabled`, bỏ qua các field khác
- **File:** `daemon/src/ipc/config_watcher.cpp:165-200`
- **Mô tả:** Khi daemon toggle E/V qua shortcut, nó gọi `write_config()` nhưng hàm này chỉ regex-replace field `"enabled"` trong file JSON. Nếu GUI vừa thay đổi `input_method` hay `toggle_shortcut`, daemon sẽ giữ nguyên các giá trị đó. Tuy nhiên, nếu file config chưa tồn tại hoặc bị corrupt, nó tạo file mới **thiếu `excluded_apps`** → GUI mất danh sách excluded apps.
- **Tác động:** Mất cấu hình `excluded_apps` khi daemon tạo lại file config.
- **Giải pháp:** Daemon nên đọc toàn bộ config, chỉ thay đổi field `enabled`, rồi ghi lại toàn bộ. Hoặc dùng JSON library (nlohmann/json đã có trong CMake).

### 8. GUI poller spawn `Isolate.run()` mỗi 500ms — tốn tài nguyên
- **File:** `gui/lib/main.dart:90-116`
- **Mô tả:** Mỗi 500ms, GUI spawn 1 Dart Isolate mới để chạy shell script `bash -c` (gọi `gdbus`). Mỗi lần spawn isolate + fork process bash tốn ~5-10ms CPU. Dù đã có `_isPolling` flag, Isolate overhead vẫn cao.
- **Tác động:** Tiêu tốn CPU và gây lag giao diện trên máy cấu hình thấp.
- **Giải pháp:** Chuyển sang persistent Isolate hoặc dùng FFI/method channel gọi trực tiếp D-Bus API thay vì fork bash mỗi 500ms.

### 9. Không có cơ chế "flush on non-Vietnamese key"
- **File:** `daemon/src/main.cpp:465-503`
- **Mô tả:** Khi gõ phím không phải chữ cái (F1, Home, Escape, mũi tên, Tab...), engine chỉ passthrough mà **không gọi `engine.reset()`** (trừ Backspace). Buffer engine vẫn giữ nguyên → gõ tiếp có thể bị ảnh hưởng bởi buffer cũ.
- **Tác động:** Sau khi bấm Home/End/Mũi tên rồi gõ tiếp, engine xử lý sai vì buffer không khớp với văn bản thực tế trên màn hình.
- **Giải pháp:** Gọi `engine.reset()` cho tất cả non-printable keys (F keys, navigation keys, etc.).

### 10. Config watcher trên daemon không parse `excluded_apps`
- **File:** `daemon/src/ipc/config_watcher.cpp:93-139`
- **Mô tả:** Daemon parse JSON bằng hand-rolled `json_get_value()` chỉ hỗ trợ flat values. Field `excluded_apps` là một **JSON array**, hoàn toàn không được parse ở daemon side. Hiện tại excluded apps chỉ hoạt động nhờ GUI ghi `/dev/shm/vnxkey_excluded`.
- **Tác động:** Nếu GUI crash hoặc không chạy, excluded apps không hoạt động. Daemon không thể tự quyết định excluded apps.
- **Giải pháp:** Thêm JSON array parser hoặc dùng nlohmann/json (đã có trong CMakeLists).

### 11. Systemd service dùng `Environment=VNXKEY_CONFIG=/home/%u/...` sai
- **File:** `daemon/systemd/vnxkey.service:45`
- **Mô tả:** `%u` trong systemd là **tên user chạy service**, ở đây là `root` (vì `User=root`). Nên biến môi trường sẽ expand thành `/home/root/.config/vnxkey/config.json`. Nhưng config thực tế nằm ở `/var/tmp/vnxkey/config.json`.
- **Tác động:** Biến môi trường `VNXKEY_CONFIG` trỏ sai đường dẫn (nhưng daemon không dùng biến này nên không gây lỗi trực tiếp, chỉ gây nhầm lẫn khi debug).
- **Giải pháp:** Sửa thành `Environment=VNXKEY_CONFIG=/var/tmp/vnxkey/config.json` hoặc xóa bỏ nếu không cần.

---

## 🟡 Mức Thấp — Cải Tiến Chất Lượng & Trải Nghiệm

### 12. Không hỗ trợ bàn phím layout khác US QWERTY
- **File:** `daemon/src/main.cpp:131-178`
- **Mô tả:** `keycode_to_char()` hardcode US QWERTY layout. Nếu người dùng dùng layout AZERTY (Pháp), Dvorak, Colemak... thì mapping keycode→char bị sai hoàn toàn.
- **Tác động:** Bộ gõ không hoạt động trên bàn phím non-US.
- **Giải pháp:** Đọc XKB keymap từ hệ thống hoặc cho user config layout.

### 13. Thiếu unit test cho CapsLock + engine integration
- **File:** `daemon/tests/engine_test.cpp`
- **Mô tả:** Engine test hiện tại chỉ test `process_key(char)` với `is_capslock_on=false` (default). Không có test nào kiểm tra CapsLock=ON ảnh hưởng output Unikey ra sao.
- **Tác động:** Không phát hiện regression khi sửa code CapsLock.
- **Giải pháp:** Thêm test cases: `process_key('a', true)` phải ra uppercase `Ă` thay vì `ă`.

### 14. Thiếu unit test cho `emit_unicode` với CapsLock shift inversion
- **File:** `daemon/src/output/uinput_handler.cpp:358-380`
- **Mô tả:** Logic `shift = !shift` khi CapsLock ON chỉ áp dụng cho `KEY_A..KEY_Z` nhưng không có test nào verify. Nếu ai đó sửa code và vô tình thay đổi range check, lỗi sẽ không bị phát hiện.
- **Giải pháp:** Thêm unit test cho `basic_ascii_to_keycode` + capslock inversion logic.

### 15. Tray icon path là relative — không hoạt động ngoài working directory
- **File:** `gui/lib/main.dart:146, 164`
- **Mô tả:** `iconPath` dùng `'assets/icons/V_128x128.png'` (relative path). Nếu GUI được launch từ một working directory khác (ví dụ qua `.desktop` file), icon không tìm thấy.
- **Tác động:** Tray icon bị mất khi launch từ Application Menu.
- **Giải pháp:** Dùng absolute path hoặc resolve relative path từ executable location.

### 16. `is_duplicate()` dùng 15ms threshold — có thể bỏ qua event hợp lệ
- **File:** `daemon/src/main.cpp:192-204`
- **Mô tả:** Nếu 2 bàn phím vật lý khác nhau gửi cùng keycode trong 15ms (ví dụ USB + Bluetooth), event thứ hai bị bỏ qua. Nhưng nếu người dùng gõ cực nhanh cùng phím (double tap) < 15ms, event cũng bị nuốt.
- **Tác động:** Mất phím khi gõ double-tap rất nhanh.
- **Giải pháp:** Thêm `device_id` vào dedup key để chỉ dedup trong cùng device.

### 17. `epoll` được tạo rồi close ngay — lãng phí
- **File:** `daemon/src/main.cpp:300-307`
- **Mô tả:** Code tạo `epoll_create1()` rồi `close(epfd)` ngay lập tức, sau đó dùng busy-polling loop. Comment nói "Phase 4 sẽ refactor" nhưng vẫn chưa làm.
- **Tác động:** CPU usage cao hơn cần thiết (~1-5% idle).
- **Giải pháp:** Implement epoll-based event loop để giảm CPU usage xuống ~0%.

### 18. Thiếu log rotation / log level control
- **File:** Toàn bộ daemon code dùng `std::cout` / `std::cerr`
- **Mô tả:** Daemon log mọi thứ ra stdout/stderr. Khi chạy lâu dài qua systemd, journal sẽ lớn dần. Không có cách tắt debug log mà không recompile.
- **Giải pháp:** Thêm log level (ERROR/WARN/INFO/DEBUG) và cho phép config qua file hoặc env var.

### 19. Update service download `.deb` vào `/tmp` — không an toàn
- **File:** `gui/lib/update_service.dart:82`
- **Mô tả:** File update được download vào `/tmp/vnxkey_update.deb`. Trên hệ thống multi-user, `/tmp` là world-writable. Attacker có thể race condition thay file `.deb` bằng malicious package trước khi `pkexec dpkg -i` chạy.
- **Tác động:** Lỗ hổng bảo mật (privilege escalation via TOCTOU).
- **Giải pháp:** Dùng `mktemp -d` để tạo thư mục riêng với permission 0700, hoặc verify checksum/signature trước khi install.

### 20. Config file ở `/var/tmp/vnxkey/` với permission 0777 — không an toàn
- **File:** `daemon/src/ipc/config_watcher.cpp:44-47`
- **Mô tả:** Thư mục config có permission `0777` (all users can read/write). Bất kỳ user nào trên hệ thống đều có thể thay đổi config, bao gồm cả `emergency_stop=true` để vô hiệu hóa bàn phím người khác.
- **Tác động:** Lỗ hổng bảo mật trên hệ thống multi-user.
- **Giải pháp:** Dùng `0755` cho directory và tạo group `vnxkey` riêng.

---

## 🔵 Tính Năng Cần Bổ Sung (Feature Requests)

### 21. VNI input method chưa được test
- **Mô tả:** GUI cho phép chọn VNI, daemon gọi `UnikeySetInputMethod(UkVni)`, nhưng không có unit test nào cho VNI mode.
- **Giải pháp:** Thêm test cases cho VNI: `a1` → `á`, `a2` → `à`, `a6` → `â`...

### 22. Các option UI chưa được implement ở daemon
- **Mô tả:** GUI hiển thị nhiều toggle switch nhưng daemon chưa implement:
  - `allow_fjwz`: Cho phép f, j, w, z làm phụ âm
  - `auto_cap`: Tự động viết hoa sau dấu câu
  - `spellcheck`: Kiểm tra chính tả
  - `disable_non_us`: Tắt khi layout khác US
  - `standard_send_key`: Phương thức gửi phím chuẩn
  - `charset`: TCVN3 / VNI Windows (chỉ hỗ trợ Unicode)
- **Giải pháp:** Implement từng tính năng hoặc disable/ghi chú "Coming soon" trên UI.

### 23. Thiếu hỗ trợ cho Electron/Chromium apps (VSCode, Slack, Discord)
- **Mô tả:** Ctrl+Shift+U là shortcut mở Developer Tools trong Electron apps. Khi daemon gõ Unicode sequence, nó sẽ vô tình mở DevTools thay vì nhập ký tự.
- **Giải pháp:** Detect Electron apps (qua WM_CLASS) và dùng phương pháp output khác (XDoTool, `ydotool`, hoặc IBus relay).

### 24. Thiếu hỗ trợ cho Qt/KDE apps
- **Mô tả:** Ctrl+Shift+U Unicode input chỉ hoạt động trong GTK apps. Qt apps (Kate, KDE apps) dùng cơ chế nhập Unicode khác hoặc không hỗ trợ.
- **Giải pháp:** Detect Qt apps và dùng `xdotool type` hoặc clipboard-based input.

### 25. Không có khả năng auto-detect display server
- **Mô tả:** `window_poller.dart` thử nhiều phương pháp (hyprctl, qdbus, gdbus, xprop) nhưng daemon không biết đang chạy trên X11 hay Wayland để chọn output method tối ưu.
- **Giải pháp:** Daemon nên check `$XDG_SESSION_TYPE` hoặc `$WAYLAND_DISPLAY` để tự chọn strategy.

### 26. Chưa hỗ trợ Macro (từ viết tắt)
- **Mô tả:** Unikey core có sẵn `CMacroTable` nhưng chưa được expose qua VietEngine/GUI.
- **Giải pháp:** Thêm UI để người dùng define macros (ví dụ: `k` → `không`, `tqm` → `tôi quan mến`).

---

## 🧪 Test Coverage Gaps

| Module | Có test | Cần thêm |
|--------|---------|----------|
| `viet_engine.cpp` (Telex) | ✅ 32 tests | CapsLock, Shift+CapsLock, word boundary |
| `viet_engine.cpp` (VNI) | ❌ | Full VNI test suite |
| `basic_ascii_to_keycode` | ✅ 37 tests | CapsLock inversion |
| `keycode_to_char` | ❌ | Full keycode mapping |
| `emit_unicode` | ❌ | Cần mock uinput |
| `emit_backspace` | ❌ | Count=0, count=negative |
| `config_watcher` parse_json | ❌ | Edge cases, malformed JSON |
| `is_duplicate` | ❌ | Timing edge cases |
| GUI config_service | ❌ | Read/write/watch cycle |

---

## 📋 Ưu Tiên Sửa Theo Thứ Tự

1. **#3** — Reset modifier state khi Emergency Stop (dễ sửa, impact cao)
2. **#4** — Xử lý key repeat đúng cách (dễ sửa, hay gặp)
3. **#9** — Flush engine khi gõ navigation keys (dễ sửa, hay gặp)
4. **#6** — Mở rộng `keycode_to_char()` (trung bình, hay gặp)
5. **#1** — CapsLock + Shift logic (trung bình, edge case)
6. **#2** — Cache excluded state (tối ưu hiệu năng)
7. **#7** — Fix write_config mất excluded_apps (trung bình)
8. **#17** — Implement epoll (tối ưu CPU)
9. **#22** — Implement các option UI còn thiếu (tính năng)
10. **#23** — Hỗ trợ Electron apps (tính năng quan trọng)
