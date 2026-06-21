/**
 * @file config_watcher.cpp
 * @brief inotify-based Config Watcher - Phase 4 Implementation
 *
 * Dùng inotify (Linux kernel API) để watch file config thay đổi.
 * Khi Flutter GUI save config mới, daemon nhận thông báo và
 * update state ngay mà không cần restart.
 *
 * JSON parsing: tự implement minimal parser để tránh dependency nặng.
 * Chỉ cần parse {"enabled": bool, "input_method": str, "output_charset": str}
 */

#include "config_watcher.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/inotify.h>

// ============================================================
// Constructor / Destructor
// ============================================================

ConfigWatcher::ConfigWatcher(const std::string& config_path)
    : m_config_path(config_path) {}

ConfigWatcher::~ConfigWatcher() {
    stop();
}

// ============================================================
// Config directory setup
// ============================================================

bool ConfigWatcher::ensure_config_dir() {
    std::string config_dir = "/var/tmp/vnxkey";
    try {
        std::filesystem::create_directories(config_dir);
        // Chmod 0777 để mọi user (kể cả UI) có thể ghi vào đây
        std::filesystem::permissions(config_dir,
            std::filesystem::perms::all,
            std::filesystem::perm_options::replace);
        std::cout << "[config] Config dir: " << config_dir << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[config] ERROR creating config dir: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================
// JSON Parsing (minimal, no dependencies)
// ============================================================

/**
 * @brief Tìm value của key trong JSON string đơn giản
 * Chỉ hỗ trợ flat JSON object (không nested).
 */
static std::string json_get_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos += search.size();

    // Skip whitespace và dấu :
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':')) pos++;

    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        // String value
        pos++;
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    } else {
        // Boolean/number value
        size_t end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}') end++;
        std::string val = json.substr(pos, end - pos);
        // Trim whitespace
        while (!val.empty() && std::isspace(val.back())) val.pop_back();
        return val;
    }
}

VnxConfig ConfigWatcher::parse_json(const std::string& json) const {
    VnxConfig cfg;

    std::string enabled_str = json_get_value(json, "enabled");
    if (!enabled_str.empty()) {
        cfg.enabled = (enabled_str == "true" || enabled_str == "1");
    }

    std::string method = json_get_value(json, "input_method");
    if (!method.empty()) {
        cfg.input_method = method;
    }

    std::string charset = json_get_value(json, "output_charset");
    if (!charset.empty()) {
        cfg.output_charset = charset;
    }

    std::string em_stop_str = json_get_value(json, "emergency_stop");
    if (!em_stop_str.empty()) {
        cfg.emergency_stop = (em_stop_str == "true" || em_stop_str == "1");
    }

    std::string shortcut = json_get_value(json, "toggle_shortcut");
    if (!shortcut.empty()) {
        cfg.toggle_shortcut = shortcut;
    }

    return cfg;
}

VnxConfig ConfigWatcher::read_config() const {
    std::ifstream file(m_config_path);
    if (!file.is_open()) {
        std::cout << "[config] Config file not found, using defaults: "
                  << m_config_path << std::endl;
        return VnxConfig{};
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    if (content.empty()) {
        return VnxConfig{};
    }

    VnxConfig cfg = parse_json(content);
    std::cout << "[config] Loaded: enabled=" << cfg.enabled
              << " method=" << cfg.input_method 
              << " shortcut=" << cfg.toggle_shortcut << std::endl;
    return cfg;
}

void ConfigWatcher::write_config(const VnxConfig& cfg) const {
    std::ofstream file(m_config_path);
    if (!file.is_open()) {
        std::cerr << "[config] ERROR writing to config file" << std::endl;
        return;
    }
    // Very simple JSON generation, assume string values do not contain quotes
    file << "{\n"
         << "  \"enabled\": " << (cfg.enabled ? "true" : "false") << ",\n"
         << "  \"input_method\": \"" << cfg.input_method << "\",\n"
         << "  \"output_charset\": \"" << cfg.output_charset << "\",\n"
         << "  \"toggle_shortcut\": \"" << cfg.toggle_shortcut << "\",\n"
         << "  \"emergency_stop\": " << (cfg.emergency_stop ? "true" : "false") << "\n"
         << "}\n";
}

// ============================================================
// inotify Watch Loop
// ============================================================

void ConfigWatcher::start(ConfigCallback callback) {
    if (m_running.load()) {
        std::cerr << "[config] WARNING: Already watching" << std::endl;
        return;
    }

    // Đọc config lần đầu và notify
    VnxConfig initial = read_config();
    if (callback) {
        callback(initial);
    }

    m_running.store(true);

    // Bắt đầu background thread
    m_watch_thread = std::thread([this, callback]() {
        watch_loop(callback);
    });
}

void ConfigWatcher::stop() {
    m_running.store(false);

    // Wake up inotify bằng cách close fd
    if (m_watch_fd >= 0) {
        inotify_rm_watch(m_inotify_fd, m_watch_fd);
        m_watch_fd = -1;
    }
    if (m_inotify_fd >= 0) {
        close(m_inotify_fd);
        m_inotify_fd = -1;
    }

    if (m_watch_thread.joinable()) {
        m_watch_thread.join();
    }
}

void ConfigWatcher::watch_loop(ConfigCallback callback) {
    // Tạo inotify instance
    m_inotify_fd = inotify_init1(IN_NONBLOCK);
    if (m_inotify_fd < 0) {
        std::cerr << "[config] ERROR: inotify_init1 failed: "
                  << strerror(errno) << std::endl;
        return;
    }

    // Watch thư mục chứa file (không watch file trực tiếp vì
    // nhiều editor save bằng cách tạo file mới rồi rename)
    std::string dir = std::filesystem::path(m_config_path).parent_path().string();

    m_watch_fd = inotify_add_watch(
        m_inotify_fd,
        dir.c_str(),
        IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE
    );

    if (m_watch_fd < 0) {
        // Thư mục chưa tồn tại - đợi
        std::cout << "[config] Config dir not found: " << dir
                  << " (will retry when available)" << std::endl;
        close(m_inotify_fd);
        m_inotify_fd = -1;
        return;
    }

    std::cout << "[config] Watching: " << dir << std::endl;

    // Buffer cho inotify events
    constexpr size_t BUF_LEN = 4096;
    char buffer[BUF_LEN];

    std::string config_filename = std::filesystem::path(m_config_path)
                                      .filename().string();

    while (m_running.load()) {
        // Dùng select để có timeout (không block mãi)
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(m_inotify_fd, &fds);

        struct timeval tv;
        tv.tv_sec = 1;  // Timeout 1 giây
        tv.tv_usec = 0;

        int rc = select(m_inotify_fd + 1, &fds, nullptr, nullptr, &tv);

        if (rc < 0) {
            if (errno == EINTR) continue; // Bị interrupt bởi signal, tiếp tục
            break;
        }

        if (rc == 0) {
            // Timeout - tiếp tục loop
            continue;
        }

        // Đọc events
        ssize_t len = read(m_inotify_fd, buffer, BUF_LEN);
        if (len < 0) {
            if (errno == EAGAIN) continue;
            break;
        }

        // Parse events
        char* ptr = buffer;
        while (ptr < buffer + len) {
            struct inotify_event* event = reinterpret_cast<struct inotify_event*>(ptr);

            // Kiểm tra xem có phải file config của ta không
            if (event->len > 0 && event->name == config_filename) {
                std::cout << "[config] Config file changed, reloading..." << std::endl;

                // Đọc lại config
                VnxConfig new_config = read_config();
                if (callback) {
                    callback(new_config);
                }
            }

            ptr += sizeof(struct inotify_event) + event->len;
        }
    }

    // Cleanup
    if (m_watch_fd >= 0) {
        inotify_rm_watch(m_inotify_fd, m_watch_fd);
        m_watch_fd = -1;
    }
    if (m_inotify_fd >= 0) {
        close(m_inotify_fd);
        m_inotify_fd = -1;
    }

    std::cout << "[config] Config watcher stopped" << std::endl;
}
