# Arquitectura de la Interfaz I2C entre el Puente Sonix (SN9C102) y Sensores CMOS (OV7660/SOI968)

Este documento detalla el funcionamiento interno de la comunicación I2C entre el controlador principal Sonix SN9C102 y el sensor óptico CMOS.

---

## 1. El Puente I2C del SN9C102

El chip procesador SN9C102 no requiere que la computadora ejecute una pila de driver I2C nativa. En su lugar, el SN9C102 incluye un controlador **Master I2C de hardware** accesible a través de transferencias USB Control (`WinUsb_ControlTransfer`).

El puente utiliza 3 áreas principales en su espacio de registros:
- **`0x08` (`I2C_CTRL`):** Estado del bus (100kHz / 400kHz, flags de lectura/escritura, estado ocupado `I2C_RDY` y errores `I2C_ERR`).
- **`0x09` (`SLAVE_ID`):** Guarda la dirección esclava I2C de 7/8 bits del sensor configurado.
- **`0x0A` - `0x0E` (`I2C_DATA`):** Buffer de 5 bytes para enviar la dirección del registro del sensor y sus datos.

---

## 2. Flujo de Transmisión I2C (Escritura y Lectura)

### A. Escritura en Registros del Sensor CMOS
Para escribir en un registro interno del sensor (por ejemplo, escribir `0x80` en el registro `0x12` del OV7660):
1. Se escribe en la posición de memoria I2C del puente la dirección de registro del sensor y su valor.
2. El puente SN9C102 genera la señal de `START` en las líneas SDA y SCL del sensor.
3. El puente transmite la dirección `SLAVE_ID` (`0x50` / `0x42`), la dirección del registro (`0x12`) y el dato (`0x80`).
4. El puente aguarda la señal de confirmación `ACK` del sensor.

### B. Lectura de Registros del Sensor CMOS
La lectura en sensores I2C de cámaras incluye 2 fases (Escritura ficticia + Lectura):
1. **Fase 1 (Dummy Write):** Se envía la dirección del registro que se desea leer.
2. **Fase 2 (Read Phase):** El puente solicita el byte al sensor mediante una lectura I2C y el sensor retorna el valor en SDA.

---

## 3. Estructura C++ de la Interfaz de Sensores (`sn9c102_sensor.h`)

La abstracción modular definida en el proyecto sigue el estándar de diseño de `sn9c102_sensor.h`:

```cpp
namespace Genius {
    struct SensorDriver {
        const char* name;
        std::uint8_t i2c_slave_id;
        std::uint8_t pid_reg;
        std::uint8_t expected_pid;
        
        // Puntero a función de inicialización del sensor
        bool (*init_sensor)(const DeviceBridge& bridge);
    };
}
```

Esta arquitectura modular permite añadir soporte para nuevos sensores (OmniVision, Micron, Hynix) simplemente registrando sus tablas de inicialización.
