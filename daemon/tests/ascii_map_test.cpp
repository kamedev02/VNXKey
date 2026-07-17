#include <iostream>
#include <cassert>
#include <linux/input.h>

// Include uinput_handler.cpp directly to test its static function basic_ascii_to_keycode
// But first, we mock some types/functions so it compiles without needing the real UinputHandler
#define UINPUT_TESTING
#include "../src/output/uinput_handler.cpp"

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

int main() {
    std::cout << "================================================" << std::endl;
    std::cout << "   VNXKey ASCII Map Unit Tests                  " << std::endl;
    std::cout << "================================================" << std::endl;

    bool shift;
    
    // Test control chars
    test_case("'\\n' -> KEY_ENTER", basic_ascii_to_keycode('\n', shift) == KEY_ENTER && !shift);
    test_case("'\\b' -> KEY_BACKSPACE", basic_ascii_to_keycode('\b', shift) == KEY_BACKSPACE && !shift);
    test_case("'\\t' -> KEY_TAB", basic_ascii_to_keycode('\t', shift) == KEY_TAB && !shift);
    test_case("'\\x1b' -> KEY_ESC", basic_ascii_to_keycode('\x1b', shift) == KEY_ESC && !shift);
    
    // Test basic chars
    test_case("'a' -> KEY_A, shift=false", basic_ascii_to_keycode('a', shift) == KEY_A && !shift);
    test_case("'A' -> KEY_A, shift=true", basic_ascii_to_keycode('A', shift) == KEY_A && shift);
    test_case("'1' -> KEY_1, shift=false", basic_ascii_to_keycode('1', shift) == KEY_1 && !shift);
    
    // Test unshifted punctuation
    test_case("',' -> KEY_COMMA, shift=false", basic_ascii_to_keycode(',', shift) == KEY_COMMA && !shift);
    test_case("'.' -> KEY_DOT, shift=false", basic_ascii_to_keycode('.', shift) == KEY_DOT && !shift);
    test_case("'/' -> KEY_SLASH, shift=false", basic_ascii_to_keycode('/', shift) == KEY_SLASH && !shift);
    test_case("'-' -> KEY_MINUS, shift=false", basic_ascii_to_keycode('-', shift) == KEY_MINUS && !shift);
    test_case("'=' -> KEY_EQUAL, shift=false", basic_ascii_to_keycode('=', shift) == KEY_EQUAL && !shift);
    test_case("'[' -> KEY_LEFTBRACE, shift=false", basic_ascii_to_keycode('[', shift) == KEY_LEFTBRACE && !shift);
    test_case("']' -> KEY_RIGHTBRACE, shift=false", basic_ascii_to_keycode(']', shift) == KEY_RIGHTBRACE && !shift);
    test_case("'\\\\' -> KEY_BACKSLASH, shift=false", basic_ascii_to_keycode('\\', shift) == KEY_BACKSLASH && !shift);
    test_case("';' -> KEY_SEMICOLON, shift=false", basic_ascii_to_keycode(';', shift) == KEY_SEMICOLON && !shift);
    test_case("'\\'' -> KEY_APOSTROPHE, shift=false", basic_ascii_to_keycode('\'', shift) == KEY_APOSTROPHE && !shift);
    
    // Test shifted punctuation
    test_case("'!' -> KEY_1, shift=true", basic_ascii_to_keycode('!', shift) == KEY_1 && shift);
    test_case("'@' -> KEY_2, shift=true", basic_ascii_to_keycode('@', shift) == KEY_2 && shift);
    test_case("'#' -> KEY_3, shift=true", basic_ascii_to_keycode('#', shift) == KEY_3 && shift);
    test_case("'$' -> KEY_4, shift=true", basic_ascii_to_keycode('$', shift) == KEY_4 && shift);
    test_case("'%' -> KEY_5, shift=true", basic_ascii_to_keycode('%', shift) == KEY_5 && shift);
    test_case("'^' -> KEY_6, shift=true", basic_ascii_to_keycode('^', shift) == KEY_6 && shift);
    test_case("'&' -> KEY_7, shift=true", basic_ascii_to_keycode('&', shift) == KEY_7 && shift);
    test_case("'*' -> KEY_8, shift=true", basic_ascii_to_keycode('*', shift) == KEY_8 && shift);
    test_case("'(' -> KEY_9, shift=true", basic_ascii_to_keycode('(', shift) == KEY_9 && shift);
    test_case("')' -> KEY_0, shift=true", basic_ascii_to_keycode(')', shift) == KEY_0 && shift);
    test_case("'_' -> KEY_MINUS, shift=true", basic_ascii_to_keycode('_', shift) == KEY_MINUS && shift);
    test_case("'+' -> KEY_EQUAL, shift=true", basic_ascii_to_keycode('+', shift) == KEY_EQUAL && shift);
    test_case("'{' -> KEY_LEFTBRACE, shift=true", basic_ascii_to_keycode('{', shift) == KEY_LEFTBRACE && shift);
    test_case("'}' -> KEY_RIGHTBRACE, shift=true", basic_ascii_to_keycode('}', shift) == KEY_RIGHTBRACE && shift);
    test_case("'|' -> KEY_BACKSLASH, shift=true", basic_ascii_to_keycode('|', shift) == KEY_BACKSLASH && shift);
    test_case("':' -> KEY_SEMICOLON, shift=true", basic_ascii_to_keycode(':', shift) == KEY_SEMICOLON && shift);
    test_case("'\"' -> KEY_APOSTROPHE, shift=true", basic_ascii_to_keycode('"', shift) == KEY_APOSTROPHE && shift);
    test_case("'<' -> KEY_COMMA, shift=true", basic_ascii_to_keycode('<', shift) == KEY_COMMA && shift);
    test_case("'>' -> KEY_DOT, shift=true", basic_ascii_to_keycode('>', shift) == KEY_DOT && shift);
    test_case("'?' -> KEY_SLASH, shift=true", basic_ascii_to_keycode('?', shift) == KEY_SLASH && shift);

    std::cout << "\n================================================" << std::endl;
    std::cout << "Results: " << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    std::cout << "================================================" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
