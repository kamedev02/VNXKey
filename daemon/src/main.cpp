/**
 * @file main.cpp
 * @brief VNXKey Daemon - Entry point (Phase 3)
 *
 * === KEYBOARD SAFETY CRITICAL ===
 * Daemon này grab bàn phím vật lý. Nếu bị terminate mà không ungrab,
 * bàn phím bị treo. Hệ thống phòng vệ:
 *
 * 1. signal_handler: bắt SIGTERM/SIGINT → set g_running=false → main loop exit
 * 2. RAII: EvdevHandler destructor ungrab khi object out of scope
 * 3. g_evdev_ptr: signal handler có thể ungrab trực tiếp nếu cần emergency
 * 4. Không dùng exit() thô - luôn return từ main() để RAII hoạt động
 *
 * NOTE VỀ SIGKILL:
 * SIGKILL (-9) KHÔNG thể bị bắt - kernel kill process ngay lập tức.
 * Khi process bị SIGKILL, kernel sẽ tự close file descriptors,
 * và Linux kernel sẽ tự ungrab evdev khi fd bị close.
 * Tuy nhiên, để an toàn, TRÁNH dùng: sudo kill -9 <pid>
 * Thay vào đó dùng: sudo systemctl stop vnxkey (→ SIGTERM)
 */

#include <iostream>
#include <fstream>
#include <chrono>
#include <atomic>
#include <set>
#include <algorithm>
#include <csignal>
#include <thread>
#include <unistd.h>
#include <cstring>
#include <memory>
#include <vector>
#include <mutex>
#include <sys/epoll.h>
#include <linux/input-event-codes.h>  // KEY_A, KEY_BACKSPACE, etc.

#include "engine/viet_engine.h"
#include "ipc/config_watcher.h"

#ifdef HAVE_LIBEVDEV
#include "input/evdev_handler.h"
#include "output/uinput_handler.h"
#endif


// ============================================================
// Global state cho signal handler
// ============================================================

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_emergency_stop{false};

// Config tracking for toggle
static std::mutex g_config_mutex;
static VnxConfig g_current_config;

#ifdef HAVE_LIBEVDEV
// Danh sách con trỏ để ungrab khẩn cấp
static std::vector<EvdevHandler*> g_evdev_ptrs;
#endif

// ============================================================
// Signal Handler
// ============================================================

/**
 * @brief Signal handler cho SIGTERM, SIGINT, SIGHUP
 *
 * RULES cho signal handler:
 * - Chỉ dùng async-signal-safe functions
 * - Không: malloc, printf, cout, mutex lock
 * - Có thể: write(), atomic store, read global vars
 *
 * Cơ chế:
 * 1. Set g_running = false → main loop sẽ exit ở vòng tiếp theo
 * 2. Khi main() exit → RAII destructors chạy → ungrab() được gọi
 *
 * Emergency ungrab trong signal handler được tránh vì:
 * - libevdev_grab() không async-signal-safe
 * - RAII đảm bảo ungrab khi main() return
 */
static void signal_handler(int signum) {
    // Chỉ write() - async-signal-safe
    const char* msg;
    switch (signum) {
        case SIGTERM: msg = "\n[vnxkey] SIGTERM received. Stopping...\n"; break;
        case SIGINT:  msg = "\n[vnxkey] SIGINT received. Stopping...\n"; break;
        case SIGHUP:  msg = "\n[vnxkey] SIGHUP received. Stopping...\n"; break;
        default:      msg = "\n[vnxkey] Signal received. Stopping...\n"; break;
    }
    // Chỉ write() - async-signal-safe
    // Ignore return value để tránh warning (vì đang trong signal handler không làm gì được)
    if (write(STDERR_FILENO, msg, strlen(msg)) < 0) {
        // Lỗi ghi ra stderr, không thể làm gì hơn
    }

    // Signal main loop để dừng
    g_running.store(false, std::memory_order_relaxed);
}

static void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    // SA_RESTART: restart các syscall như read() khi bị interrupt
    // Tuy nhiên với epoll_wait ta muốn nó bị interrupt → dùng 0
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    // Ignore SIGPIPE (nếu socket/pipe bị đóng)
    signal(SIGPIPE, SIG_IGN);

    // NOTE: Không bắt SIGSEGV vì handler phức tạp và có thể gây deadlock.
    // Trong trường hợp SIGSEGV, kernel sẽ terminate process,
    // Linux kernel tự close fd → evdev bị ungrab tự động.
}

// ============================================================
// Keycode → char mapping (chỉ cho ASCII)
// ============================================================

/**
 * @brief Chuyển Linux keycode → ASCII char (lowercase)
 * Trả về 0 nếu không phải ký tự ASCII thường.
 */
#ifdef HAVE_LIBEVDEV
static char keycode_to_char(int keycode, bool shift, bool capslock) {
    bool is_alpha = (keycode >= KEY_A && keycode <= KEY_Z);
    bool effective_upper = shift;
    if (is_alpha && capslock) {
        effective_upper = !shift;
    }

    switch (keycode) {
        // Chữ cái a-z
        case KEY_A: return effective_upper ? 'A' : 'a';
        case KEY_B: return effective_upper ? 'B' : 'b';
        case KEY_C: return effective_upper ? 'C' : 'c';
        case KEY_D: return effective_upper ? 'D' : 'd';
        case KEY_E: return effective_upper ? 'E' : 'e';
        case KEY_F: return effective_upper ? 'F' : 'f';
        case KEY_G: return effective_upper ? 'G' : 'g';
        case KEY_H: return effective_upper ? 'H' : 'h';
        case KEY_I: return effective_upper ? 'I' : 'i';
        case KEY_J: return effective_upper ? 'J' : 'j';
        case KEY_K: return effective_upper ? 'K' : 'k';
        case KEY_L: return effective_upper ? 'L' : 'l';
        case KEY_M: return effective_upper ? 'M' : 'm';
        case KEY_N: return effective_upper ? 'N' : 'n';
        case KEY_O: return effective_upper ? 'O' : 'o';
        case KEY_P: return effective_upper ? 'P' : 'p';
        case KEY_Q: return effective_upper ? 'Q' : 'q';
        case KEY_R: return effective_upper ? 'R' : 'r';
        case KEY_S: return effective_upper ? 'S' : 's';
        case KEY_T: return effective_upper ? 'T' : 't';
        case KEY_U: return effective_upper ? 'U' : 'u';
        case KEY_V: return effective_upper ? 'V' : 'v';
        case KEY_W: return effective_upper ? 'W' : 'w';
        case KEY_X: return effective_upper ? 'X' : 'x';
        case KEY_Y: return effective_upper ? 'Y' : 'y';
        case KEY_Z: return effective_upper ? 'Z' : 'z';
        // Số
        case KEY_1: return shift ? '!' : '1';
        case KEY_2: return shift ? '@' : '2';
        case KEY_3: return shift ? '#' : '3';
        case KEY_4: return shift ? '$' : '4';
        case KEY_5: return shift ? '%' : '5';
        case KEY_6: return shift ? '^' : '6';
        case KEY_7: return shift ? '&' : '7';
        case KEY_8: return shift ? '*' : '8';
        case KEY_9: return shift ? '(' : '9';
        case KEY_0: return shift ? ')' : '0';
        
        // Ký tự đặc biệt (Punctuation)
        case KEY_MINUS:      return shift ? '_' : '-';
        case KEY_EQUAL:      return shift ? '+' : '=';
        case KEY_LEFTBRACE:  return shift ? '{' : '[';
        case KEY_RIGHTBRACE: return shift ? '}' : ']';
        case KEY_BACKSLASH:  return shift ? '|' : '\\';
        case KEY_SEMICOLON:  return shift ? ':' : ';';
        case KEY_APOSTROPHE: return shift ? '"' : '\'';
        case KEY_GRAVE:      return shift ? '~' : '`';
        case KEY_COMMA:      return shift ? '<' : ',';
        case KEY_DOT:        return shift ? '>' : '.';
        case KEY_SLASH:      return shift ? '?' : '/';
        
        // Điều khiển cơ bản
        case KEY_SPACE: return ' ';
        case KEY_ENTER: return '\n';
        case KEY_TAB:   return '\t';
        default: return 0; // Không phải ASCII
    }
}
#endif  // HAVE_LIBEVDEV

// ============================================================
// Event Deduplication
// ============================================================

struct LastEvent {
    int keycode = -1;
    int value = -1;
    std::chrono::steady_clock::time_point time;
};

static bool is_duplicate(LastEvent& last, int keycode, int value) {
    auto now = std::chrono::steady_clock::now();
    if (keycode == last.keycode && value == last.value) {
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - last.time).count();
        if (diff < 15) { // 15ms threshold là an toàn (human double tap > 50ms)
            return true;
        }
    }
    last.keycode = keycode;
    last.value = value;
    last.time = now;
    return false;
}

// ============================================================
// Main
// ============================================================


int main(int argc, char* argv[]) {
    std::cout << "[vnxkey] VNXKey Daemon v0.1.0 starting..." << std::endl;

    // ── Kiểm tra quyền root ──
    if (geteuid() != 0) {
        std::cerr << "[vnxkey] ERROR: Must run as root (need /dev/input access)" << std::endl;
        return 1;
    }

    // ── Setup signal handlers TRƯỚC mọi thứ ──
    // (Đặc biệt trước grab() để đảm bảo luôn có thể ungrab qua signal)
    setup_signal_handlers();

    // ── Khởi tạo Engine ──
    VietEngine engine(InputMethod::TELEX);
    std::cout << "[vnxkey] VietEngine initialized (TELEX mode)" << std::endl;

    // ── Khởi tạo Config Watcher ──
    ConfigWatcher::ensure_config_dir();
    ConfigWatcher config_watcher("/var/tmp/vnxkey/config.json");
    config_watcher.start([&engine](const VnxConfig& cfg) {
        std::cout << "[main] Config changed: enabled=" << cfg.enabled
                  << " method=" << cfg.input_method 
                  << " emergency=" << cfg.emergency_stop << std::endl;
        
        {
            std::lock_guard<std::mutex> lock(g_config_mutex);
            g_current_config = cfg;
        }
        
        g_emergency_stop.store(cfg.emergency_stop, std::memory_order_relaxed);

        // Update engine state
        if (!cfg.enabled) {
            engine.set_enabled(false);
        } else {
            engine.set_enabled(true);
            if (cfg.input_method == "telex") {
                engine.set_method(InputMethod::TELEX);
            } else if (cfg.input_method == "vni") {
                engine.set_method(InputMethod::VNI);
            }
        }
    });

#ifdef HAVE_LIBEVDEV
    // ── Tạo virtual keyboard (output) TRƯỚC khi grab (input) ──
    // Nếu tạo virtual keyboard thất bại, không grab để tránh deadlock
    UinputHandler uinput;
    if (!uinput.create_device()) {
        std::cerr << "[vnxkey] ERROR: Cannot create virtual keyboard. Aborting." << std::endl;
        return 1;
    }

    // ── Tìm và mở tất cả keyboard vật lý ──
    std::vector<std::string> kb_paths = EvdevHandler::find_keyboards();
    if (kb_paths.empty()) {
        std::cerr << "[vnxkey] ERROR: No keyboard found in /dev/input/" << std::endl;
        return 1;
    }

    std::vector<std::unique_ptr<EvdevHandler>> evdev_list;
    for (const auto& path : kb_paths) {
        auto evdev = std::make_unique<EvdevHandler>();
        if (evdev->open_device(path)) {
            if (evdev->grab()) {
                g_evdev_ptrs.push_back(evdev.get());
                evdev_list.push_back(std::move(evdev));
            } else {
                std::cerr << "[vnxkey] WARNING: Cannot grab " << path << std::endl;
            }
        }
    }

    if (evdev_list.empty()) {
        std::cerr << "[vnxkey] ERROR: Could not grab any keyboard. Aborting." << std::endl;
        return 1;
    }

    // ── Tracking modifier keys ──
    bool shift_pressed = false;
    bool ctrl_pressed  = false;
    bool alt_pressed   = false;
    std::set<int> physical_pressed_keys;

    std::cout << "[vnxkey] Running. Grabbed " << evdev_list.size() << " keyboards. Use Ctrl+C to stop." << std::endl;

    // ── Main event loop ──
    // Dùng epoll để đọc events hiệu quả hơn busy-waiting
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        std::cerr << "[vnxkey] ERROR: epoll_create1 failed" << std::endl;
        return 1;
    }

    // TODO: Phase 4 sẽ refactor để expose fd cho epoll
    close(epfd);

    // Simple polling loop
    bool currently_grabbed = true;
    LastEvent last_event;
    bool ctrl_shift_ready = false;
    
    auto last_hotplug_scan = std::chrono::steady_clock::now();

    while (g_running.load(std::memory_order_relaxed)) {
        bool em_stop = g_emergency_stop.load(std::memory_order_relaxed);
        
        // Xử lý Emergency Stop: nhả tất cả bàn phím
        if (em_stop && currently_grabbed) {
            std::cout << "[vnxkey] EMERGENCY STOP activated! Ungrabbing all keyboards." << std::endl;
            for (auto& evdev : evdev_list) evdev->ungrab();
            currently_grabbed = false;
            
            // [CRITICAL] Reset toàn bộ trạng thái để tránh kẹt phím khi bật lại
            physical_pressed_keys.clear();
            shift_pressed = false;
            ctrl_pressed = false;
            alt_pressed = false;
            engine.reset();
        } else if (!em_stop && !currently_grabbed) {
            std::cout << "[vnxkey] EMERGENCY STOP deactivated. Re-grabbing keyboards." << std::endl;
            for (auto& evdev : evdev_list) evdev->grab();
            currently_grabbed = true;
        }

        if (!currently_grabbed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            // Discard pending events khi đang emergency stop
            for (auto& evdev : evdev_list) {
                KeyEvent kev;
                while (evdev->read_event(kev) == ReadStatus::HAS_EVENT) {} 
            }
            continue;
        }

        bool got_any_event = false;
        
        for (auto it = evdev_list.begin(); it != evdev_list.end(); ) {
            auto& evdev = *it;
            KeyEvent kev;
            ReadStatus status;
            bool device_removed = false;

            while ((status = evdev->read_event(kev)) == ReadStatus::HAS_EVENT) {
                got_any_event = true;

                // Deduplicate events giống nhau (do có nhiều interface cho cùng 1 bàn phím vật lý)
                if (is_duplicate(last_event, kev.keycode, kev.value)) {
                    continue; // Bỏ qua duplicate
                }

                // Chỉ xử lý key press (value=1) và repeat (value=2)
                // Key release (value=0)
                if (kev.value == 0) {
                    physical_pressed_keys.erase(kev.keycode);
                    
                    bool was_ctrl_shift = (kev.keycode == KEY_LEFTSHIFT || kev.keycode == KEY_RIGHTSHIFT || 
                                           kev.keycode == KEY_LEFTCTRL || kev.keycode == KEY_RIGHTCTRL);

                    // [WORKING][CRITICAL] Check release trigger for Ctrl+Shift (E/V Toggle) - DO NOT MODIFY UNLESS NECESSARY
                    if (was_ctrl_shift && ctrl_shift_ready) {
                        std::string current_shortcut;
                        {
                            std::lock_guard<std::mutex> lock(g_config_mutex);
                            current_shortcut = g_current_config.toggle_shortcut;
                        }
                        if (current_shortcut == "Ctrl+Shift") {
                            std::cout << "[vnxkey] Toggle shortcut (Ctrl+Shift) triggered on release!" << std::endl;
                            VnxConfig new_cfg;
                            {
                                std::lock_guard<std::mutex> lock(g_config_mutex);
                                new_cfg = g_current_config;
                            }
                            new_cfg.enabled = !new_cfg.enabled;
                            config_watcher.write_config(new_cfg);
                        }
                    }
                    if (was_ctrl_shift) {
                        ctrl_shift_ready = false;
                    }

                    if (kev.keycode == KEY_LEFTSHIFT || kev.keycode == KEY_RIGHTSHIFT) {
                        shift_pressed = false;
                    } else if (kev.keycode == KEY_LEFTCTRL || kev.keycode == KEY_RIGHTCTRL) {
                        ctrl_pressed = false;
                    } else if (kev.keycode == KEY_LEFTALT || kev.keycode == KEY_RIGHTALT) {
                        alt_pressed = false;
                    }
                    uinput.emit_key(kev.keycode, 0); // PHẢI PHÁT LẠI RELEASE CHO OS NHẬN!
                    continue;
                }

                // Từ đây trở đi là press hoặc repeat
                physical_pressed_keys.insert(kev.keycode);

                // Track modifier keys and Shortcut Toggle (E/V)
                bool is_modifier = false;
                if (kev.keycode == KEY_LEFTSHIFT || kev.keycode == KEY_RIGHTSHIFT) {
                    shift_pressed = true;
                    is_modifier = true;
                } else if (kev.keycode == KEY_LEFTCTRL || kev.keycode == KEY_RIGHTCTRL) {
                    ctrl_pressed = true;
                    is_modifier = true;
                    engine.reset();
                } else if (kev.keycode == KEY_LEFTALT || kev.keycode == KEY_RIGHTALT) {
                    alt_pressed = true;
                    is_modifier = true;
                    engine.reset();
                }

                // [WORKING][CRITICAL] Shortcut Toggle (E/V) interception - DO NOT MODIFY UNLESS NECESSARY
                if (kev.value == 1) { // Only on key press
                    if (is_modifier) {
                        if (ctrl_pressed && shift_pressed && !alt_pressed) {
                            if (physical_pressed_keys.size() == 2) {
                                ctrl_shift_ready = true;
                            } else {
                                ctrl_shift_ready = false;
                            }
                        } else {
                            ctrl_shift_ready = false;
                        }
                    } else {
                        ctrl_shift_ready = false;
                    }

                    bool shortcut_triggered = false;
                    std::string current_shortcut;
                    {
                        std::lock_guard<std::mutex> lock(g_config_mutex);
                        current_shortcut = g_current_config.toggle_shortcut;
                    }
                    
                    if (current_shortcut == "Alt+Z" && alt_pressed && kev.keycode == KEY_Z) {
                        shortcut_triggered = true;
                    } else if (current_shortcut == "Ctrl+Space" && ctrl_pressed && kev.keycode == KEY_SPACE) {
                        shortcut_triggered = true;
                    }

                    if (shortcut_triggered) {
                        std::cout << "[vnxkey] Toggle shortcut triggered! current state=" << g_current_config.enabled << std::endl;
                        // Toggle enabled state
                        VnxConfig new_cfg;
                        {
                            std::lock_guard<std::mutex> lock(g_config_mutex);
                            new_cfg = g_current_config;
                        }
                        new_cfg.enabled = !new_cfg.enabled;
                        config_watcher.write_config(new_cfg);
                        
                        // consume key for Alt+Z and Ctrl+Space
                        continue;
                    }
                }

                if (is_modifier) {
                    uinput.emit_key(kev.keycode, kev.value);
                    continue;
                }

                // Backspace: phím đặc biệt - passthrough nhưng reset buffer
                if (kev.keycode == KEY_BACKSPACE) {
                    engine.reset();
                    uinput.emit_key(KEY_BACKSPACE, kev.value);
                    continue;
                }

                // Nếu đang nhấn Ctrl, passthrough tất cả (shortcuts)
                if (ctrl_pressed) {
                    uinput.emit_key(kev.keycode, kev.value);
                    continue;
                }

                // Nếu là phím repeat (giữ phím), passthrough trực tiếp để không đẩy vào engine gây lỗi lặp ký tự
                if (kev.value == 2) {
                    uinput.emit_key(kev.keycode, kev.value);
                    continue;
                }

                // Giao cho VietEngine xử lý
                bool is_capslock = evdev->is_capslock_on();
                
                // Chuyển keycode → char
                char ch = keycode_to_char(kev.keycode, shift_pressed, is_capslock);

                if (ch == 0) {
                    // Không phải ASCII (F1, Home, mũi tên...) → reset buffer và passthrough
                    engine.reset();
                    uinput.emit_key(kev.keycode, kev.value);
                    continue;
                }

                // [WORKING][CRITICAL] Đọc trạng thái excluded apps từ tmpfs (do GUI ghi xuống) - DO NOT MODIFY UNLESS NECESSARY
                static bool cached_is_excluded = false;
                static auto last_exclude_check = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();

                // Chỉ check lại file mỗi 500ms để giảm overhead
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_exclude_check).count() > 500) {
                    last_exclude_check = now;
                    cached_is_excluded = false;
                    std::ifstream ext_file("/dev/shm/vnxkey_excluded");
                    if (ext_file.is_open()) {
                        std::string content;
                        ext_file >> content;
                        if (content == "1") {
                            cached_is_excluded = true;
                        }
                    }
                }

                if (cached_is_excluded) {
                    // Nếu cửa sổ đang active bị ngoại trừ, bỏ qua buffer và gửi phím trực tiếp
                    engine.reset();
                    uinput.emit_key(kev.keycode, kev.value);
                    continue;
                }

                auto actions = engine.process_key(ch);

                // Thực thi actions
                bool needs_release = false;
                for (const auto& action : actions) {
                    if (action.type == ActionType::EMIT_CHAR || action.type == ActionType::EMIT_BACKSPACE) {
                        needs_release = true;
                        break;
                    }
                }

                auto is_modifier_key = [](int k) {
                    return k == KEY_LEFTSHIFT || k == KEY_RIGHTSHIFT ||
                           k == KEY_LEFTCTRL || k == KEY_RIGHTCTRL ||
                           k == KEY_LEFTALT || k == KEY_RIGHTALT ||
                           k == KEY_LEFTMETA || k == KEY_RIGHTMETA;
                };

                if (needs_release) {
                    // Tạm thời nhả TẤT CẢ các phím đang giữ để không kẹt GTK sequence (Ctrl+Shift+U)
                    // và để các phím chữ cái (như 'a', 'u') không bị kẹt khi hệ thống gõ mã hex.
                    for (int k : physical_pressed_keys) {
                        uinput.emit_key(k, 0);
                    }
                }

                for (const auto& action : actions) {
                    switch (action.type) {
                        case ActionType::PASSTHROUGH:
                            uinput.emit_key(kev.keycode, kev.value);
                            break;
                        case ActionType::EMIT_CHAR:
                            uinput.emit_unicode(action.data, is_capslock);
                            break;
                        case ActionType::EMIT_BACKSPACE:
                            uinput.emit_backspace(action.count);
                            break;
                        case ActionType::NONE:
                        case ActionType::COMMIT:
                            break;
                    }
                }

                if (needs_release) {
                    // Phục hồi lại trạng thái các phím MODIFIER đang giữ
                    for (int k : physical_pressed_keys) {
                        if (is_modifier_key(k)) {
                            uinput.emit_key(k, 1);
                        }
                    }
                }
            } // end while read_event

            if (status == ReadStatus::DISCONNECTED) {
                // Xoá pointer khỏi g_evdev_ptrs trước
                g_evdev_ptrs.erase(
                    std::remove(g_evdev_ptrs.begin(), g_evdev_ptrs.end(), evdev.get()),
                    g_evdev_ptrs.end()
                );
                // Xoá khỏi evdev_list
                it = evdev_list.erase(it);
                device_removed = true;
            }

            if (!device_removed) {
                ++it;
            }
        } // end for evdev_list

        // --- Bắt đầu phần Hotplug (2 giây một lần) ---
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_hotplug_scan).count() >= 2) {
            last_hotplug_scan = now;
            auto new_kb_paths = EvdevHandler::find_keyboards();
            for (const auto& path : new_kb_paths) {
                bool found = false;
                for (const auto& existing : evdev_list) {
                    if (existing->get_device_path() == path) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    std::cout << "[vnxkey] Hotplug detected new keyboard: " << path << std::endl;
                    auto new_evdev = std::make_unique<EvdevHandler>();
                    if (new_evdev->open_device(path)) {
                        if (currently_grabbed && !new_evdev->grab()) {
                            std::cerr << "[vnxkey] WARNING: Cannot grab new keyboard " << path << std::endl;
                        } else {
                            g_evdev_ptrs.push_back(new_evdev.get());
                            evdev_list.push_back(std::move(new_evdev));
                        }
                    }
                }
            }
        }
        // --- Kết thúc phần Hotplug ---

        if (!got_any_event) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // ── Cleanup ──
    g_evdev_ptrs.clear();
    // evdev destructor chạy ở đây → ungrab() tự động
    // uinput destructor chạy ở đây → destroy_device() tự động

#else
    // Không có libevdev → chạy như stub
    std::cout << "[vnxkey] libevdev not available. Install: sudo apt install libevdev-dev" << std::endl;
    std::cout << "[vnxkey] Running in stub mode (no keyboard interception)" << std::endl;

    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#endif

    config_watcher.stop();
    std::cout << "[vnxkey] Daemon stopped cleanly. Keyboard restored." << std::endl;
    return 0;
}
