#pragma once

// =========================================================
// sensor_scanner.h
// Herramienta de Escaneo Automático de Comandos y Hardware I2C
//
// Realiza un barrido exhaustivo sobre:
//  1. Todas las combinaciones de GPIO (Reg 0x02) -> Libera RESETB / VCC
//  2. Todas las Frecuencias de Reloj MCLK (Reg 0x16) -> Busca activación de oscilador
//  3. Todas las Direcciones I2C (0x01 a 0x7F) -> Encuentra el Slave ID exacto con ACK
// =========================================================

#include "device_connector.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <thread>
#include <cstdint>

namespace Genius {

    struct ScanResult {
        std::uint8_t gpio_val;
        std::uint8_t clk_val;
        std::uint8_t slave_id;
        bool ack_received;
        std::uint8_t pid_ver_read;
    };

    class SensorScanner {
    public:
        // ---------------------------------------------------------------
        // Ejecuta el escaneo completo de Hardware para descubrir los
        // comandos exactos que necesita la cámara para responder.
        // ---------------------------------------------------------------
        static void run_full_diagnostic(const DeviceBridge& bridge) noexcept {
            std::cout << "\n======================================================\n";
            std::cout << "  INICIANDO ESCANEO EXHAUSTIVO DE COMANDOS DE HARDWARE \n";
            std::cout << "======================================================\n";
            std::cout << "Probando combinaciones de GPIO, MCLK y Direcciones I2C...\n\n";

            // Direcciones I2C conocidas de sensores webcams comunes:
            // 0x21 (OV7660/SOI968), 0x28 (SOI968 alt), 0x30 (OV7648), 0x42 (8-bit write), 0x60 (OV6650), 0x10 (PAS106)
            const std::uint8_t priority_addrs[] = {
                0x21, 0x28, 0x42, 0x30, 0x10, 0x60, 0x78, 0x3c, 0x48, 0x50, 0x5d
            };

            const std::uint8_t gpio_candidates[] = { 0x00, 0x01, 0x02, 0x03, 0x05, 0x07 };
            const std::uint8_t clk_candidates[]  = { 0x24, 0x04, 0x14, 0x44, 0x00 };

            std::vector<ScanResult> successful_finds;

            // 1. Probar combinaciones prioritarias primero
            std::cout << "[FASE 1] Escaneando Direcciones Esclavas con Reset de GPIO...\n";

            for (std::uint8_t gpio : gpio_candidates) {
                // Escribir GPIO en SN9C102 (Reg 0x02)
                (void)bridge.send_register_write(SN9C102::Regs::GPIO, gpio);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                for (std::uint8_t clk : clk_candidates) {
                    // Configurar V_SIZE_CLK (Reg 0x16)
                    (void)bridge.send_register_write(SN9C102::Regs::V_SIZE_CLK, clk);
                    (void)bridge.send_register_write(SN9C102::Regs::SYS_CONTROL, 0x04);
                    (void)bridge.send_register_write(SN9C102::Regs::TIMING_SCAL, 0x68);
                    (void)bridge.send_register_write(SN9C102::Regs::SYNC_CLK_OUT, 0x8f);
                    (void)bridge.send_register_write(SN9C102::Regs::MCK_HO_SIZE, 0x20);

                    std::this_thread::sleep_for(std::chrono::milliseconds(20));

                    for (std::uint8_t addr : priority_addrs) {
                        bool ack = probe_i2c_address(bridge, addr);
                        if (ack) {
                            std::cout << "  [¡ÉXITO ENCONTRADO!] GPIO=0x"
                                      << std::hex << std::setw(2) << std::setfill('0') << (int)gpio
                                      << " | CLK=0x" << std::setw(2) << (int)clk
                                      << " | SLAVE_ID=0x" << std::setw(2) << (int)addr
                                      << std::dec << " → ¡ACK RECIBIDO EN EL BUS I2C!\n";

                            successful_finds.push_back({gpio, clk, addr, true, 0x00});
                        }
                    }
                }
            }

            // 2. Si no hay éxito en prioritarias, hacer barrido completo 0x01..0x7F
            if (successful_finds.empty()) {
                std::cout << "\n[FASE 2] Barrido completo I2C de 7-bits (0x01 a 0x7F)...\n";
                // Fijar GPIO a 0x01 y CLK a 0x24
                (void)bridge.send_register_write(SN9C102::Regs::GPIO, 0x01);
                (void)bridge.send_register_write(SN9C102::Regs::V_SIZE_CLK, 0x24);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));

                for (std::uint16_t addr = 0x01; addr <= 0x7F; ++addr) {
                    bool ack = probe_i2c_address(bridge, static_cast<std::uint8_t>(addr));
                    if (ack) {
                        std::cout << "  [¡ACK DETECTADO!] Dirección I2C=0x"
                                  << std::hex << std::setw(2) << std::setfill('0') << addr
                                  << std::dec << " respondió con éxito.\n";
                        successful_finds.push_back({0x01, 0x24, static_cast<std::uint8_t>(addr), true, 0x00});
                    }
                }
            }

            // 3. Imprimir Reporte Resumen
            std::cout << "\n======================================================\n";
            std::cout << "         RESUMEN DE COMANDOS DE HARDWARE EXACTOS        \n";
            std::cout << "======================================================\n";

            if (successful_finds.empty()) {
                std::cout << "  [ALERTA] Ninguna dirección I2C devolvió ACK.\n";
                std::cout << "  Posible causa: El sensor requiere un pulso de RESET en GPIO especifíco,\n";
                std::cout << "  o la línea de energía del sensor utiliza un pin GPIO diferente.\n";
            } else {
                std::cout << "  ¡Se encontraron " << successful_finds.size() << " configuraciones funcionales!\n\n";
                std::cout << "  COMANDOS EXACTOS A USAR EN C++:\n";
                for (const auto& item : successful_finds) {
                    std::cout << "   - send_register_write(0x0002, 0x" << std::hex << (int)item.gpio_val << "); // GPIO\n";
                    std::cout << "   - send_register_write(0x0016, 0x" << (int)item.clk_val << "); // V_SIZE_CLK\n";
                    std::cout << "   - send_register_write(0x0009, 0x" << (int)item.slave_id << "); // SLAVE_ID\n";
                }
            }
            std::cout << "======================================================\n\n";
        }

    private:
        // Prueba si una dirección I2C responde con ACK (I2C_RDY = 1 y I2C_ERR = 0)
        [[nodiscard]] static bool probe_i2c_address(const DeviceBridge& bridge, std::uint8_t slave_addr) noexcept {
            // 1. Limpiar I2C_CTRL y fijar SLAVE_ID
            (void)bridge.send_register_write(SN9C102::Regs::I2C_CTRL, 0x80);
            (void)bridge.send_register_write(SN9C102::Regs::SLAVE_ID, slave_addr);

            // 2. Intentar Escritura I2C al registro PID 0x0A del sensor
            (void)bridge.send_register_write(SN9C102::Regs::I2C_DATA0, static_cast<std::uint8_t>(0x0A));
            (void)bridge.send_register_write(
                static_cast<std::uint16_t>(SN9C102::Regs::I2C_DATA0 + 1), static_cast<std::uint8_t>(0x00));

            // Trigger Write (2 bytes payload: reg + val, 100 kHz) -> 0xA0
            if (!bridge.send_register_write(SN9C102::Regs::I2C_CTRL, static_cast<std::uint8_t>(0xA0))) {
                return false;
            }

            // 3. Polling de status (máximo 5ms)
            for (int i = 0; i < 50; ++i) {
                auto ctrl = bridge.send_register_read(SN9C102::Regs::I2C_CTRL);
                if (!ctrl) return false;

                // Si I2C_ERR (bit 3) está activo -> NACK (no hay sensor en esta dirección)
                if (*ctrl & 0x08) return false;

                // Si I2C_RDY (bit 2) está activo sin error -> ¡ACK RECIBIDO!
                if (*ctrl & 0x04) return true;

                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }

            return false;
        }
    };
}
