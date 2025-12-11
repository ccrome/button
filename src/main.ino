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
struct Config {
    uint32_t magic;
    char macro[MAX_MACRO_LEN];
};

void eeprom_clear();

Config cfg;
EmbeddedCli *cli;

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
	if ((unsigned char)cfg.macro[i] > 127 || cfg.macro[i] == '\r' || cfg.macro[i] == '\n') {
	    needs_base64 = true;
	    break;
	}
    }
    
    if (needs_base64) {
	// Base64 encode the macro to support Unicode and special characters
	char encoded[((MAX_MACRO_LEN * 4) / 3) + 4];
	base64_encode(cfg.macro, strlen(cfg.macro), encoded, sizeof(encoded));
	Serial.print("base64:");
	Serial.println(encoded);
    } else {
	// Pure ASCII - output directly
	Serial.println(cfg.macro);
    }
}

void cli_prog(EmbeddedCli *cli, char *args, void *context) {
    // args already contains the raw argument string (everything after "prog ")
    if (args == NULL || strlen(args) == 0) {
	Serial.println("Usage:  prog <value> or prog base64:<encoded-value>");
	Serial.println("  Regular ASCII: prog hello world");
	Serial.println("  Base64 (Unicode/newlines): prog base64:aGVsbG8gd29ybGQ=");
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
    Serial.printf("\"\"\"%s\"\"\"\n\r", cfg.macro);
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
	digitalWrite(LED_BUILTIN, HIGH);  // Turn LED on when sending
	Keyboard.print(cfg.macro);
    }
    if (debouncer.rose()) {
	Serial.println("Released");
	digitalWrite(LED_BUILTIN, LOW);  // Turn LED on when sending
    }
    // provide all chars to cli
    while (Serial.available() > 0) {
        embeddedCliReceiveChar(cli, Serial.read());
    }

    embeddedCliProcess(cli);

}
