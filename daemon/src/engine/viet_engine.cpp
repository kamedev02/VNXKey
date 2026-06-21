#include "viet_engine.h"
#include "unikey.h"
#include "vnconv.h"
#include <iostream>
#include <cstdint>

// Hàm tiện ích: Chuyển UTF-16 (không có surrogate pairs) sang UTF-8
static std::string utf16_to_utf8(const uint16_t* utf16_str, int len) {
    std::string utf8_str;
    for (int i = 0; i < len; ++i) {
        uint16_t cp = utf16_str[i];
        if (cp < 0x80) {
            utf8_str += static_cast<char>(cp);
        } else if (cp < 0x800) {
            utf8_str += static_cast<char>(0xC0 | (cp >> 6));
            utf8_str += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            utf8_str += static_cast<char>(0xE0 | (cp >> 12));
            utf8_str += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8_str += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return utf8_str;
}

VietEngine::VietEngine(InputMethod method) : m_method(method) {
    UnikeySetup();
    // Dùng UNICODE (UTF-16 LE) thay vì UTF-8 để UnikeyBackspaces tính đúng số *kí tự* cần xoá
    UnikeySetOutputCharset(0); // CONV_CHARSET_UNICODE là 0 (định nghĩa trong vnconv.h)
    
    // Áp dụng method
    set_method(method);
}

VietEngine::~VietEngine() {
    UnikeyCleanup();
}

void VietEngine::set_method(InputMethod method) {
    m_method = method;
    if (method == InputMethod::TELEX) {
        UnikeySetInputMethod(UkTelex);
    } else if (method == InputMethod::VNI) {
        UnikeySetInputMethod(UkVni);
    }
}

void VietEngine::set_enabled(bool enabled) {
    if (!enabled) {
        m_method = InputMethod::OFF;
    } else if (m_method == InputMethod::OFF) {
        set_method(InputMethod::TELEX); // Mặc định về Telex nếu bật lại
    }
}

void VietEngine::reset() {
    UnikeyResetBuf();
}

std::vector<EngineAction> VietEngine::flush(int /*chars_committed*/) {
    // Unikey tự động flush khi gõ dấu cách hoặc non-alphabetic
    // Nên không cần flush thủ công
    return {};
}

std::vector<EngineAction> VietEngine::process_key(char ch) {
    std::vector<EngineAction> actions;

    if (m_method == InputMethod::OFF) {
        actions.push_back({ActionType::PASSTHROUGH, std::string(1, ch), 1});
        return actions;
    }

    if (ch == '\b') {
        UnikeyBackspacePress();
    } else {
        // Cập nhật trạng thái CapsLock/Shift (TODO: lấy từ evdev nếu cần)
        UnikeySetCapsState(0, 0); 
        UnikeyFilter((unsigned char)ch);
    }

    if (UnikeyBackspaces > 0) {
        actions.push_back({ActionType::EMIT_BACKSPACE, "", UnikeyBackspaces});
    }

    // 2. Kí tự sinh ra
    // Khi dùng CONV_CHARSET_UNICODE, UnikeyBuf chứa các kí tự UTF-16 LE (mỗi kí tự 2 bytes)
    // UnikeyBufChars là số byte trong buffer (chứ không phải số kí tự)
    // Vì vậy số kí tự UTF-16 = UnikeyBufChars / 2
    if (UnikeyBufChars > 0) {
        int utf16_len = UnikeyBufChars / 2;
        std::string out_str = utf16_to_utf8(reinterpret_cast<uint16_t*>(UnikeyBuf), utf16_len);
        actions.push_back({ActionType::EMIT_CHAR, out_str, 1});
    } else if (UnikeyBackspaces == 0 && ch != '\b') {
        // Unikey không thay đổi nội dung, nghĩa là nó muốn OS tự in phím vừa gõ
        actions.push_back({ActionType::PASSTHROUGH, std::string(1, ch), 1});
    }

    return actions;
}
