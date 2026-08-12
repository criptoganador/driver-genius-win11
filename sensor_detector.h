#pragma once

#include "device_connector.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <optional>
#include <vector>

namespace Genius {

    struct CmosSensorInfo {
        std::uint8_t slave_id_raw{0};
        std::uint8_t slave_address_7bit{0};
        std::string vendor_name{"Desconocido"};
        std::string possible_models{"Modelo no identificado"};
        std::string bus_status_description{""};
    };

    class SensorDetector {
    public:
        [[nodiscard]] static std::optional<CmosSensorInfo> detect(const DeviceBridge& bridge) noexcept {
            // 1. Leer el registro SLAVE_ID (0x09) del chip SN9C102
            auto slave_reg_res = bridge.send_register_read(SN9C102::Regs::SLAVE_ID);
            if (!slave_reg_res) {
                std::cerr << "[ERROR SensorDetector] No se pudo leer el registro SLAVE_ID (0x09).\n";
                return std::nullopt;
            }

            CmosSensorInfo info{};
            info.slave_id_raw = *slave_reg_res;
            info.slave_address_7bit = static_cast<std::uint8_t>(info.slave_id_raw >> 1);

            // 2. Leer estado de I2C_CTRL (0x08)
            auto i2c_ctrl_res = bridge.send_register_read(SN9C102::Regs::I2C_CTRL);
            if (i2c_ctrl_res) {
                std::uint8_t ctrl = *i2c_ctrl_res;
                bool is_i2c_dev = (ctrl & 0x80) != 0;
                bool is_high_speed = (ctrl & 0x01) != 0;
                bool has_err = (ctrl & 0x08) != 0;

                info.bus_status_description = std::string(is_i2c_dev ? "Bus I2C activo" : "Interfaz 3-wire") +
                                              ", " + (is_high_speed ? "400 kHz (Fast)" : "100 kHz (Standard)") +
                                              (has_err ? " [Advertencia: Flag I2C_ERR detectado]" : " [Bus OK]");
            }

            // 3. Mapear la dirección I2C Slave a los fabricantes conocidos de cámaras Genius / Sonix
            std::uint8_t raw = info.slave_id_raw;
            std::uint8_t a7 = info.slave_address_7bit;

            if (raw == 0x50 || a7 == 0x28 || raw == 0x21 || a7 == 0x10 || raw == 0x42) {
                info.vendor_name = "OmniVision Technologies / Silicon Optronics";
                info.possible_models = "OmniVision OV7660 / OV7648 / OV7640 o SOI968 (VGA CMOS)";
            } else if (raw == 0x30 || raw == 0x60 || a7 == 0x18 || a7 == 0x30) {
                info.vendor_name = "OmniVision / PixArt Imaging";
                info.possible_models = "OmniVision OV9655 / PixArt PAS106 / PAS202";
            } else if (raw == 0x5C || raw == 0xBA || raw == 0x48 || raw == 0x90 || a7 == 0x2E || a7 == 0x24) {
                info.vendor_name = "Micron / Aptina Imaging";
                info.possible_models = "Micron MT9V111 / MT9V011 / MT9M001 (VGA/SXGA)";
            } else if (raw == 0x10 || raw == 0x11 || raw == 0x20 || a7 == 0x08) {
                info.vendor_name = "Hynix / Hyundai";
                info.possible_models = "Hynix HV7131R / HV7131D";
            } else if (raw == 0x7A || raw == 0xF0) {
                info.vendor_name = "TASC / ElecVision";
                info.possible_models = "TASC TAS5110 / TAS5130";
            } else {
                info.vendor_name = "Sensor CMOS I2C Detectado";
                info.possible_models = "Sensor compatible I2C (SLAVE_ID 0x" + 
                    std::string(1, "0123456789ABCDEF"[raw >> 4]) +
                    std::string(1, "0123456789ABCDEF"[raw & 0x0F]) + ")";
            }

            return info;
        }

        static void print_report(const CmosSensorInfo& info) noexcept {
            std::cout << "\n======================================================\n";
            std::cout << "        INFORME DE DETECCIÓN DE SENSOR CMOS (I2C)     \n";
            std::cout << "======================================================\n";
            std::cout << "  -> Registro SLAVE_ID (0x09) : 0x" << std::hex << std::setw(2)
                      << std::setfill('0') << static_cast<int>(info.slave_id_raw) << std::dec << "\n";
            std::cout << "  -> Dirección I2C de 7 bits  : 0x" << std::hex << std::setw(2)
                      << std::setfill('0') << static_cast<int>(info.slave_address_7bit) << std::dec << "\n";
            std::cout << "  -> Fabricante del Sensor    : " << info.vendor_name << "\n";
            std::cout << "  -> Modelos Compatibles      : " << info.possible_models << "\n";
            if (!info.bus_status_description.empty()) {
                std::cout << "  -> Estado del Bus I2C       : " << info.bus_status_description << "\n";
            }
            std::cout << "======================================================\n\n";
        }
    };
}
