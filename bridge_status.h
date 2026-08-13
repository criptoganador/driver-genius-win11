#pragma once

// =========================================================
// bridge_status.h
// Diagnostico en Tiempo Real del Puente SN9C102
//
// Provee:
//  1. Snapshot completo de registros clave del puente
//  2. Decodificacion bit a bit de I2C_CTRL y SYS_CONTROL
//  3. Dump de todos los registros 0x00-0x1F del puente
//  4. Scanner de frecuencias MCLK para encontrar la correcta
// =========================================================

#include "device_connector.h"
#include <iostream>
#include <iomanip>
#include <array>
#include <string_view>
#include <thread>
#include <chrono>

namespace Genius {

    class BridgeStatus {
    public:

        // Snapshot del estado del puente en un instante T
        struct Snapshot {
            std::uint8_t asic_id     = 0xFF; // 0x00: Debe ser 0x10
            std::uint8_t sys_control = 0xFF; // 0x01: V_TX_EN, MCLK, LED
            std::uint8_t gpio        = 0xFF; // 0x02: RESETB del sensor
            std::uint8_t i2c_ctrl    = 0xFF; // 0x08: Banderas del bus I2C
            std::uint8_t slave_id    = 0xFF; // 0x09: Direccion I2C activa
            std::uint8_t i2c_data0   = 0xFF; // 0x0A: Ultimo byte I2C
            std::uint8_t ctrl_status = 0xFF; // 0x0F: Estado interno
            std::uint8_t v_size_clk  = 0xFF; // 0x16: Frecuencia MCLK
            std::uint8_t timing_scal = 0xFF; // 0x17: Divisores SCCB
            std::uint8_t sync_clk    = 0xFF; // 0x18: Control PCK
            std::uint8_t mck_ho_size = 0xFF; // 0x19: Divisor MCLK
        };

        // 1. Captura estado del puente en tiempo real
        [[nodiscard]] static Snapshot read_snapshot(const DeviceBridge& bridge) noexcept {
            Snapshot s;
            auto rd = [&](std::uint16_t reg) -> std::uint8_t {
                auto v = bridge.send_register_read(reg);
                return v ? *v : 0xFF;
            };
            s.asic_id     = rd(SN9C102::Regs::ASIC_ID);
            s.sys_control = rd(SN9C102::Regs::SYS_CONTROL);
            s.gpio        = rd(SN9C102::Regs::GPIO);
            s.i2c_ctrl    = rd(SN9C102::Regs::I2C_CTRL);
            s.slave_id    = rd(SN9C102::Regs::SLAVE_ID);
            s.i2c_data0   = rd(SN9C102::Regs::I2C_DATA0);
            s.ctrl_status = rd(SN9C102::Regs::CTRL_STATUS);
            s.v_size_clk  = rd(SN9C102::Regs::V_SIZE_CLK);
            s.timing_scal = rd(SN9C102::Regs::TIMING_SCAL);
            s.sync_clk    = rd(SN9C102::Regs::SYNC_CLK_OUT);
            s.mck_ho_size = rd(SN9C102::Regs::MCK_HO_SIZE);
            return s;
        }

        // 2. Imprime el snapshot decodificando cada bandera
        static void print_snapshot(const Snapshot& s) noexcept {
            std::cout << "\n======================================================\n";
            std::cout <<   "   ESTADO EN TIEMPO REAL --- PUENTE SN9C102\n";
            std::cout <<   "======================================================\n";

            auto hex2 = [](std::uint8_t v) {
                std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<int>(v) << std::dec;
            };

            std::cout << "  [0x00] ASIC_ID     = "; hex2(s.asic_id);
            std::cout << (s.asic_id == 0x10 ? "  OK SN9C102" : "  !! ID inesperado") << "\n";

            std::cout << "  [0x01] SYS_CONTROL = "; hex2(s.sys_control);
            std::cout << "  | V_TX_EN=" << ((s.sys_control & 0x04) ? "1 [stream ON]" : "0 [stream OFF]");
            std::cout << " | PWR_DOWN=" << ((s.sys_control & 0x80) ? "1 [APAGADO!]" : "0 [ON]") << "\n";

            std::cout << "  [0x02] GPIO        = "; hex2(s.gpio);
            std::cout << "  | RESETB=" << ((s.gpio & 0x40) ? "1 [sensor activo]" : "0 [RESET activo!]");
            std::cout << " | VCC_EN=" << ((s.gpio & 0x04) ? "1 [alimentado]" : "0 [SIN ALIMENTACION!]") << "\n";

            // I2C_CTRL es el registro de estado critico del bus
            std::cout << "  [0x08] I2C_CTRL    = "; hex2(s.i2c_ctrl);
            std::cout << "  | I2C_DEV=" << ((s.i2c_ctrl & 0x80) ? "1 [2-wire ON]" : "0 [bus OFF]");
            std::cout << " | ERR="      << ((s.i2c_ctrl & 0x08) ? "1 [ERROR/NACK!]" : "0 [OK]");
            std::cout << " | RDY="      << ((s.i2c_ctrl & 0x04) ? "1 [listo]" : "0 [ocupado]");
            std::cout << " | SPEED="    << ((s.i2c_ctrl & 0x01) ? "400kHz" : "100kHz") << "\n";

            std::cout << "  [0x09] SLAVE_ID    = "; hex2(s.slave_id);
            std::cout << "  | Addr 7-bit activa (OV7660=0x21)\n";

            std::cout << "  [0x0A] I2C_DATA0   = "; hex2(s.i2c_data0);
            std::cout << "  | Ultimo byte del bus SCCB\n";

            std::cout << "  [0x0F] CTRL_STATUS = "; hex2(s.ctrl_status);
            std::cout << "  | Estado interno del puente (raw)\n";

            std::cout << "\n  -- Relojes -----------------------------------------------\n";
            std::cout << "  [0x16] V_SIZE_CLK  = "; hex2(s.v_size_clk);
            {
                std::uint8_t rate = s.v_size_clk & 0x07;
                static constexpr std::string_view freq_table[] = {
                    "24MHz","12MHz","6MHz","3MHz","1.5MHz","750kHz","Rsrv","Rsrv"
                };
                std::cout << "  | MCLK=" << freq_table[rate];
                std::cout << " | SEN_CLK_EN=" << ((s.v_size_clk & 0x04) ? "1 [reloj a sensor ON]" : "0 [reloj OFF!]");
            }
            std::cout << "\n";
            std::cout << "  [0x17] TIMING_SCAL = "; hex2(s.timing_scal); std::cout << "\n";
            std::cout << "  [0x18] SYNC_CLK    = "; hex2(s.sync_clk);    std::cout << "\n";
            std::cout << "  [0x19] MCK_HO_SIZE = "; hex2(s.mck_ho_size); std::cout << "\n";
            std::cout << "======================================================\n\n";
        }

        // 3. Dump completo de todos los registros 0x00-0x1F
        static void dump_bridge_registers(const DeviceBridge& bridge) noexcept {
            std::cout << "\n======================================================\n";
            std::cout <<   "   DUMP SN9C102: Registros 0x00 a 0x1F\n";
            std::cout <<   "======================================================\n";
            std::cout << "  Reg  | Valor | Nombre\n";
            std::cout << "  -----+-------+-------------------------------\n";

            struct RegName { std::uint8_t reg; std::string_view name; };
            static constexpr RegName names[] = {
                {0x00,"ASIC_ID    "},{0x01,"SYS_CTRL  "},{0x02,"GPIO      "},
                {0x08,"I2C_CTRL  "},{0x09,"SLAVE_ID  "},{0x0A,"I2C_DATA0 "},
                {0x0B,"I2C_DATA1 "},{0x0C,"I2C_DATA2 "},{0x0D,"I2C_DATA3 "},
                {0x0E,"I2C_DATA4 "},{0x0F,"CTRL_STAT "},{0x10,"H_BLANK   "},
                {0x11,"GAIN_G    "},{0x12,"H_START   "},{0x13,"V_START   "},
                {0x14,"OFFSET    "},{0x15,"H_SIZE    "},{0x16,"V_SIZE_CLK"},
                {0x17,"TIMING_SCL"},{0x18,"SYNC_CLK  "},{0x19,"MCK_HOSIZE"},
                {0x1A,"VO_SIZE   "},{0x1B,"AE_STRX   "},{0x1C,"AE_STRY   "},
                {0x1D,"AE_ENDX   "},{0x1E,"AE_ENDY   "},
            };

            for (const auto& n : names) {
                auto val = bridge.send_register_read(static_cast<std::uint16_t>(n.reg));
                std::cout << "  0x" << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<int>(n.reg) << " |  ";
                if (val) std::cout << "0x" << std::setw(2) << static_cast<int>(*val);
                else     std::cout << " ERR";
                std::cout << std::dec << " | " << n.name << "\n";
            }
            std::cout << "======================================================\n\n";
        }

        // 4. Scanner de frecuencias MCLK para encontrar la que genera ACK
        static void scan_mclk_frequencies(const DeviceBridge& bridge) noexcept {
            std::cout << "\n======================================================\n";
            std::cout <<   "   SCANNER MCLK -> Buscando frecuencia con ACK I2C\n";
            std::cout <<   "======================================================\n";

            struct ClkConfig {
                std::uint8_t v_size_clk;
                std::uint8_t timing_scal;
                std::string_view label;
            };

            static constexpr ClkConfig configs[] = {
                { 0x24, 0x68, "24 MHz (gspca ref)" },
                { 0x21, 0x68, "12 MHz + timing gspca" },
                { 0x22, 0x68, "6 MHz + timing gspca" },
                { 0x23, 0x68, "3 MHz + timing gspca" },
                { 0x24, 0x00, "24 MHz + timing 0x00" },
                { 0x24, 0x40, "24 MHz + timing 0x40" },
                { 0x24, 0x20, "24 MHz + timing 0x20" },
                { 0x21, 0x00, "12 MHz + timing 0x00" },
                { 0x21, 0x40, "12 MHz + timing 0x40" },
                { 0x20, 0x68, "24 MHz SEN_CLK=0 (ctrl)" },
            };

            bool found_any = false;
            for (const auto& cfg : configs) {
                (void)bridge.send_register_write(SN9C102::Regs::V_SIZE_CLK,  cfg.v_size_clk);
                (void)bridge.send_register_write(SN9C102::Regs::TIMING_SCAL, cfg.timing_scal);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));

                // Prueba I2C: escribir registro 0x00 a addr 0x21
                (void)bridge.send_register_write(SN9C102::Regs::I2C_CTRL,  0x00);
                (void)bridge.send_register_write(SN9C102::Regs::SLAVE_ID,  0x21);
                (void)bridge.send_register_write(SN9C102::Regs::I2C_DATA0, 0x00);
                (void)bridge.send_register_write(SN9C102::Regs::I2C_DATA0 + 1, 0xFF);
                // Disparar: I2C_DEV | 2-byte | 100kHz
                (void)bridge.send_register_write(SN9C102::Regs::I2C_CTRL,
                    static_cast<std::uint8_t>(0x80 | 0x20 | 0x00));

                bool got_ack = false;
                for (int t = 0; t < 20; ++t) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    auto ctrl = bridge.send_register_read(SN9C102::Regs::I2C_CTRL);
                    if (!ctrl) break;
                    if (*ctrl & 0x08) { got_ack = false; break; } // ERR=NACK
                    if (*ctrl & 0x04) { got_ack = true;  break; } // RDY=ACK
                }
                (void)bridge.send_register_write(SN9C102::Regs::I2C_CTRL, 0x00);

                std::cout << "  [" << (got_ack ? "ACK OK" : " NACK ") << "] "
                          << cfg.label
                          << "  V_SIZE_CLK=0x" << std::hex << static_cast<int>(cfg.v_size_clk)
                          << " TIMING=0x" << static_cast<int>(cfg.timing_scal) << std::dec << "\n";
                if (got_ack) found_any = true;
            }

            std::cout << "\n";
            if (found_any)
                std::cout << "  >> Usar los valores marcados ACK OK en enable_master_clock().\n";
            else
                std::cout << "  >> Ninguna frecuencia respondio. Verificar GPIO y alimentacion.\n";
            std::cout << "======================================================\n\n";
        }

        // 5. Wrapper: Snapshot + Dump en una sola llamada
        static void run_full_status(const DeviceBridge& bridge) noexcept {
            auto snap = read_snapshot(bridge);
            print_snapshot(snap);
            dump_bridge_registers(bridge);
        }

    }; // class BridgeStatus

} // namespace Genius
