/**
 * @file uinput_handler.h
 * @brief Uinput Virtual Keyboard Handler - Phase 3
 *
 * Tạo thiết bị bàn phím ảo bằng /dev/uinput để phát phím Unicode.
 * Dùng RAII - destructor luôn destroy device khi object out of scope.
 */

#pragma once

#include <string>
#include <cstdint>
#include <vector>

/**
 * @brief RAII wrapper cho uinput virtual keyboard device
 *
 * Tạo và quản lý 1 thiết bị bàn phím ảo. Destructor tự động
 * destroy device, đảm bảo không bị leak kể cả khi có exception.
 *
 * Thread-safety: KHÔNG thread-safe. Dùng từ 1 thread.
 */
class UinputHandler {
public:
    UinputHandler();

    /**
     * @brief Destructor - luôn destroy uinput device
     * QUAN TRỌNG: Bảo đảm device được cleanup kể cả khi crash.
     */
    ~UinputHandler();

    // Non-copyable
    UinputHandler(const UinputHandler&) = delete;
    UinputHandler& operator=(const UinputHandler&) = delete;

    // Movable
    UinputHandler(UinputHandler&& other) noexcept;

    /**
     * @brief Khởi tạo và tạo virtual keyboard device
     * @return true nếu thành công
     */
    bool create_device();

    /**
     * @brief Destroy virtual keyboard device
     */
    void destroy_device();

    /**
     * @brief Kiểm tra device đã được tạo chưa
     */
    bool is_valid() const { return m_fd >= 0; }

    /**
     * @brief Phát một phím bấm vật lý
     * @param value 1: press, 0: release, 2: repeat. Nếu -1, tự động bấm và thả (legacy)
     */
    void emit_key(int keycode, int value = -1);

    /**
     * @brief Phát phím Backspace
     */
    void emit_backspace();

    /**
     * @brief Phát 1 ký tự Unicode qua clipboard/xdotool fallback
     *
     * Vì uinput chỉ hỗ trợ keycodes chuẩn, để phát Unicode tùy ý
     * ta dùng phương pháp: key sequence Ctrl+Shift+U + hex codepoint.
     * Đây là chuẩn GTK Unicode input (hoạt động trên GNOME/KDE).
     *
     * @param utf8_char  Ký tự UTF-8 cần phát
     */
    void emit_unicode(const std::string& utf8_char, bool is_capslock_on = false);

    /**
     * @brief Phát nhiều Backspace
     * @param count Số lần nhấn Backspace
     */
    void emit_backspace(int count);

private:
    int m_fd = -1;  ///< File descriptor tới /dev/uinput

    /**
     * @brief Gửi 1 input_event raw tới kernel
     */
    void send_event(uint16_t type, uint16_t code, int32_t value);

    /**
     * @brief Gửi SYN_REPORT để kernel xử lý events đã buffer
     */
    void sync();

    /**
     * @brief Chuyển UTF-8 string sang danh sách Unicode codepoints
     */
    static std::vector<uint32_t> utf8_to_codepoints(const std::string& utf8);
};
