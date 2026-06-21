/**
 * @file evdev_handler.h
 * @brief Evdev Keyboard Grabber - Phase 3
 *
 * Grab bàn phím vật lý bằng libevdev, intercept key events.
 *
 * === SAFETY DESIGN ===
 * Đây là module nguy hiểm nhất trong toàn bộ codebase.
 * Nếu grab mà không ungrab được, bàn phím người dùng sẽ bị treo.
 *
 * Các biện pháp an toàn:
 * 1. RAII: Destructor LUÔN gọi ungrab() và close device.
 * 2. Global cleanup: main.cpp đăng ký signal handlers (SIGTERM/SIGINT/SIGSEGV)
 *    để ungrab trước khi exit.
 * 3. Timeout: Nếu không nhận được event trong timeout, tự động ungrab.
 * 4. Move semantics: Tránh copy để tránh double-ungrab.
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <atomic>
#include <memory>

// Forward declaration để tránh include libevdev trong header
struct libevdev;
struct input_event;

/**
 * @brief Kết quả từ mỗi key event được đọc
 */
struct KeyEvent {
    int keycode;    ///< Linux keycode (KEY_*)
    int value;      ///< 0=release, 1=press, 2=repeat
};

/**
 * @brief Callback type: nhận KeyEvent, trả về bool (true = consumed, false = passthrough)
 */
using KeyEventCallback = std::function<bool(const KeyEvent&)>;

/**
 * @brief RAII Evdev Keyboard Grabber
 *
 * Mở thiết bị /dev/input/eventX, grab (intercept toàn bộ phím),
 * đọc events và gọi callback. Destructor tự động ungrab và close.
 *
 * === CRITICAL GRAB/UNGRAB CONTRACT ===
 * - grab() phải được gọi SAU khi device mở thành công.
 * - ungrab() phải được gọi TRƯỚC khi close device.
 * - Destructor đảm bảo ungrab() được gọi trong mọi tình huống.
 * - main() phải thiết lập signal handlers để trigger destructor.
 *
 * Thread-safety: KHÔNG thread-safe. Chỉ dùng từ 1 thread.
 */
class EvdevHandler {
public:
    EvdevHandler();

    /**
     * @brief Destructor - LUÔN ungrab và close device
     *
     * === SAFETY CRITICAL ===
     * Phần này đảm bảo bàn phím không bị treo kể cả khi:
     * - Object bị destroy vì exception
     * - Object out of scope bình thường
     * Tuy nhiên: Nếu process bị SIGKILL (-9), destructor KHÔNG được gọi!
     * Vì vậy main() cần thêm signal handlers và tránh dùng kill -9 với daemon.
     */
    ~EvdevHandler();

    // Non-copyable để tránh double-ungrab
    EvdevHandler(const EvdevHandler&) = delete;
    EvdevHandler& operator=(const EvdevHandler&) = delete;

    // Movable - transfer ownership
    EvdevHandler(EvdevHandler&& other) noexcept;
    EvdevHandler& operator=(EvdevHandler&&) noexcept;

    /**
     * @brief Mở thiết bị keyboard
     *
     * @param device_path  Đường dẫn tới device, ví dụ "/dev/input/event3"
     *                     Dùng find_keyboard() để tự động tìm.
     * @return true nếu mở thành công
     */
    bool open_device(const std::string& device_path);

    /**
     * @brief Tìm tất cả các keyboard vật lý
     * @return Danh sách các path tới device (vd: /dev/input/eventX)
     */
    static std::vector<std::string> find_keyboards();

    /**
     * @brief Grab (chiếm độc quyền) bàn phím
     *
     * Sau khi grab, tất cả key events sẽ đến daemon thay vì ứng dụng khác.
     *
     * === SAFETY WARNING ===
     * Sau khi gọi grab(), bàn phím bị treo cho đến khi ungrab() được gọi.
     * Đảm bảo ungrab() luôn được gọi trước khi exit.
     *
     * @return true nếu grab thành công
     */
    bool grab();

    /**
     * @brief Ungrab (nhả) bàn phím
     *
     * An toàn để gọi nhiều lần (idempotent).
     * Được gọi tự động bởi destructor.
     */
    void ungrab();

    /**
     * @brief Kiểm tra đang grab hay không
     */
    bool is_grabbed() const { return m_is_grabbed; }

    /**
     * @brief Kiểm tra device đã mở chưa
     */
    bool is_open() const { return m_fd >= 0; }

    /**
     * @brief Close device (và ungrab nếu đang grab)
     */
    void close_device();

    /**
     * @brief Đọc 1 key event (blocking)
     *
     * Block cho đến khi có event, hoặc bị interrupt bởi signal.
     * @param[out] event  Event đọc được
     * @return true nếu đọc thành công, false nếu error/signal
     */
    bool read_event(KeyEvent& event);

    /**
     * @brief Lấy tên thiết bị (debug info)
     */
    std::string get_device_name() const;

private:
    int m_fd = -1;                  ///< File descriptor tới /dev/input/eventX
    struct libevdev* m_dev = nullptr; ///< libevdev device handle
    bool m_is_grabbed = false;      ///< Trạng thái grab hiện tại
};
