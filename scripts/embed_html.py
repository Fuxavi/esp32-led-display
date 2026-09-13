from pathlib import Path

# Archivo HTML original
html_file = Path("web/index.html")

# Archivo C++ que vamos a generar
output_file = Path("index_html.h")

# Leer HTML
html = html_file.read_text(encoding="utf-8")

# Generar header C++
content = """#pragma once

#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
""" + html + """
)rawliteral";
"""

# Escribir archivo
output_file.write_text(content, encoding="utf-8")

print("====================================")
print("HTML embebido correctamente")
print("Entrada :", html_file)
print("Salida  :", output_file)
print("====================================")