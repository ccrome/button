from pathlib import Path

Import("env")


def replace_once(text: str, old: str, new: str, path: Path) -> str:
    if old in text:
        return text.replace(old, new, 1)
    if new in text:
        return text
    raise RuntimeError(f"expected pattern not found in {path}")


def patch_file(path_str: str, replacements) -> None:
    path = Path(path_str)
    text = path.read_text()
    original = text
    for old, new in replacements:
        text = replace_once(text, old, new, path)
    if text != original:
        path.write_text(text)


framework_dir = Path(env.PioPlatform().get_package_dir("framework-arduinopico"))

patch_file(
    framework_dir / "cores" / "rp2040" / "USB.cpp",
    [
        (
            "            TUD_HID_DESCRIPTOR(1 /* placeholder*/, 0, HID_ITF_PROTOCOL_NONE, hid_report_len, _hid_endpoint = USB.registerEndpointIn(), CFG_TUD_HID_EP_BUFSIZE, (uint8_t)usb_hid_poll_interval)",
            "            TUD_HID_DESCRIPTOR(1 /* placeholder*/, 0, HID_ITF_PROTOCOL_KEYBOARD, hid_report_len, _hid_endpoint = USB.registerEndpointIn(), 8, (uint8_t)usb_hid_poll_interval)",
        )
    ],
)

patch_file(
    framework_dir / "libraries" / "Keyboard" / "src" / "Keyboard.cpp",
    [
        (
            'static const uint8_t desc_hid_report_consumer[] = { TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(1))};\n',
            "",
        ),
        (
            "    _idConsumer = USB.registerHIDDevice(desc_hid_report_consumer, sizeof(desc_hid_report_consumer), 11, 0x0000);\n",
            "",
        ),
        (
            "    USB.unregisterHIDDevice(_idConsumer);\n",
            "",
        ),
        (
            "void Keyboard_::sendConsumerReport(uint16_t key) {\n    if (!_running) {\n        return;\n    }\n    CoreMutex m(&USB.mutex);\n    tud_task();\n    if (USB.HIDReady()) {\n        tud_hid_report(USB.findHIDReportID(_idConsumer), &key, sizeof(key));\n    }\n    tud_task();\n}\n",
            "void Keyboard_::sendConsumerReport(uint16_t key) {\n    (void) key;\n}\n",
        ),
    ],
)
