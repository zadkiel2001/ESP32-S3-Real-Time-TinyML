import os
from pathlib import Path
import textwrap

INPUT_TFLITE = Path("models/har_int8.tflite")

OUT_DIR = Path("esp32_model")
OUT_H = OUT_DIR / "har_int8_model_data.h"
OUT_CC = OUT_DIR / "har_int8_model_data.cc"

ARRAY_NAME = "g_model_data"
LEN_NAME = "g_model_data_len"


def bytes_to_c_array(data: bytes, cols: int = 12) -> str:
    # Format like: 0x00, 0x1A, ...
    hex_bytes = [f"0x{b:02x}" for b in data]
    lines = []
    for i in range(0, len(hex_bytes), cols):
        lines.append(", ".join(hex_bytes[i:i+cols]))
    return ",\n  ".join(lines)


def main():
    if not INPUT_TFLITE.exists():
        raise FileNotFoundError(f"Input model not found: {INPUT_TFLITE}")

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    model_bytes = INPUT_TFLITE.read_bytes()
    c_array_body = bytes_to_c_array(model_bytes, cols=12)

    # Header file
    header = textwrap.dedent(f"""\
        #pragma once
        #include <cstdint>

        // TensorFlow Lite model data
        extern const unsigned char {ARRAY_NAME}[];
        extern const int {LEN_NAME};
    """)

    # Source file (.cc)
    source = textwrap.dedent(f"""\
        #include "{OUT_H.name}"

        const unsigned char {ARRAY_NAME}[] = {{
          {c_array_body}
        }};

        const int {LEN_NAME} = {len(model_bytes)};
    """)

    OUT_H.write_text(header, encoding="utf-8")
    OUT_CC.write_text(source, encoding="utf-8")

    print("  Export complete!")
    print(f"  Input : {INPUT_TFLITE} ({len(model_bytes)} bytes)")
    print(f"  Output: {OUT_H}")
    print(f"  Output: {OUT_CC}")
    print()


if __name__ == "__main__":
    main()