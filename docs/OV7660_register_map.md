# Documentación Técnica: Sensor CMOS OmniVision OV7660 / OV7648

Documento de ingeniería inversa obtenido de la especificación técnica de OmniVision y el subsistema multimedia del Kernel de Linux (`drivers/media/usb/gspca` y `drivers/media/video/sn9c102/sn9c102_ov7660.c`).

---

## 1. Especificaciones de Dirección I2C

- **Dirección I2C Slave (7 bits):** `0x21` (ó `0x28` / `0x50` según la variante del chip Sonix bridge).
- **Dirección I2C Slave (8 bits):**
  - **Escritura (Write):** `0x42` (ó `0x50`)
  - **Lectura (Read):** `0x43` (ó `0x51`)
- **Registros de Identificación del Sensor:**
  - **`0x0A` (PID):** Retorna `0x76`
  - **`0x0B` (VER):** Retorna `0x60` (ó `0x48` para OV7648)

---

## 2. Mapa de Registros Clave del OV7660

| Dirección (Hex) | Nombre | Descripción / Bits | Valor Típico Init |
| :--- | :--- | :--- | :--- |
| **`0x12`** | `COM7` | **Control Principal y Reset:**<br>bit 7: Reset de software (`0x80`)<br>bit 5: Formato QVGA (320x240)<br>bit 4: Formato CIF (352x288)<br>bit 3: Formato QCIF<br>bit 2: Formato RGB (1) vs YUV (0)<br>bit 0: RAW Bayer (1) | `0x80` (Reset)<br>`0x05` (VGA RAW) |
| **`0x00`** | `GAIN` | Control de Ganancia AGC (0x00 a 0x3F) | `0x00` |
| **`0x01`** | `BLUE` | Ganancia de Canal Azul (Blue Gain) | `0x80` |
| **`0x02`** | `RED` | Ganancia de Canal Rojo (Red Gain) | `0x80` |
| **`0x10`** | `AECH` | Byte alto de exposición (AEC Exposure Value) | `0x40` |
| **`0x11`** | `CLKRC` | Prescaler de Reloj Interno (Clock Divider) | `0x00` (Fsys directa) |
| **`0x13`** | `COM8` | **Habilitación de Control Automático (AEC/AGC/AWB):**<br>bit 0: Habilitar AEC (Auto Exposure)<br>bit 1: Habilitar AWB (Auto White Balance)<br>bit 2: Habilitar AGC (Auto Gain) | `0x87` (Todo Auto) |
| **`0x14`** | `COM9` | Control de Techo de Ganancia AGC (Max AGC Ceiling: 2x, 4x, 8x, 16x) | `0x38` (8x Max) |
| **`0x15`** | `COM10` | **Sincronización:**<br>bit 6: Invertir VSYNC<br>bit 5: Invertir HSYNC<br>bit 4: Invertir PCLK | `0x00` |
| **`0x1E`** | `MVFP` | **Espejo y Volteado (Mirror & Flip):**<br>bit 5: Volteado Vertical (V-Flip)<br>bit 4: Espejo Horizontal (H-Mirror) | `0x00` (Normal)<br>`0x30` (V-Flip + H-Mirror) |
| **`0x60`** | `COM6` | Configuración de sincronización de salida CCIR656 | `0x60` |

---

## 3. Secuencia de Inicialización C++ (Vector de Registros)

Matriz extraída de `sn9c102_ov7660.c` para encender el sensor y configurar la salida de vídeo RGB/Bayer RAW en resolución VGA (640x480):

```cpp
struct SensorRegInit {
    std::uint8_t reg;
    std::uint8_t val;
};

static const SensorRegInit ov7660_init_script[] = {
    { 0x12, 0x80 }, // 1. Reset de software del sensor
    { 0x11, 0x00 }, // 2. Reloj interno sin divisor
    { 0x12, 0x05 }, // 3. Modo VGA, salida RAW RGB Bayer
    { 0x13, 0x87 }, // 4. Activar AEC, AWB y AGC automático
    { 0x01, 0x80 }, // 5. Ganancia canal azul inicial (1.0x)
    { 0x02, 0x80 }, // 6. Ganancia canal rojo inicial (1.0x)
    { 0x14, 0x38 }, // 7. Techo máximo de ganancia a 8x
    { 0x15, 0x00 }, // 8. Sincronización normal
    { 0x1E, 0x00 }  // 9. Orientación normal (Sin espejo)
};
```
