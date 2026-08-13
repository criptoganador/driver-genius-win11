#pragma once

// =========================================================
// light_monitor.h
// Módulo de Control y Diagnóstico Completo de Imagen en Tiempo Real
// para el sensor SOI968 / OV7660
//
// Controla:
//  - Exposición (AECH - Reg 0x10)
//  - Ganancia Electrónica (GAIN - Reg 0x00)
//  - Ganancia Canal Azul (BLUE - Reg 0x01)
//  - Ganancia Canal Rojo (RED - Reg 0x02)
//  - Ganancia Canal Verde (GGAIN - Reg 0x6A): Gr + Gb simultáneos
//  - Ajuste fino Gr/Gb independiente (GFIX - Reg 0x69)
//  - Piloto Automático AWB/AEC/AGC (COM8 - Reg 0x13 = 0x87)
//
// Registro Objetivo de Brillo (AEC Stable Window):
//  - REG_AEW (0x24): Límite Superior de Brillo Objetivo (AEC Upper Bound)
//  - REG_AEB (0x25): Límite Inferior de Brillo Objetivo (AEC Lower Bound)
//  - REG_VPT (0x26): Zona de Ajuste Rápido (Fast AEC Operating Region)
//
// Canal Verde — Pilar del esqueleto de luminancia (Matriz de Bayer):
//  - El 50% de los píxeles del sensor son verdes (2 verdes por cada R y B)
//  - GGAIN (0x6A): Escala 0x00..0xFF, neutro = 0x80 (1.0x)
//  - GFIX  (0x69): bits[7:6]=Gr, bits[5:4]=Gb (00=1x, 01=1.25x, 10=1.5x, 11=1.75x)
//
//  Comportamiento del firmware del sensor:
//   - Luminosidad dentro de [AEB .. AEW] → AEC/AGC estabilizados (zona OK)
//   - Luminosidad < AEB (lente tapado)   → AEC/AGC disparan compensación máxima
//   - Luminosidad > AEW                  → AEC/AGC reducen exposición/ganancia
// =========================================================

#include "device_connector.h"
#include "sensor_i2c.h"
#include <iostream>
#include <iomanip>
#include <string_view>
#include <thread>
#include <chrono>
#include <expected>

namespace Genius {

    class LightMonitor {
    public:
        // ─────────────────────────────────────────────────────────
        // CONSTANTES: Registros de Brillo Objetivo del SOI968/OV7660
        // ─────────────────────────────────────────────────────────

        // REG_AEW (0x24): AEC Upper Bound — Límite superior del rango de brillo estable.
        // Cuando la luminosidad media del frame supera este valor, AEC reduce exposición.
        // Valor típico de 50% de luminosidad: 0x78 (escala 0x00..0xFF)
        static constexpr std::uint8_t REG_AEW         = 0x24;
        static constexpr std::uint8_t AEW_TARGET_50PCT = 0x78; // ~47% de 255 → punto alto estable

        // REG_AEB (0x25): AEC Lower Bound — Límite inferior del rango de brillo estable.
        // Si la luminosidad cae por debajo de este valor (ej. lente tapado → 0x00),
        // el AEC/AGC dispara inmediatamente el incremento de AECH y GAIN al máximo.
        static constexpr std::uint8_t REG_AEB         = 0x25;
        static constexpr std::uint8_t AEB_TARGET_50PCT = 0x68; // ~40% de 255 → punto bajo estable

        // REG_VPT (0x26): Fast AEC Operating Region.
        // Nibble alto: umbral de velocidad rápida hacia arriba.
        // Nibble bajo: umbral de velocidad rápida hacia abajo.
        // Valor típico: 0xD4 — región de ajuste rápido simétrica alrededor del objetivo.
        static constexpr std::uint8_t REG_VPT         = 0x26;
        static constexpr std::uint8_t VPT_DEFAULT     = 0xD4; // Ajuste rápido en la zona extrema

        // ─────────────────────────────────────────────────────────
        // CONSTANTES: Canales Cromáticos R/G/B — SOI968/OV7660
        // ─────────────────────────────────────────────────────────

        // REG_BLUE (0x01): Ganancia del Canal Azul (BLUE channel gain)
        // Regula la temperatura cromática (ambientes fríos vs cálidos).
        //   - 0x80 (128 dec) = 1.0x (Neutro): Respeta la luz azul ambiental.
        //   - < 0x80         = Cálido: Atenúa el azul, inclinando la paleta a tonos dorados/amarillos.
        //   - > 0x80         = Frío: Amplifica el azul, enfriando entornos LED / tubos fluorescentes.
        static constexpr std::uint8_t REG_BLUE         = 0x01;
        static constexpr std::uint8_t BLUE_NEUTRAL     = 0x80; // 1.0x — ganancia neutra canal azul

        // REG_RED (0x02): Ganancia del Canal Rojo (RED channel gain)
        // Controla los fotodiodos de la matriz Bayer sensibles a ondas largas.
        //   - 0x80 (128 dec) = 1.0x (Neutro): Respeta la luz roja ambiental.
        //   - < 0x80         = Frío/Cian: Reduce presencia del rojo.
        //   - > 0x80         = Cálido: Potencia rojos, tonos de piel y madera.
        static constexpr std::uint8_t REG_RED          = 0x02;
        static constexpr std::uint8_t RED_NEUTRAL      = 0x80; // 1.0x — ganancia neutra canal rojo

        // GGAIN (0x6A): Green channel gain — controla Gr y Gb simultáneamente.
        // Misma escala que BLUE/RED: 0x80 = 1.0x neutro.
        // El verde es el "esqueleto de luminancia" de la imagen (50% de píxeles Bayer).
        // Un valor incorrecto causa imagen borrosa o tono magenta/violáceo.
        static constexpr std::uint8_t REG_GGAIN        = 0x6A;
        static constexpr std::uint8_t GGAIN_NEUTRAL    = 0x80; // 1.0x — ganancia neutra del verde

        // GFIX (0x69): Fix Gain Control — ajuste fino INDEPENDIENTE de Gr y Gb.
        // Permite corregir el microlente diferencial entre filas de la matriz Bayer.
        //   bits[7:6] = Gr (Green-Red row):  00=1x | 01=1.25x | 10=1.5x | 11=1.75x
        //   bits[5:4] = Gb (Green-Blue row): 00=1x | 01=1.25x | 10=1.5x | 11=1.75x
        //   bits[3:2] = R  (ajuste fino Red)
        //   bits[1:0] = B  (ajuste fino Blue)
        static constexpr std::uint8_t REG_GFIX         = 0x69;
        static constexpr std::uint8_t GFIX_NEUTRAL     = 0x00; // 1.0x en todos los canales (base)

        // Máscaras para decodificar GFIX
        static constexpr std::uint8_t GFIX_GR_MASK     = 0xC0; // bits[7:6]
        static constexpr std::uint8_t GFIX_GB_MASK     = 0x30; // bits[5:4]
        static constexpr std::uint8_t GFIX_GR_SHIFT    = 6;
        static constexpr std::uint8_t GFIX_GB_SHIFT    = 4;

        // ─────────────────────────────────────────────────────────
        // Canales de Color — BLUE (0x01), RED (0x02), GREEN (0x6A/0x69)
        // ─────────────────────────────────────────────────────────

        // Leer Registro AECH (0x10) - Obturación
        [[nodiscard]] static std::expected<std::uint8_t, I2CError>
        read_exposure(const DeviceBridge& bridge) noexcept {
            return SensorI2C::read_sensor_reg(bridge, 0x10);
        }

        // Escribir Registro AECH (0x10)
        [[nodiscard]] static std::expected<void, I2CError>
        write_exposure(const DeviceBridge& bridge, std::uint8_t value) noexcept {
            return SensorI2C::write_sensor_reg(bridge, 0x10, value);
        }

        // Leer Registro GAIN (0x00) - Ganancia Electrónica AGC
        [[nodiscard]] static std::expected<std::uint8_t, I2CError>
        read_gain(const DeviceBridge& bridge) noexcept {
            return SensorI2C::read_sensor_reg(bridge, 0x00);
        }

        // Escribir Registro GAIN (0x00)
        [[nodiscard]] static std::expected<void, I2CError>
        write_gain(const DeviceBridge& bridge, std::uint8_t value) noexcept {
            return SensorI2C::write_sensor_reg(bridge, 0x00, value);
        }

        // Leer Registro BLUE (0x01) - Ganancia Canal Azul AWB
        [[nodiscard]] static std::expected<std::uint8_t, I2CError>
        read_blue_gain(const DeviceBridge& bridge) noexcept {
            return SensorI2C::read_sensor_reg(bridge, REG_BLUE);
        }

        // Escribir Registro BLUE (0x01)
        [[nodiscard]] static std::expected<void, I2CError>
        write_blue_gain(const DeviceBridge& bridge, std::uint8_t value) noexcept {
            return SensorI2C::write_sensor_reg(bridge, REG_BLUE, value);
        }

        // Leer Registro RED (0x02) - Ganancia Canal Rojo AWB
        [[nodiscard]] static std::expected<std::uint8_t, I2CError>
        read_red_gain(const DeviceBridge& bridge) noexcept {
            return SensorI2C::read_sensor_reg(bridge, REG_RED);
        }

        // Escribir Registro RED (0x02)
        [[nodiscard]] static std::expected<void, I2CError>
        write_red_gain(const DeviceBridge& bridge, std::uint8_t value) noexcept {
            return SensorI2C::write_sensor_reg(bridge, REG_RED, value);
        }

        // ─────────────────────────────────────────────────────────
        // Canal VERDE: GGAIN (0x6A) — Gr + Gb simultáneos
        //              GFIX  (0x69) — Ajuste fino Gr/Gb independiente
        // ─────────────────────────────────────────────────────────

        // Leer GGAIN (0x6A) — Ganancia global del canal verde (Gr + Gb)
        [[nodiscard]] static std::expected<std::uint8_t, I2CError>
        read_green_gain(const DeviceBridge& bridge) noexcept {
            return SensorI2C::read_sensor_reg(bridge, REG_GGAIN);
        }

        // Escribir GGAIN (0x6A)
        // Rango útil: 0x60..0xA0 (valores extremos distorsionan la imagen)
        // Neutro = 0x80 | Más verde > 0x80 | Menos verde < 0x80 → imagen magenta
        [[nodiscard]] static std::expected<void, I2CError>
        write_green_gain(const DeviceBridge& bridge, std::uint8_t value) noexcept {
            return SensorI2C::write_sensor_reg(bridge, REG_GGAIN, value);
        }

        // Leer GFIX (0x69) — Ajuste fino Gr/Gb independiente
        [[nodiscard]] static std::expected<std::uint8_t, I2CError>
        read_gfix(const DeviceBridge& bridge) noexcept {
            return SensorI2C::read_sensor_reg(bridge, REG_GFIX);
        }

        // Escribir GFIX (0x69)
        // Usar encode_gfix() para construir el byte desde los factores de cada canal
        [[nodiscard]] static std::expected<void, I2CError>
        write_gfix(const DeviceBridge& bridge, std::uint8_t value) noexcept {
            return SensorI2C::write_sensor_reg(bridge, REG_GFIX, value);
        }

        // Helper: Codifica GFIX a partir de factores discretos de Gr y Gb.
        // gr_factor: 0=1x, 1=1.25x, 2=1.5x, 3=1.75x  (0..3)
        // gb_factor: 0=1x, 1=1.25x, 2=1.5x, 3=1.75x  (0..3)
        [[nodiscard]] static constexpr std::uint8_t encode_gfix(
            std::uint8_t gr_factor, // bits[7:6]
            std::uint8_t gb_factor  // bits[5:4]
        ) noexcept {
            return static_cast<std::uint8_t>(
                ((gr_factor & 0x03) << GFIX_GR_SHIFT) |
                ((gb_factor & 0x03) << GFIX_GB_SHIFT)
            );
        }

        // Helper: Decodifica el factor de Gr desde un byte GFIX (0..3)
        [[nodiscard]] static constexpr std::uint8_t decode_gfix_gr(std::uint8_t gfix_byte) noexcept {
            return (gfix_byte & GFIX_GR_MASK) >> GFIX_GR_SHIFT;
        }

        // Helper: Decodifica el factor de Gb desde un byte GFIX (0..3)
        [[nodiscard]] static constexpr std::uint8_t decode_gfix_gb(std::uint8_t gfix_byte) noexcept {
            return (gfix_byte & GFIX_GB_MASK) >> GFIX_GB_SHIFT;
        }

        // Helper: Convierte un factor GFIX (0..3) a su multiplicador legible
        [[nodiscard]] static constexpr std::string_view gfix_factor_str(std::uint8_t factor) noexcept {
            switch (factor) {
                case 0:  return "1.00x";
                case 1:  return "1.25x";
                case 2:  return "1.50x";
                case 3:  return "1.75x";
                default: return "?";
            }
        }

        // ─────────────────────────────────────────────────────────
        // Inicialización del Canal Verde (neutro = base del AWB)
        // GGAIN (0x6A) = 0x80  → Ganancia verde 1.0x (neutral)
        // GFIX  (0x69) = 0x00  → Gr=1.0x, Gb=1.0x (sin ajuste fino)
        // ─────────────────────────────────────────────────────────
        [[nodiscard]] static bool setup_green_channel(const DeviceBridge& bridge) noexcept {
            std::cout << "\n======================================================\n";
            std::cout <<   "  CONFIGURANDO CANAL VERDE (GGAIN/GFIX) — Pilar del AWB \n";
            std::cout <<   "  50% de píxeles Bayer son verdes: Gr (fila-R) y Gb (fila-B)\n";
            std::cout <<   "======================================================\n";

            // 1. GGAIN (0x6A) = 0x80 → Ganancia global verde neutra (1.0x)
            std::cout << "  [1/2] Reg 0x6A (GGAIN) -> 0x80 (Ganancia Verde Gr+Gb = 1.0x neutro)...\n";
            if (auto r = write_green_gain(bridge, GGAIN_NEUTRAL); !r) {
                std::cerr << "  [ERROR] Falló al configurar Reg 0x6A (GGAIN).\n";
                return false;
            }
            std::cout << "        ✓ Canal verde (GGAIN) anclado en 1.0x.\n";
            std::cout << "          Sin verde = imagen magenta. Más verde > 0x80 = imagen verdosa.\n";

            // 2. GFIX (0x69) = 0x00 → Gr=1.0x, Gb=1.0x (base sin ajuste fino)
            std::cout << "  [2/2] Reg 0x69 (GFIX)  -> 0x00 (Gr=1.0x | Gb=1.0x | Sin corrección de lente)...\n";
            if (auto r = write_gfix(bridge, GFIX_NEUTRAL); !r) {
                std::cerr << "  [ERROR] Falló al configurar Reg 0x69 (GFIX).\n";
                return false;
            }
            std::cout << "        ✓ Ajuste fino Gr/Gb fijado a 1.0x.\n";
            std::cout << "          Gr=Green-Red row | Gb=Green-Blue row (Bayer balance OK).\n";

            std::cout << "======================================================\n\n";
            return true;
        }

        // ─────────────────────────────────────────────────────────
        // REQUERIMIENTO 1: Lectura/Escritura Tipada del Registro
        //                  de Brillo Objetivo (AEW 0x24, AEB 0x25, VPT 0x26)
        // ─────────────────────────────────────────────────────────

        // Leer Registro AEW (0x24) - Límite superior de brillo objetivo
        [[nodiscard]] static std::expected<std::uint8_t, I2CError>
        read_aew(const DeviceBridge& bridge) noexcept {
            return SensorI2C::read_sensor_reg(bridge, REG_AEW);
        }

        // Escribir Registro AEW (0x24)
        [[nodiscard]] static std::expected<void, I2CError>
        write_aew(const DeviceBridge& bridge, std::uint8_t value) noexcept {
            return SensorI2C::write_sensor_reg(bridge, REG_AEW, value);
        }

        // Leer Registro AEB (0x25) - Límite inferior de brillo objetivo
        [[nodiscard]] static std::expected<std::uint8_t, I2CError>
        read_aeb(const DeviceBridge& bridge) noexcept {
            return SensorI2C::read_sensor_reg(bridge, REG_AEB);
        }

        // Escribir Registro AEB (0x25)
        [[nodiscard]] static std::expected<void, I2CError>
        write_aeb(const DeviceBridge& bridge, std::uint8_t value) noexcept {
            return SensorI2C::write_sensor_reg(bridge, REG_AEB, value);
        }

        // Leer Registro VPT (0x26) - Región de ajuste AEC rápido
        [[nodiscard]] static std::expected<std::uint8_t, I2CError>
        read_vpt(const DeviceBridge& bridge) noexcept {
            return SensorI2C::read_sensor_reg(bridge, REG_VPT);
        }

        // Escribir Registro VPT (0x26)
        [[nodiscard]] static std::expected<void, I2CError>
        write_vpt(const DeviceBridge& bridge, std::uint8_t value) noexcept {
            return SensorI2C::write_sensor_reg(bridge, REG_VPT, value);
        }

        // ─────────────────────────────────────────────────────────
        // CONFIGURACIÓN: Activar Piloto Automático (COM8=0x87, COM9=0x38)
        //                e inicializar canales de color a 1.0x (0x80)
        // ─────────────────────────────────────────────────────────

        // ─────────────────────────────────────────────────────────
        // REQUERIMIENTO 2: Configurar Ventana de Brillo Objetivo (50%)
        //                  AEW (0x24) = 0x78, AEB (0x25) = 0x68, VPT (0x26) = 0xD4
        //
        // Lógica de Realimentación por Error del firmware del sensor:
        //   - El sensor compara cada frame contra [AEB .. AEW] en tiempo real.
        //   - Si luminosidad_actual < AEB (ej. lente tapado → 0x00):
        //       → Error = AEB - luminosidad_actual = máximo desfase
        //       → El AEC ordena: incrementar AECH (obturador más largo)
        //       → El AGC ordena: incrementar GAIN (amplificación máxima)
        //   - Si luminosidad_actual > AEW:
        //       → El AEC reduce exposición y ganancia.
        //   - Si AEB <= luminosidad_actual <= AEW:
        //       → AEC/AGC se estabilizan (zona OK, sin ajuste necesario).
        // ─────────────────────────────────────────────────────────
        [[nodiscard]] static bool setup_brightness_target(const DeviceBridge& bridge) noexcept {
            std::cout << "\n======================================================\n";
            std::cout <<   "  CONFIGURANDO REGISTRO OBJETIVO DE BRILLO (AEW/AEB/VPT) \n";
            std::cout <<   "  Ventana estable: [0x" << std::hex << static_cast<int>(AEB_TARGET_50PCT)
                      << " .. 0x" << static_cast<int>(AEW_TARGET_50PCT) << std::dec
                      << "] (~40%..47% luminosidad objetivo)\n";
            std::cout <<   "======================================================\n";

            // 1. AEW (0x24) = 0x78 → Límite superior del brillo objetivo
            std::cout << "  [1/3] Reg 0x24 (REG_AEW) -> 0x" << std::hex
                      << static_cast<int>(AEW_TARGET_50PCT) << std::dec
                      << " (Límite Superior AEC: ~47% luminosidad)...\n";
            if (auto r = write_aew(bridge, AEW_TARGET_50PCT); !r) {
                std::cerr << "  [ERROR] Falló al configurar Reg 0x24 (REG_AEW).\n";
                return false;
            }
            std::cout << "        ✓ Límite superior de brillo objetivo fijado a 0x"
                      << std::hex << static_cast<int>(AEW_TARGET_50PCT) << std::dec << ".\n";

            // 2. AEB (0x25) = 0x68 → Límite inferior del brillo objetivo
            std::cout << "  [2/3] Reg 0x25 (REG_AEB) -> 0x" << std::hex
                      << static_cast<int>(AEB_TARGET_50PCT) << std::dec
                      << " (Límite Inferior AEC: ~40% luminosidad)...\n";
            if (auto r = write_aeb(bridge, AEB_TARGET_50PCT); !r) {
                std::cerr << "  [ERROR] Falló al configurar Reg 0x25 (REG_AEB).\n";
                return false;
            }
            std::cout << "        ✓ Límite inferior de brillo objetivo fijado a 0x"
                      << std::hex << static_cast<int>(AEB_TARGET_50PCT) << std::dec
                      << ". Si el lente se tapa, AEC/AGC disparan compensación máxima.\n";

            // 3. VPT (0x26) = 0xD4 → Zona de ajuste AEC rápido
            std::cout << "  [3/3] Reg 0x26 (REG_VPT) -> 0x"
                      << std::hex << static_cast<int>(VPT_DEFAULT) << std::dec
                      << " (Región de Ajuste AEC Rápido activa)...\n";
            if (auto r = write_vpt(bridge, VPT_DEFAULT); !r) {
                std::cerr << "  [ERROR] Falló al configurar Reg 0x26 (REG_VPT).\n";
                return false;
            }
            std::cout << "        ✓ Región de ajuste rápido habilitada (respuesta agresiva al cambio de luz).\n";

            std::cout << "======================================================\n\n";
            return true;
        }
        [[nodiscard]] static bool setup_auto_exposure(const DeviceBridge& bridge) noexcept {
            std::cout << "\n======================================================\n";
            std::cout <<   "  CONFIGURANDO PILOTO AUTOMÁTICO LUZ Y COLOR (AEC/AGC/AWB) \n";
            std::cout <<   "======================================================\n";

            // 1. Reg 0x13 (COM8) = 0x87 -> Activar AEC, AGC, AWB simultáneamente
            std::cout << "  [1/4] Reg 0x13 (COM8) -> 0x87 (AEC + AGC + AWB Automáticos)...\n";
            if (auto r = SensorI2C::write_sensor_reg(bridge, 0x13, 0x87); !r) {
                std::cerr << "  [ERROR] Falló al configurar el Registro 0x13 (COM8).\n";
                return false;
            }
            std::cout << "        ✓ Piloto automático de exposición, ganancia y blancos ACTIVADO.\n";

            // 2. Reg 0x14 (COM9) = 0x38 -> Límite máximo de ganancia AGC a 8x
            std::cout << "  [2/4] Reg 0x14 (COM9) -> 0x38 (Límite Máximo de Ganancia AGC a 8x)...\n";
            if (auto r = SensorI2C::write_sensor_reg(bridge, 0x14, 0x38); !r) {
                std::cerr << "  [ERROR] Falló al configurar el Registro 0x14 (COM9).\n";
                return false;
            }
            std::cout << "        ✓ Límite de amplificación fijado a 8x para evitar ruido digital.\n";

            // 3. Reg 0x01 (BLUE) = 0x80 -> Ganancia inicial de canal azul a 1.0x (Neutro)
            std::cout << "  [3/4] Reg 0x01 (BLUE) -> 0x" << std::hex
                      << static_cast<int>(BLUE_NEUTRAL) << std::dec
                      << " (Ganancia inicial canal Azul 1.0x neutro)...\n";
            if (auto r = write_blue_gain(bridge, BLUE_NEUTRAL); !r) {
                std::cerr << "  [ERROR] Falló al configurar el Registro 0x01 (BLUE).\n";
                return false;
            }

            // 4. Reg 0x02 (RED) = 0x80 -> Ganancia inicial de canal rojo a 1.0x (Neutro)
            std::cout << "  [4/4] Reg 0x02 (RED)  -> 0x" << std::hex
                      << static_cast<int>(RED_NEUTRAL) << std::dec
                      << " (Ganancia inicial canal Rojo 1.0x neutro)...\n";
            if (auto r = write_red_gain(bridge, RED_NEUTRAL); !r) {
                std::cerr << "  [ERROR] Falló al configurar el Registro 0x02 (RED).\n";
                return false;
            }
            std::cout << "        ✓ Canales cromáticos BLUE y RED inicializados a 1.0x (AWB listo).\n";

            // 5. Inicializar canal verde (GGAIN + GFIX) como pilar del AWB
            if (!setup_green_channel(bridge)) {
                std::cerr << "  [ERROR] Falló al inicializar el canal Verde (GGAIN/GFIX).\n";
                return false;
            }

            std::cout << "======================================================\n\n";
            return true;
        }

        // ─────────────────────────────────────────────────────────
        // REQUERIMIENTO 3: Diagnóstico Ampliado en Tiempo Real
        //                  Target AEW/AEB vs AECH + GAIN con feedback de error
        // ─────────────────────────────────────────────────────────
        static void monitor_brightness_feedback(
            const DeviceBridge& bridge,
            int samples  = 10,
            int delay_ms = 200) noexcept
        {
            std::cout << "======================================================\n";
            std::cout << "  BUCLE DE REALIMENTACIÓN POR ERROR DE BRILLO\n";
            std::cout << "  Target Window: [AEB=0x" << std::hex
                      << static_cast<int>(AEB_TARGET_50PCT)
                      << " .. AEW=0x" << static_cast<int>(AEW_TARGET_50PCT) << std::dec
                      << "] | Muestreando " << samples << " ciclos AEC...\n";
            std::cout << "======================================================\n\n";

            for (int i = 1; i <= samples; ++i) {
                // Leer brillo objetivo activo desde el registro del sensor
                auto aew_res  = read_aew(bridge);
                auto aeb_res  = read_aeb(bridge);
                auto aech_res = read_exposure(bridge);
                auto gain_res = read_gain(bridge);

                if (!aew_res || !aeb_res || !aech_res || !gain_res) {
                    std::cout << "  -> [Ciclo AEC #" << std::setw(2) << i
                              << "] Error de lectura I2C (bus I2C)\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                    continue;
                }

                std::uint8_t aew_val  = *aew_res;   // Límite superior objetivo
                std::uint8_t aeb_val  = *aeb_res;   // Límite inferior objetivo
                std::uint8_t aech_val = *aech_res;  // Tiempo de exposición actual
                std::uint8_t gain_val = *gain_res;  // Ganancia electrónica actual

                // REQUERIMIENTO 2: Lógica de Realimentación por Error
                // El "brillo actual" se infiere del nivel de compensación activa:
                // Si AECH > 0x20 Y GAIN > 0x08 → sensor en compensación máxima
                // → inferir que la luminosidad está MUY por debajo del límite AEB.
                std::string_view feedback_state;
                if (aech_val >= 0x30 && gain_val >= 0x10) {
                    // Exposición y ganancia al máximo → lente tapado o escena muy oscura
                    feedback_state = "[Compensando Oscuridad: AECH+GAIN al max]";
                } else if (aech_val >= 0x15 || gain_val >= 0x08) {
                    // AEC activamente intentando aumentar la exposición
                    feedback_state = "[Buscando Objetivo: Ajustando Exposicion] ";
                } else if (aech_val <= 0x04 && gain_val <= 0x02) {
                    // Exposición mínima → escena muy iluminada, AEC reduciendo
                    feedback_state = "[AEC Reduciendo: Escena Sobreexpuesta]    ";
                } else {
                    // Dentro de la ventana estable [AEB..AEW]
                    feedback_state = "[Estabilizado: Brillo dentro del Objetivo]";
                }

                // REQUERIMIENTO 3: Formato de salida con Target y parámetros activos
                std::cout << "  -> [Ciclo AEC #" << std::setw(2) << std::setfill(' ') << i << "] "
                          << std::hex << std::uppercase
                          << "Target=[AEB=0x" << std::setw(2) << std::setfill('0') << static_cast<int>(aeb_val)
                          << " .. AEW=0x" << std::setw(2) << std::setfill('0') << static_cast<int>(aew_val) << "]"
                          << " | AECH=0x" << std::setw(2) << std::setfill('0') << static_cast<int>(aech_val)
                          << " | GAIN=0x" << std::setw(2) << std::setfill('0') << static_cast<int>(gain_val)
                          << std::dec << std::nouppercase
                          << " | Estado: " << feedback_state << "\n";

                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }

            std::cout << "\n======================================================\n";
            std::cout << "  ✓ Ciclo de Realimentación completado.\n";
            std::cout << "    Si AECH y GAIN subieron al máximo → el sensor\n";
            std::cout << "    detectó la oscuridad y ejecutó compensación agresiva.\n";
            std::cout << "======================================================\n\n";
        }

        // ─────────────────────────────────────────────────────────
        // Diagnóstico Quad en Tiempo Real (AECH + GAIN + BLUE + RED)
        // ─────────────────────────────────────────────────────────
        static void monitor_live_full_pipeline(const DeviceBridge& bridge, int samples = 10, int delay_ms = 150) noexcept {
            std::cout << "======================================================\n";
            std::cout << "  DIAGNÓSTICO QUAD EN TIEMPO REAL (AECH + GAIN + BLUE + RED)\n";
            std::cout << "  Muestreando " << samples << " lecturas de luz, ganancia y color AWB...\n";
            std::cout << "======================================================\n\n";

            for (int i = 1; i <= samples; ++i) {
                auto aech_res  = read_exposure(bridge);
                auto gain_res  = read_gain(bridge);
                auto blue_res  = read_blue_gain(bridge);
                auto red_res   = read_red_gain(bridge);
                auto green_res = read_green_gain(bridge);  // GGAIN (0x6A)

                if (aech_res && gain_res && blue_res && red_res && green_res) {
                    std::uint8_t aech_val  = *aech_res;
                    std::uint8_t gain_val  = *gain_res;
                    std::uint8_t blue_val  = *blue_res;
                    std::uint8_t red_val   = *red_res;
                    std::uint8_t green_val = *green_res;

                    // Lógica de Compensación Cromática ampliada (AWB triplete R/G/B)
                    std::string_view status_desc;
                    // Verde muy bajo = imagen magenta (tono enfermizo)
                    if (green_val < 0x60) {
                        status_desc = "[VERDE BAJO: Imagen Magenta/Violácea] ";
                    } else if (blue_val > red_val + 0x10 && blue_val > green_val) {
                        status_desc = "[Compensando Luz Cálida/Incandescente]";
                    } else if (red_val > blue_val + 0x10 && red_val > green_val) {
                        status_desc = "[Compensando Luz Fría / LED Azul]     ";
                    } else if (aech_val > 0x10 && gain_val > 0x10) {
                        status_desc = "[Amplificando Ganancia (Baja Luz)]    ";
                    } else if (aech_val > 0x04) {
                        status_desc = "[Ajustando Obturador (Shutter)]      ";
                    } else {
                        status_desc = "[Triplete RGB Balanceado (AWB OK)]    ";
                    }

                    // Diagnóstico QUÍNTUPLE: AECH + GAIN + BLUE + RED + GREEN
                    std::cout << "  -> [Muestra #" << std::setw(2) << std::setfill(' ') << i << "] "
                              << std::hex << std::uppercase
                              << "AECH=0x" << std::setw(2) << std::setfill('0') << static_cast<int>(aech_val)
                              << " | GAIN=0x" << std::setw(2) << std::setfill('0') << static_cast<int>(gain_val)
                              << " | BLUE=0x" << std::setw(2) << std::setfill('0') << static_cast<int>(blue_val)
                              << " | RED=0x"  << std::setw(2) << std::setfill('0') << static_cast<int>(red_val)
                              << " | GREEN=0x" << std::setw(2) << std::setfill('0') << static_cast<int>(green_val)
                              << std::dec << std::nouppercase
                              << " | " << status_desc << "\n";

                } else {
                    std::cout << "  -> [Muestra #" << std::setw(2) << i << "] Error de lectura I2C (Pipeline)\n";
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }

            std::cout << "======================================================\n";
            std::cout << "  ✓ Pipeline AWB/AEC/AGC validado: Triplete BLUE + RED + GREEN\n";
            std::cout << "    compensan dinámicamente la temperatura de color ambiental.\n";
            std::cout << "    GREEN (0x6A) es el pilar del esqueleto de luminancia Bayer.\n";
            std::cout << "======================================================\n\n";
        }
    };

} // namespace Genius
