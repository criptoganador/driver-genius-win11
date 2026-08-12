# Documentación Técnica y Registros: Chip Sonix SN9C102

Procesador backend monochip USB para cámaras PC VGA (640x480) o CIF (352x288) con sensores CMOS (RGB Bayer).

---

## 1. Características Principales

- **Entrada de datos de sensor:** RAW RGB Bayer de 9 u 8 bits.
- **Tasa de fotogramas:** Hasta 30 fps @ CIF, 12 fps @ VGA.
- **Ganancias digitales de color:** Control independiente R, G, B.
- **Función Snapshot:** Botón integrado de disparo/captura.
- **Escalado y Panning:** Mapeo de hardware a 1/1, 1/2, 1/4 (640x480, 320x240, 160x120).
- **Reloj del sistema:** Cristal de 12 MHz (Fsys_clk = 12 MHz o 24 MHz).
- **USB Endpoints:**
  - **Endpoint 0:** Control (Comandos estándar/Vendor, MaxPsz = 64 bytes).
  - **Endpoint 1 (0x81):** Lectura Isócrona de Video (MaxPsz hasta 1023 bytes, 9 Alternate Settings).
  - **Endpoint 2 (0x82):** Lectura Bulk (64 bytes).
  - **Endpoint 3 (0x83):** Lectura de Interrupción (Botón Snapshot, 1 byte).

---

## 2. Mapa de Registros ASIC del SN9C102 (Direcciones 0x00 a 0x1F)

| Dirección (Hex) | R/W | Nombre | Descripción / Bits |
| :--- | :--- | :--- | :--- |
| **`0x00` (00h)** | **R** | `ASIC_ID[7:0]` | Identificador del chip. Retorna siempre `0x10` (10h). |
| **`0x01` (01h)** | **R/W** | `SYS_CONTROL` | bit 0: `S_PWR_DN` (Power down sensor)<br>bit 1: `S_PDN_INV`<br>bit 2: `V_TX_EN` (Habilitar transferencia de vídeo)<br>bit 3: `LED` (Control salida LED)<br>bit 4: `KEY` (Estado del botón snapshot)<br>bit 6: `SYS_SEL_24M` (1: 24MHz, 0: 12MHz) |
| **`0x02` (02h)** | **R/W** | `GPIO[1:0]` | Pines GPIO multipropósito [1:0] |
| **`0x08` (08h)** | **R/W** | `I2C_CTRL` | bit 0: `I2C_HIGH` (1: 400kHz, 0: 100kHz)<br>bit 1: `I2C_SEL_RD` (1: Lectura, 0: Escritura)<br>bit 2: `I2C_RDY` (1: Listo, 0: Ocupado)<br>bit 3: `I2C_ERR` (Error I2C)<br>bits [6:4]: `I2C_BYTE_NUM[2:0]` (Número de bytes)<br>bit 7: `I2C_DEV` (1: I2C, 0: 3-wire) |
| **`0x09` (09h)** | **R/W** | `SLAVE_ID` | `SLAVE_ID[6:0]` Dirección I2C del sensor CMOS conectado |
| **`0x0A` - `0x0E`** | **R/W** | `I2C_DATA` | Puerto de 5 bytes para lectura/escritura en el bus I2C del sensor |
| **`0x0F` (0Fh)** | **R/W** | `CTRL_STATUS` | Byte de control y reporte de estado |
| **`0x10` (10h)** | **R/W** | `GAIN_R_B` | bits [3:0]: `R_GAIN[3:0]` (Ganancia Canal Rojo: 1 + R_GAIN/8)<br>bits [7:4]: `B_GAIN[3:0]` (Ganancia Canal Azul: 1 + B_GAIN/8) |
| **`0x11` (11h)** | **R/W** | `GAIN_G` | bits [3:0]: `G_GAIN[3:0]` (Ganancia Canal Verde: 1 + G_GAIN/8) |
| **`0x12` (12h)** | **R/W** | `H_START` | `H_START[7:0]` Píxel inicial activo tras HSYNC |
| **`0x13` (13h)** | **R/W** | `V_START` | `V_START[7:0]` Línea inicial activa tras VSYNC |
| **`0x14` (14h)** | **R/W** | `OFFSET` | `OFFSET[7:0]` Ajuste de offset para datos de imagen del sensor |
| **`0x15` (15h)** | **R/W** | `H_SIZE` | `H_SIZE[5:0]` Tamaño horizontal de píxeles del sensor |
| **`0x16` (16h)** | **R/W** | `V_SIZE_CLK` | bits [4:0]: `V_SIZE[4:0]` Tamaño vertical<br>bit 0: `LQ_SEL` (1: compresión baja calidad, 0: alta)<br>bits [3:2]: `SEN_RATE` (00: Fsys/MCK, 01: 12MHz, 10: 24MHz, 11: 48MHz)<br>bit 4: `TEST_IMG`<br>bit 5: `SEN_CLK_EN` |
| **`0x17` (17h)** | **R/W** | `TIMING_SCAL` | bit 0: `PCK_RIS`<br>bit 1: `HSYNC_RIS`<br>bit 2: `VSYNC_RIS`<br>bit 3: `VSYNC_HIGH`<br>bits [5:4]: `SCAL[1:0]` (00: 1/1, 01: 1/2, 1x: 1/4)<br>bit 6: `SEL_CURVE`<br>bit 7: `CMP_MODE` (1: Habilitar compresión, 0: Sin compresión) |
| **`0x18` (18h)** | **R/W** | `SYNC_CLK_OUT` | Control de sincronización y salidas de reloj de píxeles |
| **`0x19` (19h)** | **R/W** | `MCK_HO_SIZE` | bits [7:4]: `MCK_SIZE[3:0]` Divisor del reloj máster del sensor<br>bits [5:0]: `HO_SIZE[5:0]` (Unidad de 32 píxeles) |
| **`0x1A` (1Ah)** | **R/W** | `VO_SIZE` | bits [4:0]: `VO_SIZE[4:0]` (Unidad de 32 líneas) |
| **`0x1B` - `0x1F`** | **R/W** | `AE_WINDOW` | Ventana de Auto-Exposición (`AE_STRX`, `AE_STRY`, `AE_ENDX`, `AE_ENDY`) |

---

## 3. Formato del Protocolo I2C del Sensor CMOS

El chip SN9C102 actúa como Master I2C (100kHz o 400kHz):
- **Escritura I2C (Write Cycle):** Secuencia de 5 bytes escritos en `0x0A-0x0E`:
  `[Reg_SubAddress, Data0, Data1, Data2, Data3]`.
- **Lectura I2C (Read Cycle):** Fase de escritura de dirección ficticia (*Dummy Write*) + lectura de 1 byte desde el puerto I2C.
