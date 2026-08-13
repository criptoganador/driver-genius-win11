#pragma once

// =========================================================
// sensor_i2c.h
// Módulo de comunicación I2C directa con el sensor CMOS
// vía el puente I2C por hardware del chip SN9C102.
//
// Protocolo de 2 Fases del SN9C102 (Dummy Write + Read):
//   Fase 1 — Dummy Write: se envía la dirección del registro
//             del sensor al bus I2C para apuntar al registro.
//   Fase 2 — Read Phase:  el puente ejecuta la lectura real
//             y deposita el byte en I2C_DATA[0..4].
//
// SECUENCIA SEGURA OBLIGATORIA antes de cualquier transacción:
//   1. restore_slave_address()  → SLAVE_ID = 0x21
//   2. clear_i2c_error()        → Limpia flag I2C_ERR
//   3. Transacción I2C
// =========================================================

#include "device_connector.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <optional>
#include <chrono>
#include <thread>
#include <expected>


namespace Genius {

    enum class I2CError : std::uint8_t {
        BusBusy,        // Flag I2C_RDY no se activó a tiempo (timeout)
        BusError,       // Flag I2C_ERR activado por el puente
        TransferFailed  // ControlTransfer USB fallido
    };

    class SensorI2C {
    public:
        // Dirección correcta del sensor SOI968 / OV7660:
        //   7-bit : 0x21  (dirección estándar del esclavo)
        //   8-bit write: 0x42  (0x21 << 1 | 0x00)
        // El registro SLAVE_ID (0x09) del SN9C102 almacena la
        // dirección en formato de 7 bits desplazada a la izquierda.
        static constexpr std::uint8_t SENSOR_I2C_ADDR    = 0x21; // Dirección esclava 7-bit OV7660/SOI968

        static constexpr std::uint8_t I2C_CTRL_I2C_DEV   = 0x80; // Bit 7: Bus I2C activo (2-wire SCCB)
        static constexpr std::uint8_t I2C_CTRL_SPEED_100K = 0x00; // Bit 0: 0 = Modo Estándar 100 kHz
        static constexpr std::uint8_t I2C_CTRL_RDY       = 0x04; // Bit 2: Transacción completada (RDY = 1)
        static constexpr std::uint8_t I2C_CTRL_ERR       = 0x08; // Bit 3: Error o colisión en el bus (ERR = 1)
        static constexpr std::uint8_t I2C_CTRL_SEL_RD    = 0x02; // Bit 1: Modo lectura (RD = 1)

        // ── BANDERA DE OCUPADO (I2C_BUSY) ──────────────────────────────────
        // El SN9C102 NO tiene un bit dedicado de "BUSY" — el estado ocupado
        // es IMPLÍCITO: el bus está ocupado cuando ni RDY ni ERR están activos.
        //
        // Mapa de bits de I2C_CTRL:
        //   Bit 7 (0x80) = I2C_DEV  → 1 = Bus 2-wire habilitado
        //   Bit 3 (0x08) = ERR      → 1 = Error/NACK detectado (latch)
        //   Bit 2 (0x04) = RDY      → 1 = Transacción completada con éxito
        //   Bit 1 (0x02) = SEL_RD   → 1 = Modo lectura activo
        //   Bit 0 (0x01) = SPEED    → 0 = 100 kHz | 1 = 400 kHz
        //
        // BUSY = (ctrl_byte & I2C_CTRL_BUSY_MASK) == 0
        //        (ninguno de los bits de fin de transacción está levantado)
        static constexpr std::uint8_t I2C_CTRL_BUSY_MASK  = I2C_CTRL_RDY | I2C_CTRL_ERR; // 0x0C

        // Helper: devuelve true si el bus está OCUPADO (transacción en curso)
        // Uso: while (is_i2c_busy(ctrl_byte)) { /* esperar */ }
        [[nodiscard]] static constexpr bool is_i2c_busy(std::uint8_t ctrl_byte) noexcept {
            return (ctrl_byte & I2C_CTRL_BUSY_MASK) == 0x00;
        }

        // Helper: devuelve true si la última transacción terminó con éxito (ACK)
        [[nodiscard]] static constexpr bool is_i2c_ready(std::uint8_t ctrl_byte) noexcept {
            return (ctrl_byte & I2C_CTRL_RDY) != 0;
        }

        // Helper: devuelve true si el bus reporta error (NACK o colisión)
        [[nodiscard]] static constexpr bool is_i2c_error(std::uint8_t ctrl_byte) noexcept {
            return (ctrl_byte & I2C_CTRL_ERR) != 0;
        }

        static constexpr int POLL_MAX_RETRYS   = 100; // Max 100 reintentos
        static constexpr int POLL_INTERVAL_US = 100; // 100 µs por intento → Timeout máximo de 10 ms

        static constexpr int WRITE_MAX_RETRIES  = 3;   // Reintentos por fallo I2C (NACK/ERR)
        static constexpr int WRITE_RETRY_MS     = 5;   // Delay entre reintentos [ms]

        // ───────────────────────────────────────────────────────────────
        // [PASO 3] LECTURA DE ESTADO NO BLOQUEANTE — read_bus_status()
        //
        // Emite exactamente UN WinUsb_ControlTransfer de lectura apuntando
        // al registro I2C_CTRL (0x0008) del SN9C102 y retorna INMEDIATAMENTE.
        //
        // Garantías:
        //   - Sin bucle de polling → no bloquea el hilo
        //   - Sin escrituras → no altera el estado del bus ni del sensor
        //   - Sin efectos secundarios → seguro de llamar antes de cada comando
        //   - Un único ControlTransfer USB (aprox. 0.1ms de latencia)
        //
        // Uso típico:
        //   auto status = SensorI2C::read_bus_status(bridge);
        //   if (status && SensorI2C::is_i2c_busy(*status)) {
        //       // esperar antes de disparar la siguiente transacción
        //   }
        // ───────────────────────────────────────────────────────────────
        [[nodiscard]] static std::optional<std::uint8_t>
        read_bus_status(const DeviceBridge& bridge) noexcept {
            // WinUsb_ControlTransfer único al registro I2C_CTRL (0x0008)
            // RequestType=0xC1 (IN | Vendor | Interface), Request=0x00 (leer registro)
            auto result = bridge.send_register_read(SN9C102::Regs::I2C_CTRL);
            if (!result) return std::nullopt;
            return *result;
        }

        // ── Estructura de salud del bus — decodifica read_bus_status() ──
        struct BusHealth {
            std::uint8_t raw  = 0xFF;   // Byte crudo de I2C_CTRL
            bool bus_enabled  = false;  // Bit 7: Bus 2-wire activo (I2C_DEV)
            bool busy         = false;  // Implícito: ni RDY ni ERR activos
            bool ready        = false;  // Bit 2: Transacción completada (ACK)
            bool error        = false;  // Bit 3: NACK o colisión detectada
            bool read_mode    = false;  // Bit 1: Modo lectura activo (SEL_RD)
            bool fast_mode    = false;  // Bit 0: 1=400kHz | 0=100kHz

            // Imprime un resumen de una línea con todas las banderas
            void print() const noexcept {
                std::cout << "  [I2C_STATUS] raw=0x" << std::hex << std::setw(2)
                          << std::setfill('0') << static_cast<int>(raw) << std::dec
                          << " | BUS=" << (bus_enabled ? "ON" : "OFF")
                          << " | BUSY=" << (busy  ? "1 [ocupado]"    : "0 [libre]")
                          << " | RDY="  << (ready ? "1 [ACK OK]"     : "0")
                          << " | ERR="  << (error ? "1 [NACK/error]" : "0")
                          << " | SPEED="<< (fast_mode ? "400kHz" : "100kHz") << "\n";
            }
        };

        // ── Lee y decodifica el estado del bus en una sola llamada ──
        [[nodiscard]] static std::optional<BusHealth>
        read_bus_health(const DeviceBridge& bridge) noexcept {
            auto raw = read_bus_status(bridge);
            if (!raw) return std::nullopt;

            BusHealth h;
            h.raw         = *raw;
            h.bus_enabled = (*raw & I2C_CTRL_I2C_DEV)   != 0;
            h.ready       = is_i2c_ready(*raw);
            h.error       = is_i2c_error(*raw);
            h.busy        = is_i2c_busy(*raw);
            h.read_mode   = (*raw & I2C_CTRL_SEL_RD)     != 0;
            h.fast_mode   = (*raw & 0x01)                != 0;
            return h;
        }

        // ───────────────────────────────────────────────────────────────
        // [PASO 4, 5 & 6] ESPERA ACTIVA, TIMEOUT Y ERROR MONÁDICO C++23
        //
        // wait_until_not_busy():
        //   - Evalúa si el bus Sonix está ocupado (is_i2c_busy).
        //   - [PASO 5] TIMEOUT DE SEGURIDAD: Límite máximo de 50 reintentos x 1ms = 50ms.
        //   - [PASO 6] TIPO MONÁDICO C++23: Retorna std::expected<void, I2CError>
        //       - Éxito ({}) si el bus queda libre.
        //       - std::unexpected(I2CError::TransferFailed) si el USB no responde.
        //       - std::unexpected(I2CError::BusBusy) si se supera el timeout de 50ms.
        // ───────────────────────────────────────────────────────────────
        [[nodiscard]] static std::expected<void, I2CError>
        wait_until_not_busy(const DeviceBridge& bridge, int max_retries = 50) noexcept {
            int retries = 0;
            while (retries < max_retries) {
                auto status = read_bus_status(bridge);

                // Si la lectura falla (ej. USB desconectado)
                if (!status) {
                    std::cerr << "  [TIMEOUT ERROR] Dispositivo USB no responde (desconexión detectada).\n";
                    return std::unexpected(I2CError::TransferFailed);
                }

                if (!is_i2c_busy(*status)) {
                    return {}; // Bus libre, listo para transmitir
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ++retries;
            }

            // [PASO 6] Retorno explícito del error monádico BusBusy
            std::cerr << "  [TIMEOUT SECURITY] El bus I2C no se liberó tras " << max_retries << " ms. Abortando comando.\n";
            return std::unexpected(I2CError::BusBusy);
        }



        // ---------------------------------------------------------------
        // [PRIMITIVA 1] Restaurar la dirección I2C del esclavo en SLAVE_ID (0x09).
        // DEBE llamarse después de cada escritura en I2C_CTRL (que puede
        // resetear internamente el registro SLAVE_ID a su valor de reset = 0x10).
        // ---------------------------------------------------------------
        [[nodiscard]] static std::expected<void, I2CError>
        restore_slave_address(const DeviceBridge& bridge) noexcept {
            if (!bridge.send_register_write(SN9C102::Regs::SLAVE_ID, SENSOR_I2C_ADDR)) {
                return std::unexpected(I2CError::TransferFailed);
            }
            return {};
        }

        // ---------------------------------------------------------------
        // [PRIMITIVA 2] Limpiar la bandera de error I2C_ERR en el registro
        // I2C_CTRL. El SN9C102 mantiene I2C_ERR (bit 3) latched hasta que
        // se escribe el registro con ese bit a cero.
        // Estrategia: escribir I2C_DEV=1, ERR=0, RDY=0, SPEED=100kHz.
        // ---------------------------------------------------------------
        // Escribir 0x00 a I2C_CTRL para desarmar el secuenciador y limpiar estado
        [[nodiscard]] static std::expected<void, I2CError>
        clear_i2c_error(const DeviceBridge& bridge) noexcept {
            if (!bridge.send_register_write(SN9C102::Regs::I2C_CTRL, static_cast<std::uint8_t>(0x00))) {
                return std::unexpected(I2CError::TransferFailed);
            }
            return {};
        }

        // ───────────────────────────────────────────────────────────────
        // [MODO SCCB - 9º BIT DON'T CARE] Configura el Registro 0x17 (TIMING_SCAL)
        //
        // En el protocolo SCCB de OmniVision/SOI968, la transmisión de 8 bits
        // va seguida de un 9º bit "Don't Care" (NA). El sensor puede no enviar
        // ACK (SDA a LOW) en el 9º ciclo a pesar de procesar el byte.
        //
        // La configuración de TIMING_SCAL (0x17) = 0x68 le indica al SN9C102
        // que utilice la temporización y flancos del protocolo SCCB tolerante,
        // evitando falsas alarmas de error I2C_ERR por ausencia de ACK estricto.
        // ───────────────────────────────────────────────────────────────
        [[nodiscard]] static std::expected<void, I2CError>
        config_sccb_mode(const DeviceBridge& bridge) noexcept {
            constexpr std::uint8_t sccb_timing_val = 0x68; // Flancos SCCB + filtro Don't Care 9º bit
            if (!bridge.send_register_write(SN9C102::Regs::TIMING_SCAL, sccb_timing_val)) {
                std::cerr << "  [ERROR] No se pudo configurar el registro TIMING_SCAL (0x17) para modo SCCB.\n";
                return std::unexpected(I2CError::TransferFailed);
            }
            std::cout << "  [SCCB OK] Registro TIMING_SCAL (0x17) fijado a 0x68 (Modo SCCB 2 hilos, 9º bit Don't Care).\n";
            return {};
        }

        // ---------------------------------------------------------------
        // Inicialización y Validación del Reloj de Comunicación (SIO_C @ 100 kHz).
        // IMPORTANTE: Después de escribir I2C_CTRL, SLAVE_ID y flags quedan
        // en estado de reset → se restauran explícitamente aquí.
        // ---------------------------------------------------------------
        [[nodiscard]] static std::expected<void, I2CError>
        init_i2c_bus_clock(const DeviceBridge& bridge) noexcept {
            // 1. Configurar velocidad del bus a 100 kHz
            constexpr std::uint8_t ctrl_val = I2C_CTRL_I2C_DEV | I2C_CTRL_SPEED_100K;
            if (!bridge.send_register_write(SN9C102::Regs::I2C_CTRL, ctrl_val)) {
                return std::unexpected(I2CError::TransferFailed);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

            // 2. Configurar el modo SCCB tolerante (9º bit Don't Care) en reg 0x17
            if (auto r = config_sccb_mode(bridge); !r) {
                return std::unexpected(r.error());
            }

            // 3. Restaurar SLAVE_ID inmediatamente (I2C_CTRL write lo corrompe)
            if (auto r = restore_slave_address(bridge); !r) {
                return std::unexpected(r.error());
            }

            // 4. Limpiar cualquier flag I2C_ERR residual del arranque
            if (auto r = clear_i2c_error(bridge); !r) {
                return std::unexpected(r.error());
            }

            // 5. Segunda restauración: clear_i2c_error también escribe I2C_CTRL
            if (auto r = restore_slave_address(bridge); !r) {
                return std::unexpected(r.error());
            }

            std::cout << "[PASO 2.1] Reloj de Comunicación (SIO_C) configurado a 100 kHz (Modo Estándar SCCB).\n";
            std::cout << "[PASO 2]   Dirección I2C fijada a 0x"
                      << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(SENSOR_I2C_ADDR) << std::dec
                      << " (SOI968/OV7660). Banderas de bus limpias.\n";

            return {};
        }

        // ---------------------------------------------------------------
        // Escribe un byte en un registro del sensor CMOS vía puente I2C,
        // con hasta WRITE_MAX_RETRIES reintentos si aparece I2C_ERR (NACK).
        // Antes de cada intento: restaura SLAVE_ID y limpia I2C_ERR.
        // ---------------------------------------------------------------
        [[nodiscard]] static std::expected<void, I2CError>
        write_sensor_reg(const DeviceBridge& bridge,
                         std::uint8_t sensor_reg,
                         std::uint8_t value) noexcept
        {
            for (int attempt = 1; attempt <= WRITE_MAX_RETRIES; ++attempt) {
                // ── PASO 4, 5 & 6: Espera activa y chequeo de error monádico ──────
                if (auto busy_check = wait_until_not_busy(bridge, 50); !busy_check) {
                    if (attempt == WRITE_MAX_RETRIES) {
                        return std::unexpected(busy_check.error()); // Retorna I2CError::BusBusy o TransferFailed
                    }
                }

                // ── Preámbulo Seguro ──────────────────────────────────
                if (auto r = restore_slave_address(bridge); !r) {
                    return std::unexpected(I2CError::TransferFailed);
                }

                // ── Cargar datos en buffer I2C (Mapeo Hardware SN9C102 Oficial) ──
                //   SLAVE_ID  (0x09) = Dirección Esclava I2C (0x21 o 0x28)
                //   I2C_DATA0 (0x0A) = Registro del Sensor (ej. 0x12 COM7)
                //   I2C_DATA1 (0x0B) = Valor a Escribir (ej. 0x80)
                if (!bridge.send_register_write(SN9C102::Regs::SLAVE_ID, SENSOR_I2C_ADDR))
                    return std::unexpected(I2CError::TransferFailed);
                if (!bridge.send_register_write(SN9C102::Regs::I2C_DATA0, sensor_reg))
                    return std::unexpected(I2CError::TransferFailed);
                if (!bridge.send_register_write(
                        static_cast<std::uint16_t>(SN9C102::Regs::I2C_DATA0 + 1), value))
                    return std::unexpected(I2CError::TransferFailed);

                // ── Disparar transacción ─────────────────────────────
                // Bit 7 = I2C_DEV (0x80)
                // Bits 6..4 = 0x20 (2 bytes de payload: reg + val)
                // Bit 0 = 0x00 (100 kHz)
                // Total = 0x80 | 0x20 | 0x00 = 0xA0
                constexpr std::uint8_t trigger = I2C_CTRL_I2C_DEV | 0x20 | I2C_CTRL_SPEED_100K;
                if (!bridge.send_register_write(SN9C102::Regs::I2C_CTRL, trigger))
                    return std::unexpected(I2CError::TransferFailed);

                // ── Anti-Colisión POST-TX ─────────────────────────────
                auto post = wait_for_ready(bridge);
                if (post) {
                    return {}; // ✓ Éxito
                }

                // Fallo: I2C_ERR/BusBusy → reintentar si quedan intentos
                if (attempt < WRITE_MAX_RETRIES) {
                    std::cout << "  [REINTENTO " << attempt << "/" << WRITE_MAX_RETRIES
                              << "] Reg 0x" << std::hex << std::setw(2) << std::setfill('0')
                              << static_cast<int>(sensor_reg) << std::dec
                              << " -> I2C_ERR/NACK. Reintentando en " << WRITE_RETRY_MS << "ms...\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(WRITE_RETRY_MS));
                } else {
                    return std::unexpected(post.error());
                }
            }
            return std::unexpected(I2CError::BusError); // No debe alcanzarse
        }

        // ---------------------------------------------------------------
        // Lee un byte de un registro del sensor CMOS vía puente I2C.
        // Protocolo de 2 Fases: Dummy Write (Subdirección) + Read Phase.
        // Restaura SLAVE_ID antes de cada fase.
        // ---------------------------------------------------------------
        [[nodiscard]] static std::expected<std::uint8_t, I2CError>
        read_sensor_reg(const DeviceBridge& bridge, std::uint8_t sensor_reg) noexcept
        {
            // PASO 4, 5 & 6: Espera activa y validación monádica de error
            if (auto busy_check = wait_until_not_busy(bridge, 50); !busy_check) {
                return std::unexpected(busy_check.error()); // Retorna I2CError::BusBusy o TransferFailed
            }

            // Restaurar SLAVE_ID y limpiar errores antes de leer
            if (auto r = restore_slave_address(bridge); !r)
                return std::unexpected(I2CError::TransferFailed);
            if (auto r = clear_i2c_error(bridge); !r)
                return std::unexpected(I2CError::TransferFailed);
            if (auto r = restore_slave_address(bridge); !r)
                return std::unexpected(I2CError::TransferFailed);

            // Anti-Colisión (PRE-TRANSACCIÓN)
            if (auto pre_check = wait_for_ready(bridge); !pre_check) {
                return std::unexpected(pre_check.error());
            }

            // Fase 1: Dummy Write — Apuntar el registro del sensor
            // SLAVE_ID = 0x21/0x28, I2C_DATA0 = sensor_reg
            if (!bridge.send_register_write(SN9C102::Regs::SLAVE_ID, SENSOR_I2C_ADDR))
                return std::unexpected(I2CError::TransferFailed);
            if (!bridge.send_register_write(SN9C102::Regs::I2C_DATA0, sensor_reg))
                return std::unexpected(I2CError::TransferFailed);

            // Trigger Dummy Write: 1 byte subaddress (0x10) -> 0x80 | 0x10 = 0x90
            constexpr std::uint8_t dummy_trigger = I2C_CTRL_I2C_DEV | 0x10 | I2C_CTRL_SPEED_100K;
            if (!bridge.send_register_write(SN9C102::Regs::I2C_CTRL, dummy_trigger))
                return std::unexpected(I2CError::TransferFailed);

            if (auto phase1_check = wait_for_ready(bridge); !phase1_check)
                return std::unexpected(phase1_check.error());

            // Restaurar SLAVE_ID entre fases
            if (auto r = restore_slave_address(bridge); !r)
                return std::unexpected(I2CError::TransferFailed);

            // Fase 2: Read Phase — Activar ráfaga de lectura en el bus
            if (!bridge.send_register_write(SN9C102::Regs::SLAVE_ID, SENSOR_I2C_ADDR))
                return std::unexpected(I2CError::TransferFailed);

            constexpr std::uint8_t rd_trigger = I2C_CTRL_I2C_DEV | 0x10 | I2C_CTRL_SEL_RD | I2C_CTRL_SPEED_100K;
            if (!bridge.send_register_write(SN9C102::Regs::I2C_CTRL, rd_trigger))
                return std::unexpected(I2CError::TransferFailed);

            if (auto phase2_check = wait_for_ready(bridge); !phase2_check)
                return std::unexpected(phase2_check.error());

            // Leer resultado desde I2C_DATA0
            auto result = bridge.send_register_read(SN9C102::Regs::I2C_DATA0);
            if (!result) return std::unexpected(I2CError::TransferFailed);
            return *result;
        }

        // ---------------------------------------------------------------
        // Diagnóstico completo del buffer I2C:
        // Dispara una lectura del sensor_reg y vuelca los 5 bytes del
        // buffer I2C_DATA para encontrar en qué slot llega el dato real.
        // ---------------------------------------------------------------
        static void identify_sensor(const DeviceBridge& bridge) noexcept {
            std::cout << "\n======================================================\n";
            std::cout << "   IDENTIFICACIÓN EXACTA DEL SENSOR CMOS (I2C Direct) \n";
            std::cout << "======================================================\n";
            std::cout << "Protocolo: Dump completo del buffer I2C_DATA[0..4]\n";
            std::cout << "para PID (reg=0x0A) y VER (reg=0x0B) del sensor\n\n";

            const std::uint8_t regs_to_test[] = {0x0A, 0x0B, 0x00, 0x12};
            const char* names[] = {"PID (0x0A)", "VER (0x0B)", "GAIN(0x00)", "COM7(0x12)"};

            for (int r = 0; r < 4; ++r) {
                std::cout << "[I2C] Disparando lectura del registro 0x"
                          << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<int>(regs_to_test[r]) << " ("
                          << names[r] << ")...\n";

                // Restaurar SLAVE_ID antes de cada operación de diagnóstico
                (void)restore_slave_address(bridge);
                (void)clear_i2c_error(bridge);
                (void)restore_slave_address(bridge);

                // Fase 1: Dummy Write (SLAVE_ID + I2C_DATA0=reg)
                (void)bridge.send_register_write(SN9C102::Regs::SLAVE_ID, SENSOR_I2C_ADDR);
                (void)bridge.send_register_write(SN9C102::Regs::I2C_DATA0, regs_to_test[r]);
                (void)bridge.send_register_write(SN9C102::Regs::I2C_CTRL, static_cast<std::uint8_t>(I2C_CTRL_I2C_DEV | 0x10));
                (void)wait_for_ready(bridge);

                // Restaurar entre fases
                (void)restore_slave_address(bridge);

                // Fase 2: Read Phase
                (void)bridge.send_register_write(SN9C102::Regs::I2C_CTRL,
                    static_cast<std::uint8_t>(I2C_CTRL_I2C_DEV | 0x10 | I2C_CTRL_SEL_RD));
                (void)wait_for_ready(bridge);

                // Volcar los 5 bytes del buffer
                std::cout << "  -> DATA[0..4]: ";
                for (int i = 0; i < 5; ++i) {
                    auto v = bridge.send_register_read(
                        static_cast<std::uint16_t>(SN9C102::Regs::I2C_DATA0 + i));
                    std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0')
                              << static_cast<int>(v ? *v : 0xFF) << " ";
                }
                std::cout << std::dec << "\n\n";
            }

            std::cout << "======================================================\n";
            std::cout << "NOTA: El slot cuyo valor cambia según el registro\n";
            std::cout << "solicitado es el que contiene el dato real del sensor.\n";
            std::cout << "======================================================\n\n";
        }

    private:
        // ---------------------------------------------------------------
        // Polling de estado del bus I2C con Timeout Seguro (10ms)
        //
        // Flujo de estados del bus después de disparar una transacción:
        //   1. Bus OCUPADO   → is_i2c_busy(*ctrl) == true  → continuar esperando
        //   2. Bus LISTO     → is_i2c_ready(*ctrl) == true → éxito (ACK del sensor)
        //   3. Bus ERROR     → is_i2c_error(*ctrl) == true → NACK / colisión
        //   4. Timeout 10ms  → retornar BusBusy si ningún flag cambia
        // ---------------------------------------------------------------
        [[nodiscard]] static std::expected<void, I2CError>
        wait_for_ready(const DeviceBridge& bridge) noexcept {
            for (int i = 0; i < POLL_MAX_RETRYS; ++i) {
                auto ctrl = bridge.send_register_read(SN9C102::Regs::I2C_CTRL);
                if (!ctrl) return std::unexpected(I2CError::TransferFailed);

                // Verificar error ANTES que éxito: ERR tiene prioridad
                if (is_i2c_error(*ctrl)) return std::unexpected(I2CError::BusError);

                // Bus listo: transacción completada con ACK del sensor
                if (is_i2c_ready(*ctrl)) return {};

                // Bus ocupado (BUSY): is_i2c_busy(*ctrl) == true → esperar
                std::this_thread::sleep_for(std::chrono::microseconds(POLL_INTERVAL_US));
            }
            return std::unexpected(I2CError::BusBusy); // Timeout: bus bloqueado > 10ms
        }
    };
}
