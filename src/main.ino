#include <Arduino.h>
#include <Keyboard.h>
#include <Bounce2.h>
#include <EEPROM.h>
#define EMBEDDED_CLI_IMPL
#include "embedded_cli.h"
#define DEFAULT_STRING "Hello World!"

Bounce debouncer = Bounce();
#define BUTTON 13
#define MAGIC 0xDEADBEEF
#define MAX_MACRO_LEN 1000
#define EEPROM_SIZE 2048 /* arbitrary... but plenty big */
#define LONG_PRESS_MS 500  // Threshold for long press in milliseconds
#define EXTRA_LONG_PRESS_MS 10000  // Threshold for extra long press (bootloader mode) in milliseconds
struct Config {
    uint32_t magic;
    char macro[MAX_MACRO_LEN];
};

void eeprom_clear();

Config cfg;
EmbeddedCli *cli;
unsigned long buttonPressTime = 0;
bool buttonPressed = false;

// ---------------------------
// Macro parser / encoder
// ---------------------------
// Supported in cfg.macro:
// - **Tokens**: {ENTER}, {TAB}, {ESC}, {BACKSPACE}, {DELETE}, {UP}, {DOWN}, {LEFT}, {RIGHT},
//              {HOME}, {END}, {PGUP}, {PGDN}, {INSERT}, {F1}..{F24}, {SPACE}
// - **Chords**: {CTRL+C}, {CTRL+SHIFT+ESC}, {ALT+F4}, {GUI+R}, etc. (case-insensitive)
// - **Escapes**: \\n \\r \\t \\b \\e \\\\ \\xNN
// - **Caret control**: ^C (Ctrl+C), ^[ (Esc), ^^ (literal '^'), ^? (Delete)
//
// NOTE: We also accept raw ASCII control bytes (e.g. 0x03) and interpret them as control keys.

static inline bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static inline uint8_t hex_val(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(10 + (c - 'a'));
    return (uint8_t)(10 + (c - 'A'));
}

static void keyboard_tap(uint8_t k) {
    Keyboard.write(k);
}

static void keyboard_chord(const uint8_t *mods, size_t mod_count, uint8_t key) {
    for (size_t i = 0; i < mod_count; i++) {
        Keyboard.press(mods[i]);
    }
    Keyboard.press(key);
    delay(5);
    Keyboard.release(key);
    for (size_t i = 0; i < mod_count; i++) {
        Keyboard.release(mods[i]);
    }
}

static bool token_equals_ci(const char *s, size_t n, const char *lit) {
    // Case-insensitive equality check for [s, s+n) vs null-terminated lit
    size_t i = 0;
    for (; i < n && lit[i]; i++) {
        char a = s[i];
        char b = lit[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return false;
    }
    return (i == n) && (lit[i] == '\0');
}

static bool parse_named_key_ci(const char *s, size_t n, uint8_t &out_key) {
    if (token_equals_ci(s, n, "ENTER") || token_equals_ci(s, n, "RETURN")) { out_key = KEY_RETURN; return true; }
    if (token_equals_ci(s, n, "TAB")) { out_key = KEY_TAB; return true; }
    if (token_equals_ci(s, n, "ESC") || token_equals_ci(s, n, "ESCAPE")) { out_key = KEY_ESC; return true; }
    if (token_equals_ci(s, n, "BS") || token_equals_ci(s, n, "BACKSPACE")) { out_key = KEY_BACKSPACE; return true; }
    if (token_equals_ci(s, n, "DEL") || token_equals_ci(s, n, "DELETE")) { out_key = KEY_DELETE; return true; }
    if (token_equals_ci(s, n, "INS") || token_equals_ci(s, n, "INSERT")) { out_key = KEY_INSERT; return true; }
    if (token_equals_ci(s, n, "HOME")) { out_key = KEY_HOME; return true; }
    if (token_equals_ci(s, n, "END")) { out_key = KEY_END; return true; }
    if (token_equals_ci(s, n, "PGUP") || token_equals_ci(s, n, "PAGEUP") || token_equals_ci(s, n, "PAGE_UP")) { out_key = KEY_PAGE_UP; return true; }
    if (token_equals_ci(s, n, "PGDN") || token_equals_ci(s, n, "PAGEDOWN") || token_equals_ci(s, n, "PAGE_DOWN")) { out_key = KEY_PAGE_DOWN; return true; }
    if (token_equals_ci(s, n, "UP")) { out_key = KEY_UP_ARROW; return true; }
    if (token_equals_ci(s, n, "DOWN")) { out_key = KEY_DOWN_ARROW; return true; }
    if (token_equals_ci(s, n, "LEFT")) { out_key = KEY_LEFT_ARROW; return true; }
    if (token_equals_ci(s, n, "RIGHT")) { out_key = KEY_RIGHT_ARROW; return true; }
    if (token_equals_ci(s, n, "SPACE")) { out_key = (uint8_t)' '; return true; }
    if (token_equals_ci(s, n, "MENU") || token_equals_ci(s, n, "APP")) { out_key = KEY_MENU; return true; }

    // Function keys: F1..F24
    if (n >= 2 && (s[0] == 'F' || s[0] == 'f')) {
        int num = 0;
        for (size_t i = 1; i < n; i++) {
            if (s[i] < '0' || s[i] > '9') return false;
            num = num * 10 + (s[i] - '0');
        }
        if (num >= 1 && num <= 24) {
            out_key = (uint8_t)(KEY_F1 + (num - 1));
            return true;
        }
    }
    return false;
}

static bool parse_modifier_ci(const char *s, size_t n, uint8_t &out_mod) {
    if (token_equals_ci(s, n, "CTRL") || token_equals_ci(s, n, "CONTROL")) { out_mod = KEY_LEFT_CTRL; return true; }
    if (token_equals_ci(s, n, "SHIFT")) { out_mod = KEY_LEFT_SHIFT; return true; }
    if (token_equals_ci(s, n, "ALT") || token_equals_ci(s, n, "OPTION")) { out_mod = KEY_LEFT_ALT; return true; }
    if (token_equals_ci(s, n, "GUI") || token_equals_ci(s, n, "WIN") || token_equals_ci(s, n, "WINDOWS") ||
        token_equals_ci(s, n, "CMD") || token_equals_ci(s, n, "COMMAND") || token_equals_ci(s, n, "META") ||
        token_equals_ci(s, n, "SUPER")) { out_mod = KEY_LEFT_GUI; return true; }
    return false;
}

static void send_ctrl_char(uint8_t c) {
    // c is ASCII control value 0..31 or 127.
    if (c == 0x7F) {
        keyboard_tap(KEY_DELETE);
        return;
    }
    switch (c) {
    case 0x08: keyboard_tap(KEY_BACKSPACE); return; // BS
    case 0x09: keyboard_tap(KEY_TAB); return;       // TAB
    case 0x0A: keyboard_tap(KEY_RETURN); return;    // LF -> Enter
    case 0x0D: keyboard_tap(KEY_RETURN); return;    // CR -> Enter
    case 0x1B: keyboard_tap(KEY_ESC); return;       // ESC
    default:
        break;
    }
    if (c == 0x00) {
        // Ctrl+Space is sometimes treated as NUL; best-effort send Ctrl+' '.
        uint8_t mods[] = { KEY_LEFT_CTRL };
        keyboard_chord(mods, 1, (uint8_t)' ');
        return;
    }
    if (c >= 0x01 && c <= 0x1A) {
        char letter = (char)('a' + (c - 1));
        uint8_t mods[] = { KEY_LEFT_CTRL };
        keyboard_chord(mods, 1, (uint8_t)letter);
        return;
    }
    // Fallback: encode as Ctrl + (c + 64) if printable-ish
    uint8_t mods[] = { KEY_LEFT_CTRL };
    keyboard_chord(mods, 1, (uint8_t)('@' + c));
}

static void serial_print_encoded_macro(const char *s) {
    for (size_t i = 0; s[i] != '\0'; i++) {
        const uint8_t c = (uint8_t)s[i];
        // Keep backslashes as-is so users can round-trip macro escape sequences like "\n"
        // without them turning into "\\n" when displayed.
        if (c == '\n') {
            Serial.print("\\n");
        } else if (c == '\r') {
            Serial.print("\\r");
        } else if (c == '\t') {
            Serial.print("\\t");
        } else if (c == '\b') {
            Serial.print("\\b");
        } else if (c == 0x1B) {
            Serial.print("\\e");
        } else if (c == 0x7F) {
            Serial.print("^?");
        } else if (c < 0x20) {
            Serial.print('^');
            Serial.print((char)(c == 0 ? '@' : ('A' + (c - 1))));
        } else if (c >= 0x20 && c < 0x7F) {
            Serial.print((char)c);
        } else {
            // Non-ASCII byte. Keep it escaped so CLI/web UI stays sane.
            const char hex[] = "0123456789abcdef";
            Serial.print("\\x");
            Serial.print(hex[(c >> 4) & 0xF]);
            Serial.print(hex[c & 0xF]);
        }
    }
}

static bool try_send_token(const char *token, size_t n) {
    // Trim spaces around token
    while (n && (*token == ' ' || *token == '\t')) { token++; n--; }
    while (n && (token[n - 1] == ' ' || token[n - 1] == '\t')) { n--; }
    if (n == 0) return false;

    // DELAY:NNN
    if (n >= 6 && (token_equals_ci(token, 6, "DELAY:") || token_equals_ci(token, 5, "WAIT:"))) {
        const char *p = token;
        size_t start = 0;
        if (token_equals_ci(token, 6, "DELAY:")) start = 6;
        else start = 5;
        uint32_t ms = 0;
        for (size_t i = start; i < n; i++) {
            if (token[i] < '0' || token[i] > '9') return false;
            ms = ms * 10u + (uint32_t)(token[i] - '0');
        }
        delay(ms);
        return true;
    }

    // Split by '+': modifiers + final key
    uint8_t mods[4];
    size_t mod_count = 0;

    size_t part_start = 0;
    uint8_t key = 0;
    bool have_key = false;

    for (size_t i = 0; i <= n; i++) {
        const bool at_end = (i == n);
        if (!at_end && token[i] != '+') continue;

        const size_t part_len = i - part_start;
        const char *part = token + part_start;

        // Trim part
        size_t ps = 0, pe = part_len;
        while (ps < pe && (part[ps] == ' ' || part[ps] == '\t')) ps++;
        while (pe > ps && (part[pe - 1] == ' ' || part[pe - 1] == '\t')) pe--;

        const char *p2 = part + ps;
        const size_t n2 = pe - ps;

        if (n2 == 0) return false;

        uint8_t mod;
        uint8_t named;
        if (!at_end && parse_modifier_ci(p2, n2, mod)) {
            if (mod_count < sizeof(mods)) mods[mod_count++] = mod;
        } else if (parse_modifier_ci(p2, n2, mod)) {
            // Last element can still be a modifier; treat as "tap modifier" (press+release).
            Keyboard.press(mod);
            delay(5);
            Keyboard.release(mod);
            return true;
        } else if (parse_named_key_ci(p2, n2, named)) {
            key = named;
            have_key = true;
        } else if (n2 == 1) {
            // Single character key (e.g. "c", "R")
            char ch = p2[0];
            if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
            key = (uint8_t)ch;
            have_key = true;
        } else if (n2 == 2 && p2[0] == '0' && (p2[1] == 'x' || p2[1] == 'X')) {
            return false;
        } else {
            // Unsupported token part
            return false;
        }

        part_start = i + 1;
    }

    if (!have_key) {
        return false;
    }

    if (mod_count) {
        keyboard_chord(mods, mod_count, key);
    } else {
        keyboard_tap(key);
    }
    return true;
}

static void send_macro(const char *s) {
    for (size_t i = 0; s[i] != '\0'; ) {
        const uint8_t c = (uint8_t)s[i];

        // Brace escaping: "{{" -> "{", "}}" -> "}"
        if (c == '{' && s[i + 1] == '{') {
            keyboard_tap((uint8_t)'{');
            i += 2;
            continue;
        }
        if (c == '}' && s[i + 1] == '}') {
            keyboard_tap((uint8_t)'}');
            i += 2;
            continue;
        }

        // Token: {...}
        if (c == '{') {
            size_t j = i + 1;
            while (s[j] != '\0' && s[j] != '}') j++;
            if (s[j] == '}') {
                const char *tok = s + (i + 1);
                const size_t tok_len = j - (i + 1);
                if (try_send_token(tok, tok_len)) {
                    i = j + 1;
                    continue;
                }
                // If token parse fails, fall through and type literally.
            }
        }

        // Backslash escapes
        if (c == '\\') {
            const char n = s[i + 1];
            if (n == '\0') {
                keyboard_tap((uint8_t)'\\');
                i++;
                continue;
            }
            if (n == 'n') { keyboard_tap(KEY_RETURN); i += 2; continue; }
            if (n == 'r') { keyboard_tap(KEY_RETURN); i += 2; continue; }
            if (n == 't') { keyboard_tap(KEY_TAB); i += 2; continue; }
            if (n == 'b') { keyboard_tap(KEY_BACKSPACE); i += 2; continue; }
            if (n == 'e') { keyboard_tap(KEY_ESC); i += 2; continue; }
            if (n == '\\') { keyboard_tap((uint8_t)'\\'); i += 2; continue; }
            if (n == '{') { keyboard_tap((uint8_t)'{'); i += 2; continue; }
            if (n == '}') { keyboard_tap((uint8_t)'}'); i += 2; continue; }
            if (n == 'x' && is_hex(s[i + 2]) && is_hex(s[i + 3])) {
                const uint8_t v = (hex_val(s[i + 2]) << 4) | hex_val(s[i + 3]);
                if (v < 0x20 || v == 0x7F) {
                    send_ctrl_char(v);
                } else {
                    keyboard_tap(v);
                }
                i += 4;
                continue;
            }
            // Unknown escape: type '\' literally.
            keyboard_tap((uint8_t)'\\');
            i++;
            continue;
        }

        // Caret notation: ^C, ^[, ^^, ^?
        if (c == '^') {
            const char n = s[i + 1];
            if (n == '\0') {
                keyboard_tap((uint8_t)'^');
                i++;
                continue;
            }
            if (n == '^') {
                keyboard_tap((uint8_t)'^');
                i += 2;
                continue;
            }
            if (n == '?') {
                keyboard_tap(KEY_DELETE);
                i += 2;
                continue;
            }
            if (n == '[') { keyboard_tap(KEY_ESC); i += 2; continue; }
            if (n == '@') { send_ctrl_char(0x00); i += 2; continue; }
            if (n >= 'A' && n <= 'Z') {
                const uint8_t ctrl = (uint8_t)(n - 'A' + 1);
                send_ctrl_char(ctrl);
                i += 2;
                continue;
            }
            if (n >= 'a' && n <= 'z') {
                const uint8_t ctrl = (uint8_t)(n - 'a' + 1);
                send_ctrl_char(ctrl);
                i += 2;
                continue;
            }
            // Not a recognized caret escape; type '^' literally.
            keyboard_tap((uint8_t)'^');
            i++;
            continue;
        }

        // Raw control byte handling (including DEL)
        if (c < 0x20 || c == 0x7F) {
            send_ctrl_char(c);
            i++;
            continue;
        }

        keyboard_tap(c);
        i++;
    }
}

// Simple base64 decode function
int base64_decode(const char *input, char *output, int output_len) {
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int in_len = strlen(input);
    int i = 0, j = 0, in = 0;
    unsigned char char_array_4[4], char_array_3[3];
    int out_len = 0;

    while (in_len-- && (input[in] != '=') && (output_len > out_len)) {
        char_array_4[i++] = input[in]; in++;
        if (i == 4) {
            for (i = 0; i < 4; i++) {
                const char *pos = strchr(base64_chars, char_array_4[i]);
                if (pos == NULL) return -1; // Invalid character
                char_array_4[i] = pos - base64_chars;
            }

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++) {
                if (out_len < output_len) {
                    output[out_len++] = char_array_3[i];
                }
            }
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 4; j++)
            char_array_4[j] = 0;

        for (j = 0; j < 4; j++) {
            const char *pos = strchr(base64_chars, char_array_4[j]);
            if (pos == NULL && char_array_4[j] != 0) return -1; // Invalid character
            if (pos != NULL) char_array_4[j] = pos - base64_chars;
        }

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; (j < i - 1); j++) {
            if (out_len < output_len) {
                output[out_len++] = char_array_3[j];
            }
        }
    }

    output[out_len] = '\0';
    return out_len;
}

// Simple base64 encode function
void base64_encode(const char *input, int input_len, char *output, int output_len) {
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0, j = 0;
    unsigned char char_array_3[3], char_array_4[4];
    int out_len = 0;

    while (input_len-- && (output_len > out_len + 4)) {
        char_array_3[i++] = *(input++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++) {
                output[out_len++] = base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++) {
            output[out_len++] = base64_chars[char_array_4[j]];
        }

        while ((i++ < 3)) {
            output[out_len++] = '=';
        }
    }

    output[out_len] = '\0';
}

void cli_write_char(EmbeddedCli *embeddedCli, char c)
{
    Serial.print(c);
}

void cli_on_command(EmbeddedCli *embeddedCli, CliCommand *command) {
    Serial.println(F("Received command:"));
    Serial.println(command->name);
    embeddedCliTokenizeArgs(command->args);
    for (int i = 1; i <= embeddedCliGetTokenCount(command->args); ++i) {
        Serial.print(F("arg "));
        Serial.print((char) ('0' + i));
        Serial.print(F(": "));
        Serial.println(embeddedCliGetToken(command->args, i));
    }
}
void cli_show(EmbeddedCli *cli, char *args, void *context) {
    Serial.print("Current: ");
    
    // Check if macro contains non-ASCII characters or newlines
    bool needs_base64 = false;
    int len = strlen(cfg.macro);
    for (int i = 0; i < len; i++) {
        // Preserve old behavior for non-ASCII bytes / literal newlines (web UI relies on base64 for these),
        // but keep control bytes printable for CLI/UI by encoding them (not base64).
	if ((unsigned char)cfg.macro[i] > 127 || cfg.macro[i] == '\r' || cfg.macro[i] == '\n') {
	    needs_base64 = true;
	    break;
	}
    }
    
    if (needs_base64) {
	// Base64 encode the macro to support non-ASCII bytes and special characters
	char encoded[((MAX_MACRO_LEN * 4) / 3) + 4];
	base64_encode(cfg.macro, strlen(cfg.macro), encoded, sizeof(encoded));
	Serial.print("base64:");
	Serial.println(encoded);
    } else {
	// ASCII-only: print an encoded, single-line representation safe for terminals/web serial.
        serial_print_encoded_macro(cfg.macro);
        Serial.println();
    }
}

void cli_prog(EmbeddedCli *cli, char *args, void *context) {
    // args already contains the raw argument string (everything after "prog ")
    if (args == NULL || strlen(args) == 0) {
	Serial.println("Usage:  prog <value> or prog base64:<encoded-value>");
	Serial.println("  Regular ASCII: prog hello world");
	Serial.println("  Base64 (non-ASCII/newlines): prog base64:aGVsbG8gd29ybGQ=");
	return;
    }
    
    // Check if it starts with "base64:" prefix
    const char *base64_prefix = "base64:";
    if (strncmp(args, base64_prefix, strlen(base64_prefix)) == 0) {
	// Base64 encoded value - decode it
	const char *encoded_value = args + strlen(base64_prefix);
	char decoded[MAX_MACRO_LEN];
	int decoded_len = base64_decode(encoded_value, decoded, MAX_MACRO_LEN - 1);
	if (decoded_len <= 0 || decoded_len >= MAX_MACRO_LEN) {
	    Serial.println("Error: Decoded value too long or invalid base64");
	    return;
	}
	strcpy(cfg.macro, decoded);
    } else {
	// Regular ASCII value - use as-is
	if (strlen(args) >= MAX_MACRO_LEN) {
	    Serial.println("Error: Value too long");
	    return;
	}
	strcpy(cfg.macro, args);
    }
    
    Serial.println("Programming to:");
    Serial.print("\"\"\"");
    serial_print_encoded_macro(cfg.macro);
    Serial.print("\"\"\"\n\r");
    EEPROM.put(0, cfg);
    EEPROM.commit();
}

void cli_default(EmbeddedCli *cli, char *args, void *context) {
    Serial.println("Resetting EEPROM to defaults");
    eeprom_clear();
}

void cli_init() {
    EmbeddedCliConfig *config = embeddedCliDefaultConfig();
    config->maxBindingCount = 16;
    cli = embeddedCliNew(config);
    cli->writeChar = cli_write_char;
    cli->onCommand = cli_on_command;

    embeddedCliAddBinding(
	cli,
	{
	    "show",
	    "Show current macro value",
	    false,
	    NULL,
	    cli_show,
	});

    embeddedCliAddBinding(
	cli,
	{
	    "prog",
	    "Program a new macro value.\r\n"
	    "  Usage: prog <value> | prog base64:<encoded>\r\n"
	    "  Examples:\r\n"
	    "    prog hello world\r\n"
	    "    prog base64:aGVsbG8gd29ybGQ=",
	    false,
	    NULL,
	    cli_prog,
	});

    embeddedCliAddBinding(
	cli,
	{
	    "clear",
	    "Reset EEPROM to default state",
	    false,
	    NULL,
	    cli_default,
	});

    
}

void eeprom_clear() {
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = MAGIC;
    strncpy(cfg.macro, DEFAULT_STRING, MAX_MACRO_LEN-1);
    cfg.macro[MAX_MACRO_LEN-1] = 0;
    EEPROM.put(0, cfg);
    EEPROM.commit();
}
void eeprom_init() {
    if (sizeof (Config) > EEPROM_SIZE) {
	while(1) {
	    Serial.println("ERROR: Config size is too big!");
	    digitalWrite(LED_BUILTIN, 1);
	    delay(100);
	    digitalWrite(LED_BUILTIN, 0);
	    delay(100);
	}
    }
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, cfg);
    if (cfg.magic != MAGIC) {   // uninitialized
	eeprom_clear();
    } else {
	Serial.println("Did not Write");
    }
}

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    pinMode(BUTTON, INPUT_PULLUP);
    Keyboard.begin();
    debouncer.attach(BUTTON);
    eeprom_init();
    cli_init();
}

void loop() {
    debouncer.update();
    
    if (debouncer.fell()) {
	Serial.println("Pressed");
	buttonPressTime = millis();
	buttonPressed = true;
	digitalWrite(LED_BUILTIN, HIGH);
    }
    
    if (debouncer.rose()) {
	Serial.println("Released");
	digitalWrite(LED_BUILTIN, LOW);
	
	if (buttonPressed) {
	    unsigned long pressDuration = millis() - buttonPressTime;
	    
	    if (pressDuration < LONG_PRESS_MS) {
		// Short press: send macro
		Serial.println("Short press - sending macro");
		send_macro(cfg.macro);
	    } else if (pressDuration < EXTRA_LONG_PRESS_MS) {
		// Long press: open URL (Ctrl+L opens address bar in most browsers)
		Serial.println("Long press - opening URL");
		uint8_t mods[] = { KEY_LEFT_CTRL };
		keyboard_chord(mods, 1, (uint8_t)'l');
		delay(100);  // Small delay to ensure address bar opens
		// Type the URL (you can customize this)
		Keyboard.print("https://ccrome.github.io/button");
		delay(50);
		keyboard_tap(KEY_RETURN);
	    } else {
		// Extra long press: enter bootloader mode
		Serial.println("Extra long press - entering bootloader mode");
		delay(100);  // Small delay to ensure message is sent
		rp2040.rebootToBootloader();
	    }
	    
	    buttonPressed = false;
	}
    }
    
    // provide all chars to cli
    while (Serial.available() > 0) {
        embeddedCliReceiveChar(cli, Serial.read());
    }

    embeddedCliProcess(cli);

}
