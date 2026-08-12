#pragma once

// =========================================================
// sensor_init.h
// Módulo de Inicialización del Sensor y Habilitación de Vídeo
//
// Encargado de:
//  1. Configurar la alimentación y reloj del procesador SN9C102
//  2. Enviar el vector de registros I2C al sensor SOI968 / OV7660
//  3. Habilitar la transmisión de vídeo (V_TX_EN) por USB
// =========================================================

#include "device_connector.h"
#include "sensor_i2c.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <thread>
#include <expected>

namespace Genius {

    struct SensorRegisterCmd {
        std::uint8_t reg;
        std::uint8_t val;
        const char* description;
    };

    class SensorInit {
    public:
        // Tabla de inicialización oficial para el sensor SOI968 / OV7660
        // (Extraída del subsistema gspca/sn9c102 del Kernel de Linux)
        static constexpr SensorRegisterCmd init_sequence[] = {
            { 0x12, 0x80, "Reset de software del sensor (COM7 = 0x80)" },
            { 0x11, 0x00, "Prescaler de reloj sin divisor (CLKRC = 0x00)" },
            { 0x12, 0x05, "Modo VGA, salida RAW RGB Bayer (COM7 = 0x05)" },
            { 0x13, 0x87, "Activar Auto Exposure, Auto White Balance y AGC (COM8 = 0x87)" },
            { 0x01, 0x80, "Ganancia canal Azul inicial a 1.0x (BLUE = 0x80)" },
            { 0x02, 0x80, "Ganancia canal Rojo inicial a 1.0x (RED = 0x80)" },
            { 0x14, 0x38, "Límite máximo AGC a 8x (COM9 = 0x38)" },
            { 0x15, 0x00, "Polaridad HSYNC/VSYNC normal (COM10 = 0x00)" },
            { 0x1E, 0x00, "Orientación normal sin espejo (MVFP = 0x00)" }
        };

        [[nodiscard]] static bool initialize(const DeviceBridge& bridge) noexcept {
            std::cout << "\n======================================================\n";
            std::cout << "  INICIALIZACIÓN DEL SENSOR Y HABILITACIÓN DE VÍDEO   \n";
            std::cout << "======================================================\n";

            // ── FASE 1: Configurar Reloj y Alimentación en el SN9C102 ──
            std::cout << "[PASO 1] Configurando procesador SN9C102 (Alimentación y Reloj)...\n";
            
            // Encender sensor (S_PWR_DN = 0) y seleccionar reloj de 12MHz/24MHz
            if (!bridge.send_register_write(SN9C102::Regs::SYS_CONTROL, 0x00)) {
                std::cerr << "  [ERROR] No se pudo configurar SYS_CONTROL (0x01).\n";
                return false;
            }

            // Habilitar reloj de salida del sensor (SEN_CLK_EN = bit 5)
            if (!bridge.send_register_write(SN9C102::Regs::V_SIZE_CLK, 0x24)) { // SEN_RATE=24MHz + SEN_CLK_EN
                std::cerr << "  [ERROR] No se pudo habilitar el reloj del sensor (0x16).\n";
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            std::cout << "  [OK] Reloj del sensor activo (SEN_CLK_EN activado).\n\n";

            // ── FASE 2: Enviar Secuencia I2C de Inicialización al Sensor ──
            std::cout << "[PASO 2] Enviando secuencia de inicialización I2C al sensor SOI968/OV7660...\n";

            size_t success_count = 0;
            for (const auto& cmd : init_sequence) {
                auto res = SensorI2C::write_sensor_reg(bridge, cmd.reg, cmd.val);
                if (res) {
                    std::cout << "  -> Reg 0x" << std::hex << std::setw(2) << std::setfill('0')
                              << static_cast<int>(cmd.reg) << " = 0x" << std::setw(2)
                              << static_cast<int>(cmd.val) << std::dec << " [" << cmd.description << "] → OK\n";
                    success_count++;
                } else {
                    std::cerr << "  -> Reg 0x" << std::hex << static_cast<int>(cmd.reg)
                              << std::dec << " [" << cmd.description << "] → Falló escritura I2C\n";
                }
                
                // Si es la instrucción de Reset (COM7 = 0x80), pausar 50ms para que el sensor reinicie
                if (cmd.reg == 0x12 && cmd.val == 0x80) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }

            std::cout << "\n  Resultados de inicialización I2C: " << success_count << "/"
                      << (sizeof(init_sequence) / sizeof(init_sequence[0])) << " comandos exitosos.\n\n";

            // ── FASE 3: Habilitar Transmisión de Vídeo en el SN9C102 ──
            std::cout << "[PASO 3] Habilitando transmisión de vídeo USB (V_TX_EN)...\n";

            // Bit 2: V_TX_EN (Video Transfer Enable) = 1
            if (!bridge.send_register_write(SN9C102::Regs::SYS_CONTROL, 0x04)) {
                std::cerr << "  [ERROR] No se pudo activar la transmisión de vídeo V_TX_EN.\n";
                return false;
            }

            // Verificar que SYS_CONTROL refleja V_TX_EN activo
            auto sys_ctrl = bridge.send_register_read(SN9C102::Regs::SYS_CONTROL);
            if (sys_ctrl && (*sys_ctrl & 0x04)) {
                std::cout << "  [ÉXITO] Transmisión de vídeo activada (V_TX_EN = 1).\n";
                std::cout << "  El chip procesador está enviando fotogramas al Endpoint 1 (ISO).\n";
            } else {
                std::cout << "  [ADVERTENCIA] V_TX_EN no devolvió confirmación esperada.\n";
            }

            std::cout << "======================================================\n\n";
            return true;
        }
    };
}
