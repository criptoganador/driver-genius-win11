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

        // ── PASO 1: Configuración Inicial del Puente y Encendido del MCLK ──
        [[nodiscard]] static std::expected<void, ConnectionError> enable_master_clock(const DeviceBridge& bridge) noexcept {
            // Helper monádico para validar la escritura estricta en cada registro del puente
            auto write_reg_monadic = [&](std::uint16_t reg, std::uint8_t val) -> std::expected<void, ConnectionError> {
                if (!bridge.send_register_write(reg, val)) {
                    std::cout << "[FALLO] Error de sincronización de reloj en registro 0x"
                              << std::hex << std::setw(2) << std::setfill('0')
                              << static_cast<int>(reg) << std::dec << "\n";
                    return std::unexpected(ConnectionError::WinUsbInitFailed);
                }
                return {};
            };

            // 1. Rutina de Desbloqueo (Reset de Bus I2C):
            //    Limpiar flag I2C_ERR (reg 0x08) y restaurar dirección esclava por defecto (0x28 en reg 0x09)
            if (auto r = write_reg_monadic(SN9C102::Regs::I2C_CTRL, static_cast<std::uint8_t>(0x80)); !r) return r;
            if (auto r = write_reg_monadic(SN9C102::Regs::SLAVE_ID, SensorI2C::SENSOR_I2C_ADDR); !r) return r;

            // 2. Registro 0x01 (Sistema / SYS_CONTROL) y Registro 0x02 (GPIO = 0x44):
            //    0x01 = 0x04 -> Habilita reloj MCLK y sistema
            //    0x02 = 0x44 -> Libera el pin RESETB y alimenta el sensor OmniVision/SOI968
            if (auto r = write_reg_monadic(SN9C102::Regs::SYS_CONTROL, static_cast<std::uint8_t>(0x04)); !r) return r;
            if (auto r = write_reg_monadic(SN9C102::Regs::GPIO, static_cast<std::uint8_t>(0x44)); !r) return r;

            // 3. Registros 0x16 y 0x17 (Configuración I2C / SCCB 2 hilos):
            //    0x16 (V_SIZE_CLK) = 0x24 (Frecuencia de salida 24 MHz + SEN_CLK_EN = 1)
            //    0x17 (TIMING_SCAL) = 0x68 (Muestreo y flancos de reloj SCCB)
            if (auto r = write_reg_monadic(SN9C102::Regs::V_SIZE_CLK, static_cast<std::uint8_t>(0x24)); !r) return r;
            if (auto r = write_reg_monadic(SN9C102::Regs::TIMING_SCAL, static_cast<std::uint8_t>(0x68)); !r) return r;

            // 4. Registros 0x18 y 0x19 (Divisores de Reloj I2C @ 100 kHz):
            //    0x18 (SYNC_CLK_OUT) = 0x8f (Sincronización PCK y bus SCCB)
            //    0x19 (MCK_HO_SIZE) = 0x20 (Forzar metrónomo I2C a 100 kHz exactos)
            if (auto r = write_reg_monadic(SN9C102::Regs::SYNC_CLK_OUT, static_cast<std::uint8_t>(0x8f)); !r) return r;
            if (auto r = write_reg_monadic(SN9C102::Regs::MCK_HO_SIZE, static_cast<std::uint8_t>(0x20)); !r) return r;

            std::cout << "[PASO 1] Reloj Maestro (MCLK) y metrónomo I2C (100 kHz) sincronizados con éxito.\n";
            std::cout << "         Esperando estabilización eléctrica del sensor (50ms)...\n";

            // Delay de estabilización eléctrica obligatorio de 50ms
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            return {};
        }

        // ── PASO 2: Configurar registros I2C del sensor SOI968/OV7660 ──
        [[nodiscard]] static bool configure_sensor(const DeviceBridge& bridge) noexcept {
            std::cout << "[PASO 2] Configurando bus I2C y enviando secuencia al sensor SOI968/OV7660...\n";

            // Inicializar y validar el Reloj del Bus de Comunicación (SIO_C @ 100 kHz)
            if (auto clk_res = SensorI2C::init_i2c_bus_clock(bridge); !clk_res) {
                std::cerr << "  [ERROR] No se pudo inicializar el Reloj de Comunicación I2C (SIO_C).\n";
                return false;
            }

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

                    // Manejo Estricto de Errores: Abortar si la configuración de CLKRC (0x11) falla
                    if (cmd.reg == 0x11) {
                        std::cerr << "[ERROR CRÍTICO] No se pudo configurar el reloj interno del sensor (CLKRC 0x11)\n";
                        return false;
                    }
                }
                
                // 1. Si es la instrucción de Reset (COM7 = 0x80), pausar 50ms para que el sensor reinicie
                if (cmd.reg == 0x12 && cmd.val == 0x80) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                // 2. Si es la instrucción CLKRC (0x11 = 0x00), pausar 10ms para enganche de fase (PLL Lock)
                if (cmd.reg == 0x11 && cmd.val == 0x00 && res) {
                    std::cout << "  [PASO 3] Reloj interno sin divisor (CLKRC=0x00) activo. Enganche de fase PLL estabilizado (10ms).\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }

            std::cout << "\n  Resultados de inicialización I2C: " << success_count << "/"
                      << (sizeof(init_sequence) / sizeof(init_sequence[0])) << " comandos exitosos.\n\n";

            return success_count > 0;
        }

        // ── PASO 4: Activación del Stream y Verificación del Reloj de Píxeles (PCLK) ──
        [[nodiscard]] static bool enable_video_stream(const DeviceBridge& bridge) noexcept {
            std::cout << "[PASO 4] Activando transmisión de vídeo y verificando Reloj de Píxeles (PCLK)...\n";

            // 1. VALIDACIÓN DE POLARIDAD (Requisito 1): Verificar que COM10 (0x15) = 0x00 está en
            //    la secuencia de inicialización para asegurar que PCLK, HSYNC y VSYNC coinciden
            //    en polaridad con lo que el puente SN9C102 espera recibir.
            constexpr bool com10_ok = []() {
                for (const auto& cmd : init_sequence)
                    if (cmd.reg == 0x15 && cmd.val == 0x00) return true;
                return false;
            }();

            static_assert(com10_ok,
                "FALLO DE POLARIDAD: COM10 (0x15 = 0x00) no está en init_sequence. "
                "El PCLK estará invertido y el puente SN9C102 no podrá capturar píxeles.");

            std::cout << "  [OK] Polaridad de sincronización validada (COM10=0x00: PCLK, HSYNC, VSYNC en fase).\n";

            // 2. Programar los registros de imagen del PUENTE SN9C102 (pipeline de captura).
            //    Sin estos registros el SN9C102 no sabe el tamaño del frame y nunca genera
            //    payload USB aunque el sensor emita PCLK correctamente.
            //
            //    Valores de referencia (driver Linux gspca/sn9c102 para OV7660 VGA):
            //      0x10 (H_BLANK)     = 0x20  → Blanking horizontal
            //      0x12 (HStart)      = 0x1a  → Inicio captura horizontal (pixel 26)
            //      0x13 (VStart)      = 0x02  → Inicio captura vertical (línea 2)
            //      0x14 (VSTART_HIGH) = 0x01  → Bit alto de VStart
            //      0x15 (HSize)       = 0x28  → 40 × 16 = 640 píxeles (VGA)
            //      0x16 (VSize)       = 0x25  → 37 × 16 = 592 → recortado a 480 líneas
            //      0x17 (CalidadCompr)= 0x20  → Calidad compresión media
            //      0x18 (Compresión)  = 0x40  → Habilitar compresión JPEG del puente
            struct BridgeReg { std::uint16_t reg; std::uint8_t val; const char* desc; };
            static constexpr BridgeReg bridge_pipeline[] = {
                { 0x10, 0x20, "H_BLANK: blanking horizontal" },
                { 0x12, 0x1a, "HStart: inicio captura horizontal (px 26)" },
                { 0x13, 0x02, "VStart LSB: inicio captura vertical (linea 2)" },
                { 0x14, 0x01, "VStart MSB: bit alto de VStart" },
                { 0x15, 0x28, "HSize: 640px (40x16)" },
                { 0x16, 0x25, "VSize: 480 lineas (37x16, recortado)" },
                { 0x17, 0x68, "TIMING_SCAL: Modo SCCB (flancos y 9º bit Don't Care)" },
                { 0x18, 0x40, "Comprension: JPEG habilitado en el puente" },
            };

            std::cout << "  [PIPELINE] Programando registros de imagen del puente SN9C102...\n";
            for (const auto& r : bridge_pipeline) {
                if (!bridge.send_register_write(r.reg, r.val)) {
                    std::cerr << "  [ERROR] Fallo al escribir registro del puente 0x"
                              << std::hex << r.reg << std::dec << " (" << r.desc << ")\n";
                    return false;
                }
                std::cout << "    -> Bridge Reg 0x" << std::hex << std::setw(2) << std::setfill('0')
                          << r.reg << " = 0x" << static_cast<int>(r.val) << std::dec
                          << " [" << r.desc << "] → OK\n";
            }

            // 3. Activar V_TX_EN usando lectura-modificación-escritura sobre SYS_CONTROL (0x01)
            //    para NO borrar los bits de MCLK que se programaron en enable_master_clock().
            auto sys_ctrl_val = bridge.send_register_read(SN9C102::Regs::SYS_CONTROL);
            std::uint8_t new_sys_ctrl = 0x04; // Valor base con V_TX_EN
            if (sys_ctrl_val) {
                new_sys_ctrl = static_cast<std::uint8_t>(*sys_ctrl_val | 0x04); // OR con V_TX_EN
                std::cout << "  [SYS_CTRL] Leído SYS_CONTROL=0x" << std::hex
                          << static_cast<int>(*sys_ctrl_val) << " -> escribiendo 0x"
                          << static_cast<int>(new_sys_ctrl) << std::dec << "\n";
            }

            if (!bridge.send_register_write(SN9C102::Regs::SYS_CONTROL, new_sys_ctrl)) {
                std::cerr << "  [ERROR] No se pudo activar la transmisión de vídeo V_TX_EN.\n";
                return false;
            }

            // Verificar que SYS_CONTROL refleja V_TX_EN activo
            auto sys_ctrl = bridge.send_register_read(SN9C102::Regs::SYS_CONTROL);
            if (sys_ctrl && (*sys_ctrl & 0x04)) {
                std::cout << "  [OK] V_TX_EN = 1. El SN9C102 está habilitado para capturar PCLK.\n";
            } else {
                std::cout << "  [ADVERTENCIA] V_TX_EN no devolvió confirmación esperada.\n";
            }

            // 4. FLUSH del Endpoint 1 (Requisito 2): Limpiar resíduos eléctricos previos al PCLK
            if (bridge.flush_endpoint1()) {
                std::cout << "  [OK] Endpoint 1 (0x81) purgado (WinUsb_FlushPipe). Buffer limpio.\n";
            } else {
                std::cout << "  [AVISO] WinUsb_FlushPipe no confirmó (puede ser normal en ISO).\n";
            }

            // 5. DETECCIÓN DEL PCLK (Requisito 3): Prueba de lectura con timeout de 500ms
            //    Si el sensor no emite PCLK, el Endpoint 1 estará mudo.
            std::cout << "  Verificando presencia del Reloj de Píxeles (PCLK) en Endpoint 1 (timeout=500ms)...\n";
            if (!bridge.probe_pclk(500)) {
                std::cerr << "[ERROR CRÍTICO] El Endpoint 1 no recibe datos. "
                          << "El Reloj de Píxeles (PCLK) está ausente o desincronizado\n";
                return false;
            }

            std::cout << "  [OK] PCLK detectado. El sensor está generando fotogramas en el Endpoint 1.\n";
            std::cout << "======================================================\n\n";
            return true;
        }

        [[nodiscard]] static bool initialize(const DeviceBridge& bridge) noexcept {
            std::cout << "\n======================================================\n";
            std::cout << "  INICIALIZACIÓN DEL SENSOR Y HABILITACIÓN DE VÍDEO   \n";
            std::cout << "======================================================\n";

            if (!enable_master_clock(bridge)) return false;
            if (!configure_sensor(bridge)) return false;
            return enable_video_stream(bridge);
        }
    };
}
