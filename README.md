# BUTTON - Programmable USB Keyboard Button

A Raspberry Pi Pico-based USB HID keyboard button that types a programmable macro string when pressed. The macro can be configured via a serial CLI or a web-based interface.

<img src="images/button.jpg" alt="Button" width="300" height="300">

## Features

- **USB HID Keyboard**: Acts as a USB keyboard device, typing the programmed macro when the button is pressed
- **EEPROM Storage**: Macro is stored in EEPROM and persists across power cycles
- **Serial CLI**: Command-line interface over USB Serial (CDC) for programming
- **Web Interface**: Modern web-based UI using Web Serial API for easy configuration
- **Base64 Support for Non-ASCII / Newlines**: You can store arbitrary bytes or multi-line values via base64 for safe transport over the CLI/Web Serial
- **Button Debouncing**: Hardware debouncing using Bounce2 library

## Hardware Requirements

- Raspberry Pi Pico (or compatible RP2040 board)
- Push button (momentary, normally open)
- USB connection to host computer

## Button Wiring

The button connects to the Raspberry Pi Pico as follows:

- **One terminal** of the button → **GPIO 13** (pin 17 on the Pico)
- **Other terminal** of the button → **GND** (any ground pin)

The firmware uses the internal pull-up resistor on GPIO 13, so no external pull-up resistor is needed. The button is active LOW - when pressed, it connects GPIO 13 to ground, triggering the macro.

**Wiring Diagram:**
```
Button Terminal 1 ──── GPIO 13 (Pin 17)
Button Terminal 2 ──── GND
```

**Note:** The button should be a momentary push button (normally open). When released, the internal pull-up keeps GPIO 13 HIGH. When pressed, it connects to GND, pulling GPIO 13 LOW and triggering the macro.

## 3D Printed Enclosure

The `3d/` folder contains STEP files for a 3D printed enclosure:

- **The Button - button case top.step**: Top cover of the enclosure
- **The Button - button-case-bottom.step**: Bottom base of the enclosure
- **The Button - button-case-bottom-cap.step**: Bottom cap/cover piece

These files can be opened in any CAD software that supports STEP format (e.g., Fusion 360, FreeCAD, SolidWorks, Onshape) and exported to STL for 3D printing. The enclosure is designed to house the Raspberry Pi Pico and button assembly.

## Software Requirements

- PlatformIO
- Compatible browser with Web Serial API support (Chrome, Edge, Opera)

## Project Structure

```
rpi-button/
├── src/
│   ├── main.ino          # Main firmware code
│   └── embedded_cli.h    # Embedded CLI library header
├── app/
│   └── index.html        # Web interface for programming
├── 3d/
│   ├── The Button - button case top.step
│   ├── The Button - button-case-bottom-cap.step
│   └── The Button - button-case-bottom.step
├── platformio.ini        # PlatformIO configuration
└── README.md            # This file
```

## Building and Flashing

### Using PlatformIO

1. Install PlatformIO if you haven't already:
   ```bash
   pip install platformio
   ```

2. Clone this repository:
   ```bash
   git clone <repository-url>
   cd rpi-button
   ```

3. Connect your Raspberry Pi Pico to your computer via USB

4. Build and upload:
   ```bash
   pio run -t upload
   ```

5. Monitor serial output (optional):
   ```bash
   pio device monitor
   ```

### Bootloader Mode

If you need to enter bootloader mode manually:

```bash
sudo stty -F /dev/ttyACM0 1200
```

### GitHub Actions CI/CD

The repository includes GitHub Actions workflows that automatically build the firmware:

#### Build Workflow

On every push or pull request, the workflow builds both `pico` and `pico2` environments. Build artifacts (`.uf2`, `.bin`, `.elf` files) are available for download:

1. Go to the **Actions** tab in GitHub
2. Click on the latest workflow run
3. Scroll down to the **Artifacts** section
4. Download the `firmware` artifact (contains builds for both environments)
5. Artifacts are retained for 7 days

#### Creating a Release

To create a GitHub release with firmware files attached:

1. Go to **Releases** → **Draft a new release**
2. Create a new tag (e.g., `v1.0.0`) or select an existing tag
3. Fill in the release title and description
4. Click **Publish release**

The release workflow will automatically:
- Build both `pico` and `pico2` environments
- Attach the `.uf2` firmware files to the release
- Files will be named `firmware-pico.uf2` and `firmware-pico2.uf2`

You can then download the firmware directly from the release page and flash it to your Pico by dragging the `.uf2` file to the mounted drive.

## Usage

### Serial CLI Interface

Connect to the device via USB Serial (CDC). The device provides a command-line interface with the following commands:

#### `show`
Display the current macro value.

- If the macro contains non-ASCII bytes or literal newlines, it will be shown as base64-encoded (`base64:...`).
- Otherwise it is printed as a single line using escapes for control keys (so it is safe to copy/paste).

#### `prog <value>`
Program a new macro value. For ASCII-only text:
```
prog hello world
```

#### Macro syntax (control keys, chords, and escapes)
Your programmed value can include common control signals.

- **Chords** (press modifiers + a key):

```
prog {CTRL+C}
prog {CTRL+SHIFT+ESC}
prog {ALT+F4}
prog {GUI+R}
```

- **Named keys**:

```
prog {ENTER}
prog {TAB}
prog {ESC}
prog {BACKSPACE}
prog {DELETE}
prog {UP}{UP}{DOWN}{DOWN}{LEFT}{RIGHT}{LEFT}{RIGHT}
prog {F5}
```

- **Caret control notation** (classic terminal style):

```
prog ^C
prog ^[
prog ^?
```

- **Backslash escapes**:

```
prog hello\nworld            ; sends Enter between words
prog one\\t two              ; sends Tab
prog \\\\                    ; sends a literal backslash
prog \\x1b                   ; sends Escape
```

Notes:
- **Use `\n` for Enter** if you want a “multi-line” macro without using literal newlines (this keeps it ASCII and avoids base64).
- To type literal braces, use `{{` for `{` and `}}` for `}`.

For non-ASCII bytes or multi-line text, use base64 encoding:
```
prog base64:aGVsbG8gd29ybGQ=
```

#### `clear`
Reset the EEPROM to default values and restore the default macro.

### Web Interface

#### Using GitHub Pages

To host the web interface on GitHub Pages:

1. Enable GitHub Pages in your repository settings:
   - Go to Settings → Pages
   - Under "Source", select "Deploy from a branch"
   - Choose the branch (usually `main` or `master`)
   - Select the folder containing `index.html` (e.g., `/app` or `/docs` if you move it there)
   - Click Save

2. If your `index.html` is in the `app/` folder, you have two options:
   - **Option A**: Move `app/index.html` to a `docs/` folder in the root, then set Pages to deploy from `/docs`
   - **Option B**: Configure Pages to deploy from `/app` folder (if supported by your GitHub Pages setup)

3. Once deployed, access the interface at:
   ```
   https://<your-username>.github.io/rpi-button/
   ```
   (or the path you configured)

4. The Web Serial API requires HTTPS (or localhost), so GitHub Pages is perfect for this use case.

#### Using Locally

Alternatively, you can open `app/index.html` directly in a compatible browser (Chrome, Edge, or Opera):

1. Open `app/index.html` in your browser

2. Click "Connect" and select your device from the serial port list

3. The current macro value will be displayed automatically

4. Enter a new value in the text area:
   - Supports multi-line text (press Enter for new lines)
   - Supports non-ASCII text (sent via base64)
   - Use Ctrl+Enter (Cmd+Enter on Mac) to program the value

5. Click "Program" to save the new macro, or "Clear" to reset to defaults

6. The web interface automatically handles base64 encoding for non-ASCII and multi-line content

## Default Macro

The default macro is:
```
112057-689678-188617-064284-039138-379665-263186-371723
```

## Technical Details

### Firmware

- **Framework**: Arduino (using Earle Philhower's Arduino-Pico core)
- **Libraries**:
  - `Bounce2`: Button debouncing
  - `Keyboard`: USB HID keyboard functionality
  - `EEPROM`: Non-volatile storage
  - `embedded_cli`: Command-line interface

### Configuration

The macro is stored in EEPROM with the following structure:
```cpp
struct Config {
    uint32_t magic;        // Magic number (0xDEADBEEF) for validation
    char macro[MAX_MACRO_LEN];  // Macro string (max 1000 characters)
};
```

### Base64 Encoding

When the macro contains:
- Non-ASCII bytes (for example, extended characters depending on your editor/encoding)
- Carriage returns or newlines

The device automatically uses base64 encoding for storage and transmission. The web interface handles encoding/decoding transparently.

Note: this device sends **key events**, not Unicode codepoints. If you store text outside basic ASCII, what the host receives depends on the host OS, keyboard layout, and how the core maps characters to key sequences.

## Troubleshooting

### Device Not Recognized
- Ensure the Pico is in bootloader mode if flashing fails
- Check USB cable connection
- Verify correct board selection in PlatformIO

### Serial Connection Issues
- Ensure no other program is using the serial port
- Try disconnecting and reconnecting the device
- For bootloader mode, use: `sudo stty -F /dev/ttyACM0 1200`

### Web Interface Not Working
- Use a compatible browser (Chrome, Edge, or Opera)
- Ensure the device is connected and recognized by the OS
- Check browser console for errors

## License

MIT License

Copyright (c) 2024

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Contributing

[Add contribution guidelines if applicable]
