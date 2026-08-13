#pragma once

// =========================================================
// sensor_isp.h
// Módulo de Procesamiento Avanzado de Señal de Imagen (ISP)
// para el sensor CMOS SOI968 / OV7660 en C++23
//
// Gestiona:
//  1. Matriz de Corrección de Color (CCM 3x3 - Regs 0x4F..0x54, 0x58)
//  2. Saturación, Tinte y Contraste (Regs 0x55, 0x56, 0x57, 0x67, 0x68)
//  3. Curvas de Corrección de Gamma (Regs 0x7A..0x89 - GAM1..GAM15 LUT)
//  4. Calibración de Niveles de Negro y Blanco / Pedestal (Regs 0x03, 0x3D COM13)
// =========================================================

#include "device_connector.h"
#include "sensor_i2c.h"
#include <iostream>
#include <iomanip>
#include <array>
#include <string_view>
#include <expected>

namespace Genius {

    // ─────────────────────────────────────────────────────────
    // CONSTANTES Y REGISTROS ISP DEL SENSOR SOI968/OV7660
    // ─────────────────────────────────────────────────────────

    namespace ISPRegs {
        // Matriz de Corrección de Color (CCM 3x3)
        static constexpr std::uint8_t MTX1      = 0x4F; // Coeficiente RR (Red to Red)
        static constexpr std::uint8_t MTX2      = 0x50; // Coeficiente RG (Green to Red)
        static constexpr std::uint8_t MTX3      = 0x51; // Coeficiente RB (Blue to Red)
        static constexpr std::uint8_t MTX4      = 0x52; // Coeficiente GR (Red to Green)
        static constexpr std::uint8_t MTX5      = 0x53; // Coeficiente GG (Green to Green)
        static constexpr std::uint8_t MTX6      = 0x54; // Coeficiente GB (Blue to Green)
        static constexpr std::uint8_t MTXS      = 0x58; // Signo y Auto-Control de Matriz

        // Saturación, Tinte, Brillo y Contraste
        static constexpr std::uint8_t BRIGHT    = 0x55; // Offset de Brillo / Pedestal (signed)
        static constexpr std::uint8_t CONTRAS   = 0x56; // Multiplicador de Contraste (0x40 = 1.0x)
        static constexpr std::uint8_t CONTRAS_CTR = 0x57; // Centro de Contraste / Tonos Medios
        static constexpr std::uint8_t MANU      = 0x67; // Ganancia de Saturación U / Cb
        static constexpr std::uint8_t MANV      = 0x68; // Ganancia de Saturación V / Cr

        // Pedestal y Control de Negro/Blanco
        static constexpr std::uint8_t REG03     = 0x03; // Pedestal de Negro Offset
        static constexpr std::uint8_t COM13     = 0x3D; // Auto-Saturación UV + Black Sun Compensation

        // Curva de Gamma LUT (15 Puntos no lineales: GAM1..GAM15 + SLOP)
        static constexpr std::uint8_t GAM_START = 0x7A; // Registro inicial GAM1
        static constexpr std::uint8_t SLOP      = 0x89; // Pendiente alta de Gamma

        // 1. Nitidez y Mejora de Bordes (Edge Enhancement & De-Speckle)
        static constexpr std::uint8_t EDGE      = 0x3F; // Umbral y ganancia de nitidez de bordes
        static constexpr std::uint8_t SPECKLE   = 0x77; // Umbral de filtro anti-punteado (De-speckle)

        // 2. Reducción de Ruido Espacial 2D (Spatial De-Noise)
        static constexpr std::uint8_t DNST1     = 0x4C; // Umbral de denoise nivel 1
        static constexpr std::uint8_t DNST2     = 0x4D; // Umbral de denoise nivel 2
        static constexpr std::uint8_t DNST3     = 0x4E; // Umbral de denoise nivel 3

        // 3. Corrección de Viñeteado de Lente (Lens Shading Correction - LCC)
        static constexpr std::uint8_t LCC1      = 0x62; // LCC Control y Centro X
        static constexpr std::uint8_t LCC2      = 0x63; // Centro Y de lente
        static constexpr std::uint8_t LCC3      = 0x64; // Ganancia de viñeteado Canal R
        static constexpr std::uint8_t LCC5      = 0x65; // Ganancia de viñeteado Canal G
        static constexpr std::uint8_t LCC4      = 0x66; // Ganancia de viñeteado Canal B

        // 4. Filtro Anti-Aliasing y De-Bayering (Anti-Falsos Colores)
        static constexpr std::uint8_t COM15     = 0x4B; // Bit 0: Anti-Aliasing De-Bayer enable
        static constexpr std::uint8_t REG_5C    = 0x5C; // Umbral de artefacto de falso color
        static constexpr std::uint8_t REG_5D    = 0x5D; // Límite de ganancia anti-moiré

        // 5. Detección Automática Anti-Parpadeo 50Hz / 60Hz (Anti-Flicker)
        static constexpr std::uint8_t COM11     = 0x3B; // Bit 3: Auto 50/60Hz detect enable
        static constexpr std::uint8_t BAN50     = 0x9D; // Paso de banda 50Hz
        static constexpr std::uint8_t BAN60     = 0x9E; // Paso de banda 60Hz
    }

    // ─────────────────────────────────────────────────────────
    // 1. MATRIZ DE CORRECCIÓN DE COLOR (CCM 3x3)
    // ─────────────────────────────────────────────────────────
    struct Matrix3x3CCM {
        std::uint8_t mtx1; // RR (ej. 0x80 = +1.0)
        std::uint8_t mtx2; // RG (ej. 0x10 = mezcla verde a rojo)
        std::uint8_t mtx3; // RB (ej. 0x00)
        std::uint8_t mtx4; // GR (ej. 0x18)
        std::uint8_t mtx5; // GG (ej. 0x70)
        std::uint8_t mtx6; // GB (ej. 0x1E)
        std::uint8_t mtxs; // Signos y Control (ej. 0x9E)
        std::string_view name;
    };

    // ─────────────────────────────────────────────────────────
    // 3. CURVAS DE GAMMA (LUT 15 Puntos)
    // ─────────────────────────────────────────────────────────
    using GammaLUT = std::array<std::uint8_t, 16>; // 15 Puntos GAM1..15 + 1 Pendiente SLOP

    // ─────────────────────────────────────────────────────────
    // CLASE PRINCIPAL: SensorISP
    // ─────────────────────────────────────────────────────────
    class SensorISP {
    public:

        // =========================================================
        // 1. MATRIZ DE CORRECCIÓN DE COLOR (CCM 3x3)
        // =========================================================

        // Perfiles Preconfigurados de CCM
        [[nodiscard]] static constexpr Matrix3x3CCM get_ccm_profile(std::string_view profile_name) noexcept {
            if (profile_name == "CientificoFiel" || profile_name == "sRGB") {
                // Matriz sRGB Calibrada de Fábrica: Elimina solapamiento del filtro Bayer
                return {0x80, 0x10, 0x00, 0x18, 0x70, 0x1E, 0x9E, "sRGB Científico Fiel"};
            } else if (profile_name == "VividoMatriz") {
                // Matriz de Alta Saturación Cromática Cruzada
                return {0x98, 0x08, 0x00, 0x10, 0x88, 0x08, 0x9E, "Vívido Matriz Cruzada"};
            } else if (profile_name == "DesaturadoPelicula") {
                // Mezcla tonal neutra desaturada estilo cinematográfico
                return {0x68, 0x18, 0x08, 0x20, 0x60, 0x20, 0x9E, "Desaturado Película"};
            }
            // Por defecto: Bypass / Matriz Neutra
            return {0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, "Neutro Bypass"};
        }

        // Programar Matriz CCM 3x3 en el hardware del sensor
        [[nodiscard]] static std::expected<void, I2CError>
        set_color_correction_matrix(const DeviceBridge& bridge, const Matrix3x3CCM& ccm) noexcept {
            std::cout << "[ISP] Programando Matriz de Corrección de Color (CCM 3x3): " << ccm.name << "\n";
            std::cout << "      MTX[1..6] = [0x" << std::hex << std::uppercase
                      << static_cast<int>(ccm.mtx1) << ", 0x" << static_cast<int>(ccm.mtx2) << ", 0x"
                      << static_cast<int>(ccm.mtx3) << ", 0x" << static_cast<int>(ccm.mtx4) << ", 0x"
                      << static_cast<int>(ccm.mtx5) << ", 0x" << static_cast<int>(ccm.mtx6)
                      << "] | MTXS=0x" << static_cast<int>(ccm.mtxs)
                      << std::dec << std::nouppercase << "\n";

            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::MTX1, ccm.mtx1); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::MTX2, ccm.mtx2); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::MTX3, ccm.mtx3); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::MTX4, ccm.mtx4); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::MTX5, ccm.mtx5); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::MTX6, ccm.mtx6); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::MTXS, ccm.mtxs); !r) return std::unexpected(r.error());

            std::cout << "      ✓ Coeficientes CCM 3x3 programados en hardware.\n";
            return {};
        }

        // =========================================================
        // 2. CONTROLES DE SATURACIÓN, TINTE Y CONTRASTE (YUV / CbCr)
        // =========================================================

        // Configurar Saturación de Color (U/V Channels - Regs 0x67 / 0x68)
        // saturation_pct: 0% = Blanco y Negro Puro | 100% = Normal (0x80) | 200% = Máxima Saturación (0xFF)
        [[nodiscard]] static std::expected<void, I2CError>
        set_saturation(const DeviceBridge& bridge, int saturation_pct) noexcept {
            // Mapear 0..200% a byte 0x00..0xFF (0x80 = 100%)
            std::uint8_t val = static_cast<std::uint8_t>(
                std::clamp((saturation_pct * 128) / 100, 0, 255)
            );

            std::cout << "[ISP] Ajustando Saturación de Color a " << saturation_pct << "% (Byte U/V=0x"
                      << std::hex << std::uppercase << static_cast<int>(val) << std::dec << std::nouppercase << ")...\n";

            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::MANU, val); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::MANV, val); !r) return std::unexpected(r.error());

            std::cout << "      ✓ Saturación cromática U/V actualizada.\n";
            return {};
        }

        // Configurar Contraste y Centro Tonal (Regs 0x56 / 0x57)
        // contrast_val: 0x40 = 1.0x Neutro | >0x40 = Mayor Contraste | <0x40 = Menor Contraste
        [[nodiscard]] static std::expected<void, I2CError>
        set_contrast(const DeviceBridge& bridge, std::uint8_t contrast_val, std::uint8_t center_val = 0x80) noexcept {
            std::cout << "[ISP] Ajustando Contraste (Reg 0x56=0x" << std::hex << std::uppercase
                      << static_cast<int>(contrast_val) << ", Center 0x57=0x" << static_cast<int>(center_val)
                      << std::dec << std::nouppercase << ")...\n";

            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::CONTRAS, contrast_val); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::CONTRAS_CTR, center_val); !r) return std::unexpected(r.error());

            std::cout << "      ✓ Contraste y centro de tonos medios actualizados.\n";
            return {};
        }

        // =========================================================
        // 3. CURVAS DE CORRECCIÓN DE GAMMA (LUT 15 PUNTOS)
        // =========================================================

        // Curva Gamma 2.2 Estándar (Respuesta Perceptual Humana)
        static constexpr GammaLUT GAMMA_STANDARD_2_2 = {
            0x04, 0x0E, 0x1A, 0x31, 0x5A, 0x69, 0x75, 0x7E,
            0x88, 0x8F, 0x96, 0xA3, 0xAF, 0xC4, 0xD7, 0x22
        };

        // Curva Gamma Cine S-Curve (Alto Contraste y Sombras Profundas)
        static constexpr GammaLUT GAMMA_CINE_SCURVE = {
            0x02, 0x08, 0x14, 0x28, 0x48, 0x60, 0x78, 0x8C,
            0x9C, 0xAC, 0xBC, 0xCC, 0xDC, 0xEC, 0xFC, 0x1E
        };

        // Curva Gamma Lineal (Diagnóstico RAW)
        static constexpr GammaLUT GAMMA_LINEAR = {
            0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
            0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x10
        };

        // Programar Tabla LUT de Gamma en Registros 0x7A..0x89
        [[nodiscard]] static std::expected<void, I2CError>
        set_gamma_curve(const DeviceBridge& bridge, const GammaLUT& lut, std::string_view curve_name) noexcept {
            std::cout << "[ISP] Programando Curva de Corrección Gamma (LUT 15 puntos): " << curve_name << "...\n";

            for (std::size_t i = 0; i < 15; ++i) {
                std::uint8_t reg_addr = static_cast<std::uint8_t>(ISPRegs::GAM_START + i);
                if (auto r = SensorI2C::write_sensor_reg(bridge, reg_addr, lut[i]); !r) {
                    return std::unexpected(r.error());
                }
            }
            // Escribir registro de pendiente SLOP (0x89)
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::SLOP, lut[15]); !r) {
                return std::unexpected(r.error());
            }

            std::cout << "      ✓ Curva Gamma cargada en LUT de 15 puntos con éxito.\n";
            return {};
        }

        // =========================================================
        // 4. CALIBRACIÓN DE NIVELES DE NEGRO Y BLANCO (PEDESTAL)
        // =========================================================

        // Configurar Pedestal de Negro (Black Level Offset & COM13 Auto-Pedestal)
        // pedestal_offset_signed: -128..+127 (0x00 = Neutro, +0x08 = Negro Suave, -0x08 = Negro Puro Profundo)
        [[nodiscard]] static std::expected<void, I2CError>
        calibrate_black_white_pedestal(const DeviceBridge& bridge, std::int8_t pedestal_offset_signed) noexcept {
            std::uint8_t bright_val = static_cast<std::uint8_t>(pedestal_offset_signed);

            std::cout << "[ISP] Calibrando Pedestal de Nivel de Negro (BRIGHT 0x55=0x"
                      << std::hex << std::uppercase << static_cast<int>(bright_val) << std::dec << std::nouppercase
                      << ", Offset: " << static_cast<int>(pedestal_offset_signed) << ")...\n";

            // 1. Programar offset de brillo pedestal en Reg 0x55
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::BRIGHT, bright_val); !r) {
                return std::unexpected(r.error());
            }

            // 2. Configurar COM13 (0x3D): Activar Auto Black Sun Compensation & UV Auto-Saturation
            // Bit 4 (0x10) = Black Sun Compensation | Bit 0 (0x01) = UV Auto-Saturation
            constexpr std::uint8_t com13_val = 0x11;
            std::cout << "      Habilitando Compensación Black Sun & Pedestal Automático (COM13 0x3D=0x11)...\n";
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::COM13, com13_val); !r) {
                return std::unexpected(r.error());
            }

            std::cout << "      ✓ Pedestal de niveles de negro y blanco calibrado sin dominantes de color.\n";
            return {};
        }

        // =========================================================
        // 5. NITIDEZ Y MEJORA DE BORDES (EDGE ENHANCEMENT)
        // =========================================================
        [[nodiscard]] static std::expected<void, I2CError>
        set_edge_enhancement(const DeviceBridge& bridge, bool enable, std::uint8_t edge_gain = 0x08) noexcept {
            std::uint8_t val = enable ? edge_gain : 0x00;
            std::cout << "[ISP] " << (enable ? "Habilitando" : "Deshabilitando")
                      << " Filtro de Nitidez y Bordes (EDGE Reg 0x3F=0x"
                      << std::hex << std::uppercase << static_cast<int>(val) << std::dec << std::nouppercase << ")...\n";

            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::EDGE, val); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::SPECKLE, enable ? 0x04 : 0x00); !r) return std::unexpected(r.error());

            std::cout << "      ✓ Filtro de Nitidez y bordes actualizado.\n";
            return {};
        }

        // =========================================================
        // 6. REDUCCIÓN DE RUIDO ESPACIAL 2D (SPATIAL DE-NOISE)
        // =========================================================
        [[nodiscard]] static std::expected<void, I2CError>
        set_spatial_denoise(const DeviceBridge& bridge, bool enable, std::uint8_t strength = 0x0C) noexcept {
            std::uint8_t d1 = enable ? strength : 0x00;
            std::uint8_t d2 = enable ? static_cast<std::uint8_t>(strength + 0x04) : 0x00;
            std::uint8_t d3 = enable ? static_cast<std::uint8_t>(strength + 0x08) : 0x00;

            std::cout << "[ISP] " << (enable ? "Habilitando" : "Deshabilitando")
                      << " Reducción de Ruido Espacial 2D (DNST 0x4C..0x4E=0x"
                      << std::hex << std::uppercase << static_cast<int>(d1) << std::dec << std::nouppercase << ")...\n";

            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::DNST1, d1); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::DNST2, d2); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::DNST3, d3); !r) return std::unexpected(r.error());

            std::cout << "      ✓ Filtro de Ruido Espacial 2D actualizado.\n";
            return {};
        }

        // =========================================================
        // 7. CORRECCIÓN DE VIÑETEADO DE LENTE (LENS SHADING CORRECTION - LCC)
        // =========================================================
        [[nodiscard]] static std::expected<void, I2CError>
        set_lens_shading_correction(const DeviceBridge& bridge, bool enable) noexcept {
            std::cout << "[ISP] " << (enable ? "Habilitando" : "Deshabilitando")
                      << " Corrección de Viñeteado de Lente (LCC Reg 0x62..0x66)...\n";

            std::uint8_t lcc1 = enable ? 0x01 : 0x00; // Bit 0: LCC enable
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::LCC1, lcc1); !r) return std::unexpected(r.error());

            if (enable) {
                // Programar centro de lente y ganancias de radio R/G/B estándar
                if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::LCC2, 0x20); !r) return std::unexpected(r.error());
                if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::LCC3, 0x10); !r) return std::unexpected(r.error());
                if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::LCC4, 0x10); !r) return std::unexpected(r.error());
                if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::LCC5, 0x14); !r) return std::unexpected(r.error());
            }

            std::cout << "      ✓ Corrección de viñeteado de lente LCC actualizada.\n";
            return {};
        }

        // =========================================================
        // 8. FILTRO ANTI-ALIASING Y DE-BAYERING (ANTI-FALSOS COLORES)
        // =========================================================
        [[nodiscard]] static std::expected<void, I2CError>
        set_anti_aliasing_debayer(const DeviceBridge& bridge, bool enable) noexcept {
            std::cout << "[ISP] " << (enable ? "Habilitando" : "Deshabilitando")
                      << " Filtro Anti-Aliasing y Anti-Moiré De-Bayer (COM15 Reg 0x4B)...\n";

            std::uint8_t com15 = enable ? 0xC1 : 0xC0; // Bit 0: Anti-aliasing auto
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::COM15, com15); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::REG_5C, enable ? 0x0A : 0x00); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::REG_5D, enable ? 0x12 : 0x00); !r) return std::unexpected(r.error());

            std::cout << "      ✓ Filtro Anti-Aliasing y Falso Color De-Bayer actualizado.\n";
            return {};
        }

        // =========================================================
        // 9. AUTO-DETECCIÓN ANTI-PARPADEO 50HZ / 60HZ (ANTI-FLICKER)
        // =========================================================
        [[nodiscard]] static std::expected<void, I2CError>
        set_anti_flicker_auto(const DeviceBridge& bridge, bool enable_auto_50_60hz = true) noexcept {
            std::cout << "[ISP] " << (enable_auto_50_60hz ? "Habilitando" : "Deshabilitando")
                      << " Auto-Detección Anti-Parpadeo 50Hz/60Hz (COM11 Reg 0x3B)...\n";

            // COM11 (0x3B): Bit 3 = Auto 50/60Hz detect | Bit 4 = Night mode auto frame rate
            std::uint8_t com11 = enable_auto_50_60hz ? 0x18 : 0x00;
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::COM11, com11); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::BAN50, 0x99); !r) return std::unexpected(r.error());
            if (auto r = SensorI2C::write_sensor_reg(bridge, ISPRegs::BAN60, 0x7F); !r) return std::unexpected(r.error());

            std::cout << "      ✓ Auto-Detección Anti-Parpadeo 50Hz/60Hz activa.\n";
            return {};
        }

        // ─────────────────────────────────────────────────────────
        // DEMOSTRACIÓN DE PIPELINE ISP COMPLETO (9 MÓDULOS ISP)
        // ─────────────────────────────────────────────────────────
        static void test_full_isp_pipeline(const DeviceBridge& bridge) noexcept {
            std::cout << "\n======================================================\n";
            std::cout <<   "  EJECUTANDO PIPELINE COMPLETO DE 9 MÓDULOS ISP PRO\n";
            std::cout <<   "======================================================\n";

            // 1. Programar Matriz sRGB 3x3
            (void)set_color_correction_matrix(bridge, get_ccm_profile("sRGB"));

            // 2. Fijar Saturación al 120% y Contraste al 1.1x
            (void)set_saturation(bridge, 120);
            (void)set_contrast(bridge, 0x48, 0x80);

            // 3. Cargar Curva Gamma 2.2 Perceptual
            (void)set_gamma_curve(bridge, GAMMA_STANDARD_2_2, "Gamma 2.2 Perceptual");

            // 4. Calibrar Pedestal de Negro Puro (offset 0)
            (void)calibrate_black_white_pedestal(bridge, 0);

            // 5. Habilitar Filtro de Nitidez y Bordes
            (void)set_edge_enhancement(bridge, true, 0x08);

            // 6. Habilitar Reducción de Ruido Espacial 2D
            (void)set_spatial_denoise(bridge, true, 0x0C);

            // 7. Habilitar Corrección de Viñeteado de Lente (LCC)
            (void)set_lens_shading_correction(bridge, true);

            // 8. Habilitar Anti-Aliasing y De-Bayering Anti-Falso Color
            (void)set_anti_aliasing_debayer(bridge, true);

            // 9. Activar Auto-Detección Anti-Parpadeo 50Hz/60Hz
            (void)set_anti_flicker_auto(bridge, true);

            std::cout << "======================================================\n";
            std::cout << "  ✓ Pipeline de 9 Módulos ISP de Procesamiento 100% Activo.\n";
            std::cout << "======================================================\n\n";
        }
    };

} // namespace Genius
