/**
 * @file config_watcher.h
 * @brief inotify-based Config File Watcher - Phase 4
 *
 * Lắng nghe thay đổi của file JSON config và notify daemon
 * để update state (bật/tắt tiếng Việt, chọn kiểu gõ...) real-time.
 */

#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>

/**
 * @brief Cấu hình từ JSON file
 */
struct VnxConfig {
    bool enabled = true;             ///< Bật/tắt tiếng Việt
    std::string input_method = "telex"; ///< "telex" hoặc "vni"
    std::string output_charset = "unicode"; ///< "unicode"
    std::string toggle_shortcut = "Ctrl+Shift"; ///< "Ctrl+Shift", "Alt+Z", "Ctrl+Space"
    bool emergency_stop = false;     ///< Dừng khẩn cấp: nhả hoàn toàn bàn phím
};

/**
 * @brief Callback khi config thay đổi
 */
using ConfigCallback = std::function<void(const VnxConfig&)>;

/**
 * @brief inotify-based Config Watcher (RAII)
 *
 * Chạy 1 background thread để watch file JSON.
 * Khi file thay đổi, đọc JSON và gọi callback.
 *
 * Thread-safety: Callback được gọi từ background thread.
 * Đảm bảo callback không block và thread-safe.
 */
class ConfigWatcher {
public:
    explicit ConfigWatcher(const std::string& config_path);
    ~ConfigWatcher();

    // Non-copyable
    ConfigWatcher(const ConfigWatcher&) = delete;
    ConfigWatcher& operator=(const ConfigWatcher&) = delete;

    /**
     * @brief Bắt đầu watch và gọi callback khi có thay đổi
     * @param callback  Hàm được gọi khi config thay đổi
     */
    void start(ConfigCallback callback);

    /**
     * @brief Dừng watching
     */
    void stop();

    /**
     * @brief Đọc config ngay (không cần đợi file change)
     * @return Config hiện tại từ file, hoặc default nếu lỗi
     */
    VnxConfig read_config() const;

    /**
     * @brief Ghi lại config xuống file
     */
    void write_config(const VnxConfig& cfg) const;

    /**
     * @brief Đảm bảo thư mục config tồn tại
     * Tạo ~/.config/vnxkey/ nếu chưa có
     */
    static bool ensure_config_dir();

private:
    std::string m_config_path;
    std::atomic<bool> m_running{false};
    std::thread m_watch_thread;
    int m_inotify_fd = -1;
    int m_watch_fd = -1;

    void watch_loop(ConfigCallback callback);
    VnxConfig parse_json(const std::string& json_content) const;
};
