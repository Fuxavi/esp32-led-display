#!/usr/bin/env python3
"""
Convierte uno o varios PNG en un formato .bin de animación RGB565 para ESP32.

Formato:
  2 bytes -> width
  2 bytes -> height
  2 bytes -> numFrames
  2 bytes -> fps
  N * 2 bytes -> frame 0 (RGB565)
  N * 2 bytes -> frame 1 (RGB565)
  ...

Cada píxel ocupa 2 bytes en formato RGB565:

  RRRRRGGGGGGBBBBB

Transparencia:
  alpha == 0 -> 0x0000

IMPORTANTE:
  En esta versión 0x0000 también representa RGB565 negro.
  Por tanto, el negro puro se interpreta como transparente.

Ejemplos:
  python png_animation_to_bin.py frames
  python png_animation_to_bin.py frames animacion.bin
  python png_animation_to_bin.py frames animacion.bin 12
  python png_animation_to_bin.py frames animacion.bin 12 --preview
  python png_animation_to_bin.py frame_001.png
"""

from pathlib import Path
import argparse
import re
import struct
import sys

from PIL import Image


MAX_WIDTH = 128
MAX_HEIGHT = 64
MAX_FRAMES = 65535
MAX_FPS = 65535


def natural_sort_key(path: Path):
    # frame_1, frame_2, frame_10 en ese orden
    parts = re.split(r"(\d+)", path.stem)

    return [
        (0, int(p)) if p.isdigit() else (1, p.lower())
        for p in parts
    ]


def find_frames(input_path: Path):
    if input_path.is_file():
        if input_path.suffix.lower() != ".png":
            raise ValueError(f"El archivo no es un PNG: {input_path}")

        return [input_path]

    if not input_path.is_dir():
        raise ValueError(f"No existe la ruta: {input_path}")

    frames = [
        p
        for p in input_path.iterdir()
        if p.is_file() and p.suffix.lower() == ".png"
    ]

    if not frames:
        raise ValueError(
            f"No se encontraron PNGs en: {input_path}"
        )

    frames.sort(key=natural_sort_key)

    return frames


def rgb888_to_rgb565(r, g, b):
    """
    Convierte RGB888 (8 bits por canal) a RGB565.

    RGB565:
        RRRRR GGGGGG BBBBB
         5       6      5 bits
    """

    return (
        ((r & 0xF8) << 8)
        | ((g & 0xFC) << 3)
        | (b >> 3)
    )


def png_to_rgb565_buffer(image: Image.Image):
    """
    Convierte un PNG RGBA a un buffer RGB565.

    Cada píxel ocupa 2 bytes.

    alpha == 0:
        píxel transparente -> 0x0000

    alpha > 0:
        píxel visible -> RGB565
    """

    image = image.convert("RGBA")

    width, height = image.size

    buffer = bytearray(width * height * 2)

    for y in range(height):
        for x in range(width):

            r, g, b, alpha = image.getpixel((x, y))

            if alpha == 0:
                color = 0x0000
            else:
                color = rgb888_to_rgb565(r, g, b)

            index = (y * width + x) * 2

            # Little-endian
            buffer[index] = color & 0xFF
            buffer[index + 1] = (color >> 8) & 0xFF

    return buffer


def rgb565_to_rgb888(color):
    """
    Convierte RGB565 a RGB888.
    Se utiliza solamente para el preview.
    """

    r = (color >> 11) & 0x1F
    g = (color >> 5) & 0x3F
    b = color & 0x1F

    # Expandir de 5/6 bits a 8 bits
    r = (r << 3) | (r >> 2)
    g = (g << 2) | (g >> 4)
    b = (b << 3) | (b >> 2)

    return r, g, b


def print_rgb565_preview(buffer, width, height, frame_number):
    """
    Preview sencillo por consola.

    Como la terminal no representa bien todos los colores,
    se muestra el brillo aproximado del píxel.
    """

    chars = " .:-=+*#%@"

    print(f"\n--- Preview frame {frame_number} ---")

    for y in range(height):

        line = []

        for x in range(width):

            index = (y * width + x) * 2

            color = (
                buffer[index]
                | (buffer[index + 1] << 8)
            )

            if color == 0x0000:
                line.append(" ")
                continue

            r, g, b = rgb565_to_rgb888(color)

            # Luminancia aproximada
            brightness = (
                0.299 * r
                + 0.587 * g
                + 0.114 * b
            )

            char_index = int(
                brightness / 255 * (len(chars) - 1)
            )

            line.append(chars[char_index])

        print("".join(line))

    print()


def validate(width, height, num_frames, fps):

    if width == 0 or height == 0:
        raise ValueError(
            "La imagen no puede tener resolución 0."
        )

    if width > MAX_WIDTH or height > MAX_HEIGHT:
        raise ValueError(
            f"La resolución {width}x{height} supera el máximo "
            f"permitido de {MAX_WIDTH}x{MAX_HEIGHT}."
        )

    if not 1 <= num_frames <= MAX_FRAMES:
        raise ValueError(
            f"Número de frames inválido: {num_frames}."
        )

    if not 0 <= fps <= MAX_FPS:
        raise ValueError(
            f"FPS inválidos: {fps}."
        )


def output_name(input_path: Path, requested):

    if requested:
        return Path(requested)

    if input_path.is_file():
        return input_path.with_suffix(".bin")

    return input_path.parent / f"{input_path.name}.bin"


def convert(input_path: Path, output_path: Path, fps: int, preview: bool):

    frames = find_frames(input_path)

    print(f"Frames encontrados: {len(frames)}\n")

    width = height = None

    for i, frame_path in enumerate(frames, 1):

        try:
            with Image.open(frame_path) as image:
                w, h = image.size

        except Exception as e:
            raise ValueError(
                f"No se pudo abrir '{frame_path}': {e}"
            )

        print(
            f"  {i:4d}: "
            f"{frame_path.name:<30} "
            f"{w}x{h}"
        )

        if width is None:
            width, height = w, h

        elif (w, h) != (width, height):
            raise ValueError(
                f"Resolución diferente en "
                f"'{frame_path.name}'. "
                f"Esperada: {width}x{height}, "
                f"encontrada: {w}x{h}."
            )

    num_frames = len(frames)

    validate(
        width,
        height,
        num_frames,
        fps
    )

    # 2 bytes por píxel
    frame_size = width * height * 2

    expected_size = (
        8
        + frame_size * num_frames
    )

    print(f"\nResolución:       {width}x{height}")
    print(f"Frames:           {num_frames}")
    print(f"FPS:              {fps}")
    print(f"Formato:          RGB565")
    print(f"Bytes por píxel:  2")
    print(f"Bytes por frame:  {frame_size}")
    print(f"Cabecera:         8 bytes")
    print(f"Tamaño final:     {expected_size} bytes")
    print(f"Salida:            {output_path}\n")

    output_path.parent.mkdir(
        parents=True,
        exist_ok=True
    )

    with open(output_path, "wb") as output:

        # Little-endian:
        # width, height, numFrames, fps
        output.write(
            struct.pack(
                "<HHHH",
                width,
                height,
                num_frames,
                fps
            )
        )

        for i, frame_path in enumerate(frames, 1):

            print(
                f"Convirtiendo frame "
                f"{i}/{num_frames}: "
                f"{frame_path.name}"
            )

            with Image.open(frame_path) as image:

                buffer = png_to_rgb565_buffer(
                    image
                )

            if len(buffer) != frame_size:
                raise RuntimeError(
                    f"Tamaño inesperado en "
                    f"{frame_path.name}."
                )

            output.write(buffer)

            if preview:
                print_rgb565_preview(
                    buffer,
                    width,
                    height,
                    i
                )

    real_size = output_path.stat().st_size

    if real_size != expected_size:
        raise RuntimeError(
            f"Tamaño incorrecto: "
            f"esperado {expected_size}, "
            f"generado {real_size}."
        )

    print("\n" + "=" * 50)
    print("OK - Animación RGB565 generada correctamente")
    print("=" * 50)

    print(f"Archivo:          {output_path}")
    print(f"Resolución:       {width}x{height}")
    print(f"Frames:           {num_frames}")
    print(f"FPS:              {fps}")
    print(f"Bytes por píxel:  2")
    print(f"Bytes por frame:  {frame_size}")
    print(f"Tamaño total:     {real_size} bytes")


def main():

    parser = argparse.ArgumentParser(
        description=(
            "Convierte PNGs de Aseprite "
            "a un único .bin RGB565 para ESP32."
        )
    )

    parser.add_argument(
        "input",
        help="Carpeta con PNGs o un único PNG."
    )

    parser.add_argument(
        "output",
        nargs="?",
        help=(
            "Archivo .bin de salida. "
            "Si se omite, se genera automáticamente."
        )
    )

    parser.add_argument(
        "fps",
        nargs="?",
        type=int,
        default=12,
        help=(
            "FPS de la animación. "
            "Por defecto: 12."
        )
    )

    parser.add_argument(
        "--preview",
        action="store_true",
        help=(
            "Muestra cada frame en ASCII "
            "por consola."
        )
    )

    args = parser.parse_args()

    try:

        input_path = Path(args.input)

        output_path = output_name(
            input_path,
            args.output
        )

        convert(
            input_path,
            output_path,
            args.fps,
            args.preview
        )

    except (
        ValueError,
        OSError,
        RuntimeError
    ) as e:

        print(f"\nERROR: {e}")

        sys.exit(1)


if __name__ == "__main__":
    main()
