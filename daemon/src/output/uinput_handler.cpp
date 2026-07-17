/**
 * @file uinput_handler.cpp
 * @brief Uinput Virtual Keyboard Handler - Phase 3 Implementation
 *
 * Tạo virtual keyboard bằng /dev/uinput để phát phím ra ứng dụng.
 *
 * Cơ chế phát Unicode:
 * - Dùng GTK Unicode input sequence: Ctrl+Shift+U → hex → Enter
 * - Hoạt động trên hầu hết ứng dụng GTK (GNOME, Firefox, etc.)
 * - Hoặc dùng xdotool type cho môi trường X11
 */

#include "uinput_handler.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <linux/uinput.h>

// ============================================================
// Constructor / Destructor (RAII)
// ============================================================

UinputHandler::UinputHandler() : m_fd(-1) {}

UinputHandler::~UinputHandler() {
    // CRITICAL: Luôn destroy device khi object bị hủy
    // Kể cả khi process bị terminate bởi exception/signal
    destroy_device();
}

UinputHandler::UinputHandler(UinputHandler&& other) noexcept
    : m_fd(other.m_fd) {
    other.m_fd = -1; // Transfer ownership
}

// ============================================================
// Device Management
// ============================================================

bool UinputHandler::create_device() {
    // Mở /dev/uinput
    m_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (m_fd < 0) {
        std::cerr << "[uinput] ERROR: Cannot open /dev/uinput: "
                  << strerror(errno) << std::endl;
        std::cerr << "[uinput] Make sure: sudo modprobe uinput" << std::endl;
        return false;
    }

    // Khai báo các event types mà virtual device này sẽ phát
    // EV_KEY: keyboard events
    if (ioctl(m_fd, UI_SET_EVBIT, EV_KEY) < 0) {
        std::cerr << "[uinput] ERROR: UI_SET_EVBIT EV_KEY failed" << std::endl;
        destroy_device();
        return false;
    }
    if (ioctl(m_fd, UI_SET_EVBIT, EV_SYN) < 0) {
        std::cerr << "[uinput] ERROR: UI_SET_EVBIT EV_SYN failed" << std::endl;
        destroy_device();
        return false;
    }
    
    // Add EV_REP to be recognized as a real keyboard by libinput/Wayland
    ioctl(m_fd, UI_SET_EVBIT, EV_REP);
    
    // Add EV_LED for CapsLock/NumLock recognition
    ioctl(m_fd, UI_SET_EVBIT, EV_LED);
    ioctl(m_fd, UI_SET_LEDBIT, LED_NUML);
    ioctl(m_fd, UI_SET_LEDBIT, LED_CAPSL);
    ioctl(m_fd, UI_SET_LEDBIT, LED_SCROLLL);

    // Đăng ký tất cả keycodes thông dụng (0 đến KEY_MAX)
    for (int key = 0; key <= KEY_MAX; key++) {
        ioctl(m_fd, UI_SET_KEYBIT, key);
    }

    // Cấu hình thông tin device
    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x1234;  // Vendor ID giả
    usetup.id.product = 0x5678;  // Product ID giả
    strncpy(usetup.name, "VNXKey Virtual Keyboard", UINPUT_MAX_NAME_SIZE - 1);

    if (ioctl(m_fd, UI_DEV_SETUP, &usetup) < 0) {
        std::cerr << "[uinput] ERROR: UI_DEV_SETUP failed: "
                  << strerror(errno) << std::endl;
        destroy_device();
        return false;
    }

    // Tạo device
    if (ioctl(m_fd, UI_DEV_CREATE) < 0) {
        std::cerr << "[uinput] ERROR: UI_DEV_CREATE failed: "
                  << strerror(errno) << std::endl;
        destroy_device();
        return false;
    }

    // Đợi kernel nhận diện device (thường ~100ms)
    usleep(100000);

    std::cout << "[uinput] Virtual keyboard created successfully" << std::endl;
    return true;
}

void UinputHandler::destroy_device() {
    if (m_fd >= 0) {
        // Destroy uinput device trước khi close fd
        ioctl(m_fd, UI_DEV_DESTROY);
        close(m_fd);
        m_fd = -1;
        std::cout << "[uinput] Virtual keyboard destroyed" << std::endl;
    }
}

// ============================================================
// Event Emission
// ============================================================

void UinputHandler::send_event(uint16_t type, uint16_t code, int32_t value) {
    if (m_fd < 0) return;

    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type  = type;
    ev.code  = code;
    ev.value = value;

    if (write(m_fd, &ev, sizeof(ev)) < 0) {
        // Không throw - chỉ log lỗi để tránh crash daemon
        std::cerr << "[uinput] WARNING: write event failed: "
                  << strerror(errno) << std::endl;
    }
}

void UinputHandler::sync() {
    send_event(EV_SYN, SYN_REPORT, 0);
}

void UinputHandler::emit_key(int keycode, int value) {
    if (m_fd < 0) return;

    if (value != -1) {
        // Chỉ gửi đúng value được truyền (1 = press, 0 = release, 2 = repeat)
        send_event(EV_KEY, static_cast<uint16_t>(keycode), value);
        sync();
    } else {
        // Hành vi cũ: Nhấn rồi thả ngay
        send_event(EV_KEY, static_cast<uint16_t>(keycode), 1);
        sync();
        usleep(1000); // 1ms delay between press and release to prevent dropped keys
        send_event(EV_KEY, static_cast<uint16_t>(keycode), 0);
        sync();
        usleep(5000); // 5ms delay after release
    }
}

void UinputHandler::emit_backspace() {
    emit_key(KEY_BACKSPACE);
}

void UinputHandler::emit_backspace(int count) {
    if (count <= 0) return;

    // --- WORKAROUND FOR OMNIBOX/AUTOCOMPLETE HIGHLIGHT ---
    // If the browser (like Chrome) has highlighted auto-complete text,
    // the first Backspace will ONLY delete the highlight, NOT the character.
    // To safely clear any highlight before our real backspaces, we send a dummy printable character
    // (KEY_SPACE) and immediately backspace it. This forces the highlight to be overwritten and cleared.
    emit_key(KEY_SPACE);
    emit_key(KEY_BACKSPACE);
    // -----------------------------------------------------

    for (int i = 0; i < count; ++i) {
        emit_backspace();
    }
}

// ============================================================
// Unicode emission via GTK Ctrl+Shift+U sequence
//
// GTK hỗ trợ nhập Unicode qua: Ctrl+Shift+U → nhập hex codepoint → Enter
// Đây là chuẩn hoạt động trong hầu hết ứng dụng Linux (GNOME, Firefox, etc.)
// Tham khảo: https://docs.gtk.org/gtk4/input-handling.html
// ============================================================

/**
 * @brief Map hex digit char → keycode
 */
static int hex_char_to_keycode(char c) {
    switch (c) {
        case '0': return KEY_0;
        case '1': return KEY_1;
        case '2': return KEY_2;
        case '3': return KEY_3;
        case '4': return KEY_4;
        case '5': return KEY_5;
        case '6': return KEY_6;
        case '7': return KEY_7;
        case '8': return KEY_8;
        case '9': return KEY_9;
        case 'a': case 'A': return KEY_A;
        case 'b': case 'B': return KEY_B;
        case 'c': case 'C': return KEY_C;
        case 'd': case 'D': return KEY_D;
        case 'e': case 'E': return KEY_E;
        case 'f': case 'F': return KEY_F;
        default: return -1;
    }
}

static int basic_ascii_to_keycode(char c, bool& shift) {
    shift = false;
    
    // Control characters
    if (c == '\n') return KEY_ENTER;
    if (c == '\t') return KEY_TAB;
    if (c == '\b') return KEY_BACKSPACE;
    if (c == '\x1b') return KEY_ESC;
    
    // Space and digits
    if (c == ' ') return KEY_SPACE;
    if (c >= '0' && c <= '9') {
        if (c == '0') return KEY_0;
        return KEY_1 + (c - '1');
    }
    
    // Lowercase letters
    if (c >= 'a' && c <= 'z') {
        switch (c) {
            case 'a': return KEY_A; case 'b': return KEY_B; case 'c': return KEY_C;
            case 'd': return KEY_D; case 'e': return KEY_E; case 'f': return KEY_F;
            case 'g': return KEY_G; case 'h': return KEY_H; case 'i': return KEY_I;
            case 'j': return KEY_J; case 'k': return KEY_K; case 'l': return KEY_L;
            case 'm': return KEY_M; case 'n': return KEY_N; case 'o': return KEY_O;
            case 'p': return KEY_P; case 'q': return KEY_Q; case 'r': return KEY_R;
            case 's': return KEY_S; case 't': return KEY_T; case 'u': return KEY_U;
            case 'v': return KEY_V; case 'w': return KEY_W; case 'x': return KEY_X;
            case 'y': return KEY_Y; case 'z': return KEY_Z;
        }
    }
    
    // Uppercase letters
    if (c >= 'A' && c <= 'Z') {
        shift = true;
        switch (c) {
            case 'A': return KEY_A; case 'B': return KEY_B; case 'C': return KEY_C;
            case 'D': return KEY_D; case 'E': return KEY_E; case 'F': return KEY_F;
            case 'G': return KEY_G; case 'H': return KEY_H; case 'I': return KEY_I;
            case 'J': return KEY_J; case 'K': return KEY_K; case 'L': return KEY_L;
            case 'M': return KEY_M; case 'N': return KEY_N; case 'O': return KEY_O;
            case 'P': return KEY_P; case 'Q': return KEY_Q; case 'R': return KEY_R;
            case 'S': return KEY_S; case 'T': return KEY_T; case 'U': return KEY_U;
            case 'V': return KEY_V; case 'W': return KEY_W; case 'X': return KEY_X;
            case 'Y': return KEY_Y; case 'Z': return KEY_Z;
        }
    }
    
    // Punctuation (unshifted)
    switch (c) {
        case '`': return KEY_GRAVE;
        case '-': return KEY_MINUS;
        case '=': return KEY_EQUAL;
        case '[': return KEY_LEFTBRACE;
        case ']': return KEY_RIGHTBRACE;
        case '\\': return KEY_BACKSLASH;
        case ';': return KEY_SEMICOLON;
        case '\'': return KEY_APOSTROPHE;
        case ',': return KEY_COMMA;
        case '.': return KEY_DOT;
        case '/': return KEY_SLASH;
    }
    
    // Punctuation (shifted)
    shift = true;
    switch (c) {
        case '~': return KEY_GRAVE;
        case '!': return KEY_1;
        case '@': return KEY_2;
        case '#': return KEY_3;
        case '$': return KEY_4;
        case '%': return KEY_5;
        case '^': return KEY_6;
        case '&': return KEY_7;
        case '*': return KEY_8;
        case '(': return KEY_9;
        case ')': return KEY_0;
        case '_': return KEY_MINUS;
        case '+': return KEY_EQUAL;
        case '{': return KEY_LEFTBRACE;
        case '}': return KEY_RIGHTBRACE;
        case '|': return KEY_BACKSLASH;
        case ':': return KEY_SEMICOLON;
        case '"': return KEY_APOSTROPHE;
        case '<': return KEY_COMMA;
        case '>': return KEY_DOT;
        case '?': return KEY_SLASH;
    }
    
    shift = false;
    return -1;
}

std::vector<uint32_t> UinputHandler::utf8_to_codepoints(const std::string& utf8) {
    std::vector<uint32_t> codepoints;
    size_t i = 0;
    while (i < utf8.size()) {
        unsigned char c = utf8[i];
        uint32_t cp = 0;
        int extra = 0;

        if (c < 0x80) {
            cp = c;
            extra = 0;
        } else if (c < 0xC0) {
            // Continuation byte ở đầu - invalid UTF-8, skip
            i++;
            continue;
        } else if (c < 0xE0) {
            cp = c & 0x1F;
            extra = 1;
        } else if (c < 0xF0) {
            cp = c & 0x0F;
            extra = 2;
        } else {
            cp = c & 0x07;
            extra = 3;
        }

        i++;
        for (int j = 0; j < extra && i < utf8.size(); j++, i++) {
            cp = (cp << 6) | (utf8[i] & 0x3F);
        }

        codepoints.push_back(cp);
    }
    return codepoints;
}

void UinputHandler::emit_unicode(const std::string& utf8_char, bool is_capslock_on) {
    if (m_fd < 0 || utf8_char.empty()) return;

    auto codepoints = utf8_to_codepoints(utf8_char);
    if (codepoints.empty()) return;

    for (uint32_t cp : codepoints) {
        // [WORKING][CRITICAL] Bypass Ctrl+Shift+U for basic ASCII characters (120 WPM fix) - DO NOT MODIFY UNLESS NECESSARY
        if (cp < 0x80) {
            bool shift = false;
            int kc = basic_ascii_to_keycode(static_cast<char>(cp), shift);
            if (kc >= 0) {
                // Sửa lỗi: Nếu CapsLock đang BẬT, trạng thái shift cho các phím chữ cái (A-Z) sẽ bị ĐẢO NGƯỢC
                // Ví dụ: Muốn in 'A' (shift=true), nhưng CapsLock ON -> gửi 'A' KHÔNG shift để hệ điều hành in hoa.
                // Muốn in 'a' (shift=false), nhưng CapsLock ON -> gửi 'A' CÓ shift để hệ điều hành in thường.
                if (is_capslock_on && kc >= KEY_A && kc <= KEY_Z) {
                    shift = !shift;
                }

                if (shift) send_event(EV_KEY, KEY_LEFTSHIFT, 1);
                sync();
                
                send_event(EV_KEY, static_cast<uint16_t>(kc), 1);
                sync();
                usleep(1000); // 1ms delay between press and release
                send_event(EV_KEY, static_cast<uint16_t>(kc), 0);
                sync();
                
                if (shift) send_event(EV_KEY, KEY_LEFTSHIFT, 0);
                sync();
                
                usleep(2000); // 2ms delay (optimized from 10ms for much faster ASCII typing)
                continue;
            }
        }
        
        // Chuyển codepoint sang hex string
        std::ostringstream oss;
        oss << std::hex << cp;
        std::string hex = oss.str();

        // Phát Ctrl+Shift+U (nhấn xuống)
        send_event(EV_KEY, KEY_LEFTCTRL, 1);
        send_event(EV_KEY, KEY_LEFTSHIFT, 1);
        sync();
        usleep(1000); // Ensure modifier state is processed by the OS
        send_event(EV_KEY, KEY_U, 1);
        sync();
        usleep(1000); // 1ms delay between press and release
        send_event(EV_KEY, KEY_U, 0);
        sync();
        send_event(EV_KEY, KEY_LEFTSHIFT, 0);
        send_event(EV_KEY, KEY_LEFTCTRL, 0);
        sync();
        // QUAN TRỌNG: Phải chờ đủ lâu (khoảng 10-15ms) để GTK/Mutter kích hoạt trạng thái "Unicode Input Mode"
        // Nếu gõ hex quá nhanh ngay sau khi nhả Ctrl+Shift+U, GTK sẽ không nhận diện kịp và in thẳng hex code ra màn hình (ví dụ: 1eef)
        // [WORKING][CRITICAL] 15ms delay cho GTK4/Terminal - DO NOT MODIFY UNLESS NECESSARY
        usleep(15000); 

        // Gõ từng ký tự hex
        for (char hc : hex) {
            int kc = hex_char_to_keycode(hc);
            if (kc >= 0) {
                send_event(EV_KEY, static_cast<uint16_t>(kc), 1);
                sync();
                usleep(1000); // 1ms delay between press and release
                send_event(EV_KEY, static_cast<uint16_t>(kc), 0);
                sync();
                usleep(2000); // Tăng lên 2ms để tránh Wayland drop phím khi gõ hex dài (như 1b0)
            }
        }

        // Dùng SPACE để confirm thay vì ENTER để tránh nguy cơ tự động chạy lệnh
        // nếu ứng dụng (như Terminal) không hỗ trợ tính năng Ctrl+Shift+U của GTK.
        send_event(EV_KEY, KEY_SPACE, 1);
        sync();
        usleep(1000); // 1ms delay between press and release
        send_event(EV_KEY, KEY_SPACE, 0);
        sync();
        // Tăng lên 50ms để Terminal/GTK chắc chắn có đủ thời gian reset IM context
        // Đặc biệt khi in nhiều ký tự liên tiếp như "ươ" (uow) để không bị văng code hex
        usleep(50000); 
    }
}
