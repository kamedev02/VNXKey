/**
 * @file evdev_handler.cpp
 * @brief Evdev Keyboard Grabber - Phase 3 Implementation
 *
 * === SAFETY CRITICAL CODE ===
 *
 * Module này implement việc grab bàn phím vật lý.
 * Grab là thao tác nguy hiểm: nếu không ungrab được,
 * bàn phím bị treo hoàn toàn cho đến khi restart service
 * hoặc unplug/plug lại bàn phím.
 *
 * Các lớp bảo vệ:
 * 1. RAII: destructor ungrab + close
 * 2. Signal handlers trong main.cpp (SIGTERM, SIGINT)
 * 3. idempotent ungrab() (gọi nhiều lần không sao)
 * 4. Log rõ ràng mỗi bước grab/ungrab
 * 5. Không dùng exit() trực tiếp - luôn để RAII cleanup
 */

#include "evdev_handler.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <libevdev/libevdev.h>

// ============================================================
// Constructor / Destructor (RAII)
// ============================================================

EvdevHandler::EvdevHandler()
    : m_fd(-1), m_dev(nullptr), m_is_grabbed(false) {}

EvdevHandler::~EvdevHandler() {
    // CRITICAL: Đây là safety net chính.
    // Destructtor LUÔN được gọi khi object out of scope (kể cả exception).
    // Exception duy nhất: SIGKILL - không thể xử lý được.
    close_device();
}

EvdevHandler::EvdevHandler(EvdevHandler&& other) noexcept
    : m_fd(other.m_fd)
    , m_dev(other.m_dev)
    , m_is_grabbed(other.m_is_grabbed)
    , m_path(std::move(other.m_path))
{
    // Transfer ownership - old object không còn manage resource nữa
    other.m_fd = -1;
    other.m_dev = nullptr;
    other.m_is_grabbed = false;
}

EvdevHandler& EvdevHandler::operator=(EvdevHandler&& other) noexcept {
    if (this != &other) {
        // Cleanup hiện tại trước
        close_device();
        // Transfer ownership
        m_fd = other.m_fd;
        m_dev = other.m_dev;
        m_is_grabbed = other.m_is_grabbed;
        m_path = std::move(other.m_path);
        other.m_fd = -1;
        other.m_dev = nullptr;
        other.m_is_grabbed = false;
    }
    return *this;
}

// ============================================================
// Device discovery
// ============================================================

std::vector<std::string> EvdevHandler::find_keyboards() {
    std::vector<std::string> keyboards;
    const std::string base = "/dev/input/";

    try {
        for (const auto& entry : std::filesystem::directory_iterator(base)) {
            std::string path = entry.path().string();

            // Chỉ xét event* files
            if (path.find("/dev/input/event") == std::string::npos) continue;

            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;

            struct libevdev* dev = nullptr;
            int rc = libevdev_new_from_fd(fd, &dev);
            if (rc < 0) {
                close(fd);
                continue;
            }

            // Kiểm tra device có EV_KEY và có các phím chữ cái không
            bool has_key = libevdev_has_event_type(dev, EV_KEY);
            bool has_alpha = libevdev_has_event_code(dev, EV_KEY, KEY_A) &&
                             libevdev_has_event_code(dev, EV_KEY, KEY_Z);
            // Loại trừ mouse (chỉ có EV_REL)
            bool is_mouse = libevdev_has_event_type(dev, EV_REL) && !has_alpha;

            const char* dev_name = libevdev_get_name(dev);
            std::string name = dev_name ? dev_name : "";
            libevdev_free(dev);
            close(fd);

            if (has_key && has_alpha && !is_mouse) {
                if (name.find("VNXKey") != std::string::npos) {
                    continue; // Tránh tự grab virtual keyboard của chính mình!
                }
                std::cout << "[evdev] Found keyboard: " << path
                          << " (" << name << ")" << std::endl;
                keyboards.push_back(path);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[evdev] ERROR scanning /dev/input: " << e.what() << std::endl;
    }

    return keyboards;
}

// ============================================================
// Device Management
// ============================================================

bool EvdevHandler::open_device(const std::string& device_path) {
    if (m_fd >= 0) {
        std::cerr << "[evdev] WARNING: Device already open, closing first" << std::endl;
        close_device();
    }

    // Mở với O_RDONLY | O_NONBLOCK
    // NONBLOCK để read() không block mãi khi không có event
    m_fd = open(device_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (m_fd < 0) {
        std::cerr << "[evdev] ERROR: Cannot open " << device_path
                  << ": " << strerror(errno) << std::endl;
        if (errno == EACCES) {
            std::cerr << "[evdev] Hint: Run as root or add user to 'input' group" << std::endl;
        }
        return false;
    }

    // Tạo libevdev device từ fd
    int rc = libevdev_new_from_fd(m_fd, &m_dev);
    if (rc < 0) {
        std::cerr << "[evdev] ERROR: libevdev_new_from_fd failed: "
                  << strerror(-rc) << std::endl;
        close(m_fd);
        m_fd = -1;
        return false;
    }

    m_path = device_path;

    std::cout << "[evdev] Opened: " << device_path
              << " (" << libevdev_get_name(m_dev) << ")" << std::endl;
    return true;
}

std::string EvdevHandler::get_device_name() const {
    if (m_dev) {
        const char* name = libevdev_get_name(m_dev);
        return name ? name : "(unknown)";
    }
    return "(no device)";
}

// ============================================================
// GRAB / UNGRAB - Phần quan trọng nhất
// ============================================================

bool EvdevHandler::grab() {
    if (!m_dev || m_fd < 0) {
        std::cerr << "[evdev] ERROR: Cannot grab - device not open" << std::endl;
        return false;
    }

    if (m_is_grabbed) {
        std::cerr << "[evdev] WARNING: Already grabbed" << std::endl;
        return true;
    }

    // LIBEVDEV_GRAB: chiếm độc quyền thiết bị
    // Sau lệnh này, các ứng dụng khác KHÔNG nhận được key events nữa
    // → Bàn phím "ngừng hoạt động" cho đến khi ungrab
    int rc = libevdev_grab(m_dev, LIBEVDEV_GRAB);
    if (rc < 0) {
        std::cerr << "[evdev] ERROR: libevdev_grab failed: "
                  << strerror(-rc) << std::endl;
        return false;
    }

    m_is_grabbed = true;
    std::cout << "[evdev] Keyboard GRABBED: " << get_device_name() << std::endl;
    std::cout << "[evdev] WARNING: Keyboard is now exclusive to vnxkey daemon!" << std::endl;
    return true;
}

void EvdevHandler::ungrab() {
    // idempotent: an toàn khi gọi nhiều lần
    if (!m_dev || !m_is_grabbed) {
        return; // Không grab thì không cần ungrab
    }

    // LIBEVDEV_UNGRAB: nhả bàn phím về trạng thái bình thường
    // Sau lệnh này, key events trở lại các ứng dụng bình thường
    int rc = libevdev_grab(m_dev, LIBEVDEV_UNGRAB);
    if (rc < 0) {
        // Lỗi ungrab là nghiêm trọng nhưng không thể làm gì hơn
        std::cerr << "[evdev] ERROR: libevdev_grab UNGRAB failed: "
                  << strerror(-rc) << std::endl;
        // Vẫn set m_is_grabbed = false để tránh vòng lặp
    } else {
        std::cout << "[evdev] Keyboard UNGRABBED: " << get_device_name() << std::endl;
    }

    m_is_grabbed = false;
}

void EvdevHandler::close_device() {
    // Bước 1: Ungrab TRƯỚC khi close
    // CRITICAL ORDER: Phải ungrab trước, close sau
    // Nếu close trước, kernel sẽ tự ungrab nhưng không đảm bảo
    if (m_is_grabbed) {
        ungrab();
    }

    // Bước 2: Free libevdev device
    if (m_dev) {
        libevdev_free(m_dev);
        m_dev = nullptr;
    }

    // Bước 3: Close file descriptor
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}

// ============================================================
// Event Reading
// ============================================================

ReadStatus EvdevHandler::read_event(KeyEvent& out_event) {
    if (!m_dev || m_fd < 0) return ReadStatus::ERROR;

    struct input_event ev;
    int rc;

    // Loop để bỏ qua các event không phải key (EV_SYN, EV_MSC, ...)
    while (true) {
        // LIBEVDEV_READ_FLAG_NORMAL: đọc event thực (không phải syn dropped)
        rc = libevdev_next_event(m_dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // Kernel báo SYN_DROPPED: đọc hết events bị bỏ sót
            while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                rc = libevdev_next_event(m_dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
            }
            continue;
        }

        if (rc == -EAGAIN) {
            // Không có event (NONBLOCK mode)
            return ReadStatus::NO_EVENT;
        }

        if (rc == -ENODEV) {
            std::cerr << "[evdev] Device disconnected: " << get_device_name() << std::endl;
            return ReadStatus::DISCONNECTED;
        }

        if (rc < 0) {
            // Lỗi thực sự
            std::cerr << "[evdev] ERROR reading event: " << strerror(-rc) << std::endl;
            return ReadStatus::ERROR;
        }

        // Chỉ xử lý EV_KEY events (bỏ qua EV_SYN, EV_MSC, etc.)
        if (ev.type == EV_KEY) {
            out_event.keycode = ev.code;
            out_event.value   = ev.value; // 0=release, 1=press, 2=repeat
            return ReadStatus::HAS_EVENT;
        }
    }
}
