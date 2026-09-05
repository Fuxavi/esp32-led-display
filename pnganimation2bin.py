#!/usr/bin/env python3
"""
Convierte uno o varios PNG en el formato .bin de animación para ESP32.

Formato:
  2 bytes -> width
  2 bytes -> height
  2 bytes -> numFrames
  2 bytes -> fps
  N bytes -> frame 0
  N bytes -> frame 1
  ...

Transparente = OFF. Cualquier píxel visible = ON.
El color RGB se ignora.

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
    return [(0, int(p)) if p.isdigit() else (1, p.lower()) for p in parts]


def find_frames(input_path: Path):
    if input_path.is_file():
        if input_path.suffix.lower() != ".png":
            raise ValueError(f"El archivo no es un PNG: {input_path}")
        return [input_path]

    if not input_path.is_dir():
        raise ValueError(f"No existe la ruta: {input_path}")

    frames = [
        p for p in input_path.iterdir()
        if p.is_file() and p.suffix.lower() == ".png"
    ]

    if not frames:
        raise ValueError(f"No se encontraron PNGs en: {input_path}")

    frames.sort(key=natural_sort_key)
    return frames


def png_to_bit_buffer(image: Image.Image):
    image = image.convert("RGBA")
    width, height = image.size

    frame_size = (width * height + 7) // 8
    buffer = bytearray(frame_size)

    for y in range(height):
        for x in range(width):
            rgb = image.getpixel((x, y))
            r, g, b = rgb[:3]
            alpha = image.getpixel((x, y))[3]

            if alpha > 0:
                pixel_index = y * width + x
                buffer[pixel_index // 8] |= 1 << (pixel_index % 8)

    return buffer


def print_ascii_preview(buffer, width, height, frame_number):
    print(f"\n--- Preview frame {frame_number} ---")
    for y in range(height):
        line = []
        for x in range(width):
            index = y * width + x
            on = buffer[index // 8] & (1 << (index % 8))
            line.append("#" if on else ".")
        print("".join(line))
    print()


def validate(width, height, num_frames, fps):
    if width == 0 or height == 0:
        raise ValueError("La imagen no puede tener resolución 0.")

    if width > MAX_WIDTH or height > MAX_HEIGHT:
        raise ValueError(
            f"La resolución {width}x{height} supera el máximo "
            f"permitido de {MAX_WIDTH}x{MAX_HEIGHT}."
        )

    if not 1 <= num_frames <= MAX_FRAMES:
        raise ValueError(f"Número de frames inválido: {num_frames}.")

    if not 0 <= fps <= MAX_FPS:
        raise ValueError(f"FPS inválidos: {fps}.")


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
            raise ValueError(f"No se pudo abrir '{frame_path}': {e}")

        print(f"  {i:4d}: {frame_path.name:<30} {w}x{h}")

        if width is None:
            width, height = w, h
        elif (w, h) != (width, height):
            raise ValueError(
                f"Resolución diferente en '{frame_path.name}'. "
                f"Esperada: {width}x{height}, encontrada: {w}x{h}."
            )

    num_frames = len(frames)
    validate(width, height, num_frames, fps)

    frame_size = (width * height + 7) // 8
    expected_size = 8 + frame_size * num_frames

    print(f"\nResolución:       {width}x{height}")
    print(f"Frames:           {num_frames}")
    print(f"FPS:              {fps}")
    print(f"Bytes por frame:  {frame_size}")
    print(f"Cabecera:         8 bytes")
    print(f"Tamaño final:     {expected_size} bytes")
    print(f"Salida:            {output_path}\n")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "wb") as output:
        # Little-endian: width, height, numFrames, fps
        output.write(struct.pack("<HHHH", width, height, num_frames, fps))

        for i, frame_path in enumerate(frames, 1):
            print(f"Convirtiendo frame {i}/{num_frames}: {frame_path.name}")

            with Image.open(frame_path) as image:
                buffer = png_to_bit_buffer(image)

            if len(buffer) != frame_size:
                raise RuntimeError(
                    f"Tamaño inesperado en {frame_path.name}."
                )

            output.write(buffer)

            if preview:
                print_ascii_preview(buffer, width, height, i)

    real_size = output_path.stat().st_size

    if real_size != expected_size:
        raise RuntimeError(
            f"Tamaño incorrecto: esperado {expected_size}, generado {real_size}."
        )

    print("\n" + "=" * 50)
    print("OK - Animación generada correctamente")
    print("=" * 50)
    print(f"Archivo:          {output_path}")
    print(f"Resolución:       {width}x{height}")
    print(f"Frames:           {num_frames}")
    print(f"FPS:              {fps}")
    print(f"Bytes por frame:  {frame_size}")
    print(f"Tamaño total:     {real_size} bytes")


def main():
    parser = argparse.ArgumentParser(
        description="Convierte PNGs de Aseprite a un único .bin para ESP32."
    )

    parser.add_argument(
        "input",
        help="Carpeta con PNGs o un único PNG."
    )

    parser.add_argument(
        "output",
        nargs="?",
        help="Archivo .bin de salida. Si se omite, se genera automáticamente."
    )

    parser.add_argument(
        "fps",
        nargs="?",
        type=int,
        default=12,
        help="FPS de la animación. Por defecto: 12."
    )

    parser.add_argument(
        "--preview",
        action="store_true",
        help="Muestra cada frame en ASCII por consola."
    )

    args = parser.parse_args()

    try:
        input_path = Path(args.input)
        output_path = output_name(input_path, args.output)

        convert(input_path, output_path, args.fps, args.preview)

    except (ValueError, OSError, RuntimeError) as e:
        print(f"\nERROR: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
