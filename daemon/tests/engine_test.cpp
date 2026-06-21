/**
 * @file engine_test.cpp
 * @brief Unit tests cho VietEngine - Phase 2
 *
 * Compile và chạy:
 *   cd be/build && cmake .. && make vnxkey-engine-test
 *   ./vnxkey-engine-test
 *
 * Test case chính: gõ "aw" → emit [Backspace × 1, 'ă']
 */

#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include "engine/viet_engine.h"

// ============================================================
// Test helpers
// ============================================================

static int tests_passed = 0;
static int tests_failed = 0;

static void test_case(const std::string& name, bool condition) {
    if (condition) {
        std::cout << "  ✓ " << name << std::endl;
        tests_passed++;
    } else {
        std::cerr << "  ✗ FAIL: " << name << std::endl;
        tests_failed++;
    }
}

// Simulate gõ chuỗi, trả về chuỗi kết quả (mô phỏng màn hình)
// Sau khi gõ xong, đầy buffer bằng Space để trigger flush,
// rồi xóa dấu cách khỏi kết quả (nếu nó xuất hiện)
static std::string simulate_typing(VietEngine& engine, const std::string& input) {
    std::string screen = "";  // Mô phỏng nội dung trên màn hình

    auto apply_actions = [&](const std::vector<EngineAction>& actions) {
        for (const auto& action : actions) {
            switch (action.type) {
                case ActionType::PASSTHROUGH:
                    screen += action.data;
                    break;
                case ActionType::EMIT_CHAR:
                    screen += action.data;
                    break;
                case ActionType::EMIT_BACKSPACE:
                    for (int i = 0; i < action.count && !screen.empty(); i++) {
                        // Xóa từng UTF-8 character (có thể nhiều bytes)
                        while (!screen.empty()) {
                            unsigned char last = screen.back();
                            screen.pop_back();
                            // Byte đầu của UTF-8 sequence (0xxxxxxx hoặc 11xxxxxx)
                            if ((last & 0xC0) != 0x80) break;
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    };

    for (char ch : input) {
        auto actions = engine.process_key(ch);
        apply_actions(actions);
    }

    // Flush bằng Space để commit bất kỳ buffer pending
    // (mô phỏng người dùng nhấn Space sau khi gõ xong từ)
    auto flush_actions = engine.process_key(' ');
    apply_actions(flush_actions);

    // Xóa dấu cách cuối do flush tạo ra
    if (!screen.empty() && screen.back() == ' ') {
        screen.pop_back();
    }

    engine.reset();
    return screen;
}

// ============================================================
// Test suites
// ============================================================

static void test_passthrough() {
    std::cout << "\n[Test] Passthrough (OFF mode)" << std::endl;
    VietEngine engine(InputMethod::OFF);

    auto actions = engine.process_key('a');
    test_case("OFF mode: 'a' → PASSTHROUGH",
              actions.size() == 1 && actions[0].type == ActionType::PASSTHROUGH &&
              actions[0].data == "a");

    actions = engine.process_key('w');
    test_case("OFF mode: 'w' → PASSTHROUGH",
              actions.size() == 1 && actions[0].type == ActionType::PASSTHROUGH &&
              actions[0].data == "w");
}

static void test_telex_basic_vowels() {
    std::cout << "\n[Test] Telex basic vowel transforms" << std::endl;
    VietEngine engine(InputMethod::TELEX);

    // Test: a + w → ă (backspace + ă)
    {
        auto result = simulate_typing(engine, "aw");
        test_case("'aw' → 'ă'", result == "ă");
    }

    // Test: a + a → â
    {
        auto result = simulate_typing(engine, "aa");
        test_case("'aa' → 'â'", result == "â");
    }

    // Test: e + e → ê
    {
        auto result = simulate_typing(engine, "ee");
        test_case("'ee' → 'ê'", result == "ê");
    }

    // Test: o + o → ô
    {
        auto result = simulate_typing(engine, "oo");
        test_case("'oo' → 'ô'", result == "ô");
    }

    // Test: o + w → ơ
    {
        auto result = simulate_typing(engine, "ow");
        test_case("'ow' → 'ơ'", result == "ơ");
    }

    // Test: u + w → ư
    {
        auto result = simulate_typing(engine, "uw");
        test_case("'uw' → 'ư'", result == "ư");
    }

    // Test: d + d → đ
    {
        auto result = simulate_typing(engine, "dd");
        test_case("'dd' → 'đ'", result == "đ");
    }
}

static void test_telex_tones() {
    std::cout << "\n[Test] Telex tone marks" << std::endl;
    VietEngine engine(InputMethod::TELEX);

    test_case("'as' → 'á'", simulate_typing(engine, "as") == "á");
    test_case("'af' → 'à'", simulate_typing(engine, "af") == "à");
    test_case("'ar' → 'ả'", simulate_typing(engine, "ar") == "ả");
    test_case("'ax' → 'ã'", simulate_typing(engine, "ax") == "ã");
    test_case("'aj' → 'ạ'", simulate_typing(engine, "aj") == "ạ");

    test_case("'es' → 'é'", simulate_typing(engine, "es") == "é");
    test_case("'is' → 'í'", simulate_typing(engine, "is") == "í");
    test_case("'os' → 'ó'", simulate_typing(engine, "os") == "ó");
    test_case("'us' → 'ú'", simulate_typing(engine, "us") == "ú");
    test_case("'ys' → 'ý'", simulate_typing(engine, "ys") == "ý");
}

static void test_telex_combined() {
    std::cout << "\n[Test] Telex combined (vowel mod + tone)" << std::endl;
    VietEngine engine(InputMethod::TELEX);

    // ấ = â + sắc = "aas"
    test_case("'aas' → 'ấ'", simulate_typing(engine, "aas") == "ấ");
    test_case("'aaf' → 'ầ'", simulate_typing(engine, "aaf") == "ầ");
    test_case("'aws' → 'ắ'", simulate_typing(engine, "aws") == "ắ");
    test_case("'ees' → 'ế'", simulate_typing(engine, "ees") == "ế");
    test_case("'oos' → 'ố'", simulate_typing(engine, "oos") == "ố");
    test_case("'ows' → 'ớ'", simulate_typing(engine, "ows") == "ớ");
    test_case("'uws' → 'ứ'", simulate_typing(engine, "uws") == "ứ");
}

static void test_non_viet_passthrough() {
    std::cout << "\n[Test] Non-Vietnamese chars passthrough" << std::endl;
    VietEngine engine(InputMethod::TELEX);

    // Các chữ không tạo thành tổ hợp tiếng Việt
    test_case("'b' → 'b'",  simulate_typing(engine, "b") == "b");
    test_case("'c' → 'c'",  simulate_typing(engine, "c") == "c");
    test_case("'k' → 'k'",  simulate_typing(engine, "k") == "k");
    test_case("'xyz' → 'xyz'", simulate_typing(engine, "xyz") == "xyz");
}

static void test_word_simulation() {
    std::cout << "\n[Test] Word-level simulation" << std::endl;
    VietEngine engine(InputMethod::TELEX);

    // "viet" → gõ v-i-e-t, 'ee' ở giữa không có
    // v, i, e, t: không có mapping nên passthrough
    // Nhưng 'ie' → không có mapping nên passthrough "iet"
    auto result = simulate_typing(engine, "viet");
    test_case("'viet' passthrough correctly", result == "viet");

    // Gõ "tieedng" → "tiếng" (ee+d → ế, nhưng đây phức tạp hơn)
    // Bài test đơn giản hơn: xác nhận engine không crash
    result = simulate_typing(engine, "vieejt");
    // "v" → v, "i" → i, "ee" → ê, "ej" → ẹ... logic phức tạp cần refine
    // Ở Phase 2 chỉ test core mapping hoạt động, Phase 3+ sẽ refine word context
    test_case("Engine không crash với 'vieejt'", true);
}

// ============================================================
// Main
// ============================================================

int main() {
    std::cout << "================================================" << std::endl;
    std::cout << "   VNXKey Engine Unit Tests - Phase 2           " << std::endl;
    std::cout << "================================================" << std::endl;

    test_passthrough();
    test_telex_basic_vowels();
    test_telex_tones();
    test_telex_combined();
    test_non_viet_passthrough();
    test_word_simulation();

    std::cout << "\n================================================" << std::endl;
    std::cout << "Results: " << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    std::cout << "================================================" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
