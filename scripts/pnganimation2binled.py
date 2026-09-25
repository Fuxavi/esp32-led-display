"""
Convierte uno o varios PNG en un .bin de animación RGB565
para ESP32.

FORMATO DEL ARCHIVO
===================

CABECERA: 8 bytes

Offset  Tamaño  Campo
------  ------  ----------------
0       2       width
2       2       height
4       2       num_frames
6       2       fps

Después de la cabecera:

8
│
├── FRAME 0
├── FRAME 1
├── FRAME 2
├── ...
└── FRAME N

FRAMES
======

Cada píxel ocupa 2 bytes RGB565.

El orden de bytes almacenado es:

  low byte
  high byte

Ejemplo:

python png_animation_to_bin.py frames animacion.bin 12
"""

from pathlib import Path
import argparse
import re
import struct
import sys

from PIL import Image


# ============================================================
# CONFIGURACIÓN
# ============================================================

MAX_WIDTH = 128
MAX_HEIGHT = 64

MAX_FRAMES = 65535
MAX_FPS = 65535

HEADER_SIZE = 8


# ============================================================
# ORDEN NATURAL DE FRAMES
# ============================================================

def natural_sort_key(path: Path):

    parts = re.split(
        r"(\d+)",
        path.stem
    )

    return [
        (0, int(p)) if p.isdigit()
        else (1, p.lower())
        for p in parts
    ]


# ============================================================
# BUSCAR FRAMES
# ============================================================

def find_frames(input_path: Path):

    if input_path.is_file():

        if input_path.suffix.lower() != ".png":

            raise ValueError(
                f"El archivo no es un PNG: "
                f"{input_path}"
            )

        return [input_path]

    if not input_path.is_dir():

        raise ValueError(
            f"No existe la ruta: "
            f"{input_path}"
        )

    frames = [
        p
        for p in input_path.iterdir()
        if (
            p.is_file()
            and p.suffix.lower() == ".png"
        )
    ]

    if not frames:

        raise ValueError(
            f"No se encontraron PNGs en: "
            f"{input_path}"
        )

    frames.sort(
        key=natural_sort_key
    )

    return frames


# ============================================================
# RGB888 -> RGB565
# ============================================================

def rgb888_to_rgb565(r, g, b):

    return (
        ((r & 0xF8) << 8)
        |
        ((g & 0xFC) << 3)
        |
        (b >> 3)
    )


# ============================================================
# PNG -> BUFFER RGB565
# ============================================================

def png_to_rgb565_buffer(image: Image.Image):

    image = image.convert("RGBA")

    width, height = image.size

    buffer = bytearray(
        width * height * 2
    )

    for y in range(height):

        for x in range(width):

            r, g, b, alpha = image.getpixel(
                (x, y)
            )

            # ------------------------------------------------
            # Transparencia
            # ------------------------------------------------

            if alpha == 0:

                color = 0x0000

            else:

                color = rgb888_to_rgb565(
                    r,
                    g,
                    b
                )

            # ------------------------------------------------
            # RGB565 little-endian
            # ------------------------------------------------

            index = (
                (y * width + x) * 2
            )

            buffer[index] = (
                color & 0xFF
            )

            buffer[index + 1] = (
                (color >> 8) & 0xFF
            )

    return buffer


# ============================================================
# VALIDAR ANIMACIÓN
# ============================================================

def validate(
    width,
    height,
    num_frames,
    fps
):

    if width <= 0 or height <= 0:

        raise ValueError(
            "La imagen no puede tener "
            "resolución 0."
        )

    if width > MAX_WIDTH:

        raise ValueError(
            f"El ancho {width} supera "
            f"el máximo de {MAX_WIDTH}."
        )

    if height > MAX_HEIGHT:

        raise ValueError(
            f"La altura {height} supera "
            f"el máximo de {MAX_HEIGHT}."
        )

    if not (
        1 <= num_frames <= MAX_FRAMES
    ):

        raise ValueError(
            f"Número de frames inválido: "
            f"{num_frames}"
        )

    if not (
        1 <= fps <= MAX_FPS
    ):

        raise ValueError(
            f"FPS inválidos: {fps}"
        )


# ============================================================
# NOMBRE DE SALIDA
# ============================================================

def output_name(
    input_path: Path,
    requested
):

    if requested:

        return Path(requested)

    if input_path.is_file():

        return input_path.with_suffix(
            ".bin"
        )

    return input_path.parent / (
        f"{input_path.name}.bin"
    )


# ============================================================
# CONVERTIR
# ============================================================

def convert(
    input_path: Path,
    output_path: Path,
    fps: int,
    preview: bool
):

    # --------------------------------------------------------
    # FRAMES
    # --------------------------------------------------------

    frames = find_frames(
        input_path
    )

    print(
        f"Frames encontrados: "
        f"{len(frames)}"
    )

    width = None
    height = None

    # --------------------------------------------------------
    # COMPROBAR RESOLUCIONES
    # --------------------------------------------------------

    for i, frame_path in enumerate(
        frames,
        1
    ):

        try:

            with Image.open(
                frame_path
            ) as image:

                w, h = image.size

        except Exception as e:

            raise ValueError(
                f"No se pudo abrir "
                f"'{frame_path}': {e}"
            )

        print(
            f"  {i:4d}: "
            f"{frame_path.name:<30} "
            f"{w}x{h}"
        )

        if width is None:

            width = w
            height = h

        elif (
            w != width
            or h != height
        ):

            raise ValueError(
                f"Resolución diferente en "
                f"'{frame_path.name}'. "
                f"Esperada: "
                f"{width}x{height}, "
                f"encontrada: {w}x{h}."
            )

    num_frames = len(frames)

    validate(
        width,
        height,
        num_frames,
        fps
    )

    # --------------------------------------------------------
    # TAMAÑO DE FRAME
    # --------------------------------------------------------

    frame_size = (
        width
        * height
        * 2
    )

    # --------------------------------------------------------
    # TAMAÑO TOTAL
    # --------------------------------------------------------

    frames_size = (
        frame_size
        * num_frames
    )

    expected_size = (
        HEADER_SIZE
        + frames_size
    )

    # ========================================================
    # INFORMACIÓN
    # ========================================================

    print()
    print("=" * 60)

    print(
        f"Resolución:       {width}x{height}"
    )

    print(
        f"Frames:            {num_frames}"
    )

    print(
        f"FPS:               {fps}"
    )

    print(
        f"Bytes por frame:   {frame_size}"
    )

    print(
        f"Bytes de frames:   {frames_size}"
    )

    print(
        f"Header:            {HEADER_SIZE}"
    )

    print(
        f"Tamaño esperado:   {expected_size}"
    )

    print(
        f"Salida:             {output_path}"
    )

    print("=" * 60)

    # ========================================================
    # CREAR DIRECTORIO
    # ========================================================

    output_path.parent.mkdir(
        parents=True,
        exist_ok=True
    )

    # ========================================================
    # CREAR BIN
    # ========================================================

    with open(
        output_path,
        "wb"
    ) as output:

        # ----------------------------------------------------
        # CABECERA
        # ----------------------------------------------------
        #
        # EXACTAMENTE:
        #
        # uint16 width
        # uint16 height
        # uint16 frames
        # uint16 fps
        #
        # Total = 8 bytes
        #
        # ----------------------------------------------------

        header = struct.pack(
            "<HHHH",

            width,
            height,
            num_frames,
            fps
        )

        if len(header) != HEADER_SIZE:

            raise RuntimeError(
                f"Header incorrecto: "
                f"{len(header)} bytes."
            )

        output.write(
            header
        )

        # ----------------------------------------------------
        # FRAMES
        # ----------------------------------------------------

        for i, frame_path in enumerate(
            frames,
            1
        ):

            print(
                f"Convirtiendo frame "
                f"{i}/{num_frames}: "
                f"{frame_path.name}"
            )

            with Image.open(
                frame_path
            ) as image:

                buffer = (
                    png_to_rgb565_buffer(
                        image
                    )
                )

            if len(buffer) != frame_size:

                raise RuntimeError(
                    f"Tamaño inesperado en "
                    f"{frame_path.name}: "
                    f"{len(buffer)} bytes "
                    f"(esperados "
                    f"{frame_size})."
                )

            output.write(
                buffer
            )

    # ========================================================
    # COMPROBAR ARCHIVO FINAL
    # ========================================================

    real_size = (
        output_path.stat().st_size
    )

    if real_size != expected_size:

        raise RuntimeError(
            f"Tamaño incorrecto: "
            f"esperado {expected_size}, "
            f"generado {real_size}."
        )

    # ========================================================
    # COMPROBAR CABECERA FÍSICAMENTE
    # ========================================================

    with open(
        output_path,
        "rb"
    ) as f:

        header_check = f.read(
            HEADER_SIZE
        )

    if len(header_check) != HEADER_SIZE:

        raise RuntimeError(
            "No se pudo leer "
            "la cabecera completa."
        )

    values = struct.unpack(
        "<HHHH",
        header_check
    )

    if values != (
        width,
        height,
        num_frames,
        fps
    ):

        raise RuntimeError(
            "La cabecera escrita "
            "no coincide con los "
            "valores esperados."
        )

    # ========================================================
    # RESULTADO
    # ========================================================

    print()
    print("=" * 60)
    print(
        "OK - Animación generada"
    )
    print("=" * 60)

    print(
        f"Archivo:          {output_path}"
    )

    print(
        f"Tamaño:           {real_size} bytes"
    )

    print(
        f"Header:           {HEADER_SIZE} bytes"
    )

    print(
        f"Frames offset:    {HEADER_SIZE}"
    )

    print(
        f"Frame size:       {frame_size}"
    )

    print(
        f"Frames size:      {frames_size}"
    )

    print("=" * 60)

def main():

    parser = argparse.ArgumentParser(
        description=(
            "Convierte PNGs a RGB565 "
            "para una animación ESP32."
        )
    )

    parser.add_argument(
        "input",
        help=(
            "Carpeta con PNGs "
            "o un PNG."
        )
    )

    parser.add_argument(
        "output",
        nargs="?",
        help=(
            "Archivo .bin de salida."
        )
    )

    parser.add_argument(
        "fps",
        nargs="?",
        type=int,
        default=12,
        help=(
            "FPS. Por defecto: 12."
        )
    )

    parser.add_argument(
        "--preview",
        action="store_true"
    )

    args = parser.parse_args()

    try:

        input_path = Path(
            args.input
        )

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

        print()
        print(
            f"ERROR: {e}"
        )

        sys.exit(1)

if __name__ == "__main__":

    main()
