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
        static constexpr std::uint8_t I2C_CTRL_I2C_DEV = 0x80; // Bit 7: Bus I2C activo
        static constexpr std::uint8_t I2C_CTRL_RDY     = 0x04; // Bit 2: Transacción completada
        static constexpr std::uint8_t I2C_CTRL_ERR     = 0x08; // Bit 3: Error en el bus
        static constexpr std::uint8_t I2C_CTRL_SEL_RD  = 0x02; // Bit 1: Modo lectura
        static constexpr int POLL_MAX = 100;
        static constexpr int POLL_US  = 500;

        // ---------------------------------------------------------------
        // Escribe un byte en un registro del sensor CMOS vía puente I2C
        // ---------------------------------------------------------------
        [[nodiscard]] static std::expected<void, I2CError>
        write_sensor_reg(const DeviceBridge& bridge,
                         std::uint8_t sensor_reg,
                         std::uint8_t value) noexcept
        {
            if (!bridge.send_register_write(SN9C102::Regs::I2C_DATA0, sensor_reg))
                return std::unexpected(I2CError::TransferFailed);
            if (!bridge.send_register_write(
                    static_cast<std::uint16_t>(SN9C102::Regs::I2C_DATA0 + 1), value))
                return std::unexpected(I2CError::TransferFailed);

            // I2C_DEV=1, I2C_BYTE_NUM=1, modo escritura
            if (!bridge.send_register_write(SN9C102::Regs::I2C_CTRL,
                                            static_cast<std::uint8_t>(I2C_CTRL_I2C_DEV | 0x10)))
                return std::unexpected(I2CError::TransferFailed);

            return wait_for_ready(bridge);
        }

        // ---------------------------------------------------------------
        // Lee un byte de un registro del sensor CMOS vía puente I2C
        // Protocolo de 2 fases: Dummy Write + Read
        // ---------------------------------------------------------------
        [[nodiscard]] static std::expected<std::uint8_t, I2CError>
        read_sensor_reg(const DeviceBridge& bridge, std::uint8_t sensor_reg) noexcept
        {
            // Fase 1: Dummy Write — apuntar el registro del sensor
            if (!bridge.send_register_write(SN9C102::Regs::I2C_DATA0, sensor_reg))
                return std::unexpected(I2CError::TransferFailed);

            // I2C_DEV=1, I2C_BYTE_NUM=0, modo escritura ficticia
            if (!bridge.send_register_write(SN9C102::Regs::I2C_CTRL, I2C_CTRL_I2C_DEV))
                return std::unexpected(I2CError::TransferFailed);

            if (auto err = wait_for_ready(bridge); !err)
                return std::unexpected(err.error());

            // Fase 2: Read Phase — activar lectura real del sensor
            if (!bridge.send_register_write(SN9C102::Regs::I2C_CTRL,
                                            static_cast<std::uint8_t>(I2C_CTRL_I2C_DEV | I2C_CTRL_SEL_RD)))
                return std::unexpected(I2CError::TransferFailed);

            if (auto err = wait_for_ready(bridge); !err)
                return std::unexpected(err.error());

            // Leer resultado desde I2C_DATA[0]
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

                // Fase 1: Dummy Write
                bridge.send_register_write(SN9C102::Regs::I2C_DATA0, regs_to_test[r]);
                bridge.send_register_write(SN9C102::Regs::I2C_CTRL, I2C_CTRL_I2C_DEV);
                wait_for_ready(bridge);

                // Fase 2: Read
                bridge.send_register_write(SN9C102::Regs::I2C_CTRL,
                    static_cast<std::uint8_t>(I2C_CTRL_I2C_DEV | I2C_CTRL_SEL_RD));
                wait_for_ready(bridge);

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
        [[nodiscard]] static std::expected<void, I2CError>
        wait_for_ready(const DeviceBridge& bridge) noexcept {
            for (int i = 0; i < POLL_MAX; ++i) {
                auto ctrl = bridge.send_register_read(SN9C102::Regs::I2C_CTRL);
                if (!ctrl) return std::unexpected(I2CError::TransferFailed);
                if (*ctrl & I2C_CTRL_ERR) return std::unexpected(I2CError::BusError);
                if (*ctrl & I2C_CTRL_RDY) return {};
                std::this_thread::sleep_for(std::chrono::microseconds(POLL_US));
            }
            return std::unexpected(I2CError::BusBusy);
        }
    };
}
