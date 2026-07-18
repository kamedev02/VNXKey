/**
 * @file viet_engine.h
 * @brief Vietnamese Input Engine - Interface
 *
 * State machine xử lý logic tiếng Việt (Telex/VNI).
 * Phase 2: Dùng mapping thủ công cơ bản.
 * Phase 3+: Tích hợp unikey-core.
 */

#pragma once

#include <string>
#include <vector>

/**
 * @brief Loại action mà engine phát ra
 */
enum class ActionType {
    NONE,           ///< Không làm gì
    PASSTHROUGH,    ///< Chuyển tiếp phím gốc
    EMIT_CHAR,      ///< Phát 1 ký tự Unicode
    EMIT_BACKSPACE, ///< Phát phím Backspace
    COMMIT,         ///< Xác nhận chuỗi hiện tại và reset buffer
};

struct EngineAction {
    ActionType type;
    std::string data;   ///< Ký tự Unicode (nếu type == EMIT_CHAR)
    int count = 1;      ///< Số lần lặp (cho EMIT_BACKSPACE)
};

enum class InputMethod {
    TELEX,
    VNI,
    OFF,  ///< Passthrough - không xử lý tiếng Việt
};

/**
 * @brief Vietnamese Input Engine (Wrapper around Unikey Core)
 */
class VietEngine {
public:
    explicit VietEngine(InputMethod method = InputMethod::TELEX);
    ~VietEngine();

    VietEngine(const VietEngine&) = delete;
    VietEngine& operator=(const VietEngine&) = delete;

    /**
     * @brief Xử lý một ký tự ASCII đầu vào (đã được map từ keycode và phân giải hoa/thường)
     * @param ch Ký tự ASCII (ví dụ: 'a', 'A', '1', space...)
     * @return Danh sách các EngineAction cần thực hiện
     */
    std::vector<EngineAction> process_key(char ch);
    void reset();
    std::vector<EngineAction> flush(int chars_committed = -1);

    void set_method(InputMethod method);
    void set_enabled(bool enabled);
    bool is_enabled() const { return m_method != InputMethod::OFF; }

private:
    InputMethod m_method;
};

