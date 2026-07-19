# Análisis de Registros - SN9C102 (Familia SonixB)

**¡Descubrimiento Clave!**
Tu cámara no usa el protocolo del SN9C20x (rango `0x1000` - `0x11FF`). 
Cuando enviamos a leer el registro `0x1000` y devolvió `0x10`, lo que realmente pasó es que el chip **ignoró el byte superior** y devolvió el registro `0x00`.

El registro `0x00` = `0x10` es la firma inconfundible del **SN9C102 / SN9C105** (Driver `sonixb` de Linux, no `sn9c20x`).

En esta familia, todos los registros del bridge están entre **`0x00` y `0x1F`**.

### Mapa de Registros Conocidos (SN9C1xx / sonixb)

Basado en el driver `sonixb.c` del kernel de Linux:

| Registro (Hex) | Nombre / Función en `sonixb.c` | Notas |
|---|---|---|
| `0x00` | Chip ID / Status | Devuelve `0x10` si el chip está vivo y es un SN9C10x. |
| `0x01` | Control Transfer / Power / Start | `Bit 0`: 1 = Detiene el streaming (Stop). `Bit 5`: Power down del sensor. |
| `0x02` | Clock / Reset | Configura los relojes internos. |
| `0x08 - 0x09` | I2C Control y Address | `0x08` es la base para las comunicaciones I2C en esta familia (en lugar de `0x10C0`). |
| `0x12` | H_START | Inicio horizontal de la ventana de video. |
| `0x13` | V_START | Inicio vertical de la ventana de video. |
| `0x15` | Window Size | Control del tamaño de la imagen (resolución). |
| `0x17` | SensorClk (Reloj del sensor) | Configura la frecuencia del bus de pixeles. |
| `0x18` | Compression Control | Habilita/Deshabilita la compresión por hardware y el formato. |
| `0x19` | MCKSIZE | Frecuencia del reloj maestro. |
| `0x1C` | Auto-Exposure / Window | Parámetros para AE y regiones de interés. |

### Cómo I2C funciona aquí
A diferencia del chip nuevo que usa `0x10C0`, el SN9C102 usa los registros a partir de `0x08` (usualmente `0x08` y `0x09` para comandar, y otros adyacentes para los datos).
