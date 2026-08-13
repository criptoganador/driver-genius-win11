#pragma once

// =========================================================
// camera_presets.h
// Subsistema Modular de Perfiles de Configuración (Presets)
// para el sensor CMOS SOI968 / OV7660 en C++23
//
// Permite la conmutación dinámica en caliente de:
//  1. Perfiles Cromáticos (AWB): Neutral, Cálido, Frío, Cine
//  2. Perfiles de Luz y Ruido (AEC + AGC): AltaNitidez, Acción, Balanceado
//  3. Perfiles de Sensibilidad (Ventana de Brillo): UltraSensible, EstableAntiFlicker, OscuridadExtrema
// =========================================================

#include "light_monitor.h"
#include <string_view>
#include <iostream>
#include <iomanip>
#include <expected>

namespace Genius {

    // ─────────────────────────────────────────────────────────
    // 1. ENUMERADORES TIPADOS DE PERFILES (C++23)
    // ─────────────────────────────────────────────────────────

    // Perfiles de Temperatura de Color y Tinte (AWB)
    enum class ColorProfile : std::uint8_t {
        Neutral,            // Colores fieles y balanceados 1.0x (0x80/0x80/0x80)
        Calido,             // RED +20% (0x98), BLUE -20% (0x68), GREEN 1.0x (0x80)
        Frio,               // RED -20% (0x68), BLUE +20% (0x98), GREEN 1.0x (0x80)
        Cine,               // RED +12% (0x90), BLUE -8% (0x78), GREEN +3% (0x84)
        Vivido,             // RED +20% (0x98), BLUE +20% (0x98), GREEN +12% (0x90) -> Colores intensos/pop
        Retrato,            // RED +8% (0x8A), BLUE -9% (0x74), GREEN +1.5% (0x82) -> Tono piel natural
        Sepia,              // RED +28% (0xA4), BLUE -37% (0x50), GREEN +6% (0x88) -> Tono café vintage
        VisionNocturna,     // RED -50% (0x40), BLUE -50% (0x40), GREEN +31% (0xA8) -> Verde esmeralda militar
        Pastel,             // RED -9% (0x74), BLUE -9% (0x74), GREEN -3% (0x78) -> Colores suaves desaturados
        FrioExtremo,        // RED -37% (0x50), BLUE +37% (0xB0), GREEN -2.5% (0x7C) -> Azul helado ártico
        Cyberpunk,          // RED +25% (0xA0), BLUE +25% (0xA0), GREEN -25% (0x60) -> Neón púpura/synthwave
        AntiFluorescente,   // RED +6% (0x88), BLUE +12% (0x90), GREEN -11% (0x72) -> Corrige luz verde de oficina
        AtardecerFuego,     // RED +37% (0xB0), BLUE -44% (0x48), GREEN -6% (0x78) -> Naranja/rojo hora dorada
        Aqua,               // RED -37% (0x50), BLUE +25% (0xA0), GREEN +20% (0x98) -> Turquesa oceánico
        Bosque,             // RED -6% (0x78), BLUE -31% (0x58), GREEN +25% (0xA0) -> Verde vegetación natural
        MonocromoContraste, // RED 1.0x (0x80), BLUE 1.0x (0x80), GREEN 1.0x (0x80) -> Monocromo estilo Noir
        TermicoFalso,       // RED +50% (0xC0), BLUE -62% (0x30), GREEN +12% (0x90) -> Mapa de calor térmico
        SubmarinoProfundo,  // RED -62% (0x30), BLUE +44% (0xB8), GREEN +6% (0x88) -> Absorción agua profunda
        BleachBypass,       // RED -12% (0x70), BLUE -12% (0x70), GREEN -18% (0x68) -> Desaturado plateado metálico
        InfrarrojoSeguridad // RED +50% (0xC0), BLUE -62% (0x30), GREEN -50% (0x40) -> Infrarrojo noche IR
    };

    // Perfiles de Luz, Exposición y Ruido (AEC + AGC)
    enum class LightingProfile : std::uint8_t {
        AltaNitidez, // Límite AGC bajo 3x (0x18), obturador dinámico largo sin ruido
        Accion,      // Límite AGC alto 15x (0x78), obturador rápido para congelar movimiento
        Balanceado   // Límite AGC moderado 8x (0x38), equilibrio perfecto webcam
    };

    // Perfiles de Sensibilidad y Tolerancia de Brillo (AEC Window)
    enum class SensitivityProfile : std::uint8_t {
        UltraSensible,      // AEW=0x70, AEB=0x60, VPT=0xD4 (Reacción rápida a sombras)
        EstableAntiFlicker, // AEW=0x80, AEB=0x50, VPT=0x00 (Inmune a parpadeo de luces)
        OscuridadExtrema    // AEW=0x90, AEB=0x40, VPT=0xD4 (Tolerancia a sombras profundas)
    };

    // ─────────────────────────────────────────────────────────
    // ESTRUCTURAS DE DATOS DE CONFIGURACIÓN POR PRESET
    // ─────────────────────────────────────────────────────────

    struct ColorPresetConfig {
        std::uint8_t red;
        std::uint8_t blue;
        std::uint8_t green;
        std::string_view name;
        std::string_view description;
    };

    struct LightingPresetConfig {
        std::uint8_t agc_limit_reg14;
        bool fixed_shutter;
        std::uint8_t shutter_val;
        std::string_view name;
        std::string_view description;
    };

    struct SensitivityPresetConfig {
        std::uint8_t aew;
        std::uint8_t aeb;
        std::uint8_t vpt;
        std::string_view name;
        std::string_view description;
    };

    // ─────────────────────────────────────────────────────────
    // CLASE PRINCIPAL: CameraPresets
    // ─────────────────────────────────────────────────────────

    class CameraPresets {
    public:
        // Obtener parámetros del Perfil Cromático
        [[nodiscard]] static constexpr ColorPresetConfig get_color_config(ColorProfile profile) noexcept {
            switch (profile) {
                case ColorProfile::Neutral:
                    return {0x80, 0x80, 0x80, "Neutral / Real", "Colores fieles y balanceados (RED=0x80, BLUE=0x80, GREEN=0x80)"};
                case ColorProfile::Calido:
                    return {0x98, 0x68, 0x80, "Cálido (Focos / Atardecer)", "Atenúa azul (-20%), potencia rojo (+20%) para tono dorado"};
                case ColorProfile::Frio:
                    return {0x68, 0x98, 0x80, "Frío (Luz LED / Sombra)", "Atenúa rojo (-20%), potencia azul (+20%) para ambiente frío"};
                case ColorProfile::Cine:
                    return {0x90, 0x78, 0x84, "Modo Cine / Cinematográfico", "RED +12%, BLUE -8%, GREEN +3% (Estética película)"};
                case ColorProfile::Vivido:
                    return {0x98, 0x98, 0x90, "Vívido / Alta Saturación", "RED +20%, BLUE +20%, GREEN +12% (Colores intensos/pop)"};
                case ColorProfile::Retrato:
                    return {0x8A, 0x74, 0x82, "Retrato / Tono Piel Natural", "RED +8%, BLUE -9%, GREEN +1.5% (Ideal para rostros en videollamada)"};
                case ColorProfile::Sepia:
                    return {0xA4, 0x50, 0x88, "Sepia / Vintage Retro", "RED +28%, BLUE -37%, GREEN +6% (Estética fotografía antigua)"};
                case ColorProfile::VisionNocturna:
                    return {0x40, 0x40, 0xA8, "Visión Nocturna / Esmeralda", "RED -50%, BLUE -50%, GREEN +31% (Verde militar táctico)"};
                case ColorProfile::Pastel:
                    return {0x74, 0x74, 0x78, "Pastel / Suave Desaturado", "RED -9%, BLUE -9%, GREEN -3% (Tono suave anti-reflejos)"};
                case ColorProfile::FrioExtremo:
                    return {0x50, 0xB0, 0x7C, "Frío Extremo / Ártico", "RED -37%, BLUE +37%, GREEN -2.5% (Tono helado futurista)"};
                case ColorProfile::Cyberpunk:
                    return {0xA0, 0xA0, 0x60, "Cyberpunk / Neón Magenta", "RED +25%, BLUE +25%, GREEN -25% (Estética Synthwave neón)"};
                case ColorProfile::AntiFluorescente:
                    return {0x88, 0x90, 0x72, "Anti-Fluorescente (Oficina)", "RED +6%, BLUE +12%, GREEN -11% (Elimina tono verde enfermizo)"};
                case ColorProfile::AtardecerFuego:
                    return {0xB0, 0x48, 0x78, "Atardecer Fuego / Hora Dorada", "RED +37%, BLUE -44%, GREEN -6% (Tono cálido fuego intenso)"};
                case ColorProfile::Aqua:
                    return {0x50, 0xA0, 0x98, "Aqua / Turquesa Oceánico", "RED -37%, BLUE +25%, GREEN +20% (Estética agua marina)"};
                case ColorProfile::Bosque:
                    return {0x78, 0x58, 0xA0, "Bosque / Naturaleza Verde", "RED -6%, BLUE -31%, GREEN +25% (Realza plantas y vegetación)"};
                case ColorProfile::MonocromoContraste:
                    return {0x80, 0x80, 0x80, "Monocromo / Estilo Noir", "Canales calibrados a neutro 1.0x para procesamiento b&n"};
                case ColorProfile::TermicoFalso:
                    return {0xC0, 0x30, 0x90, "Térmico Falso / Mapa de Calor", "RED +50%, BLUE -62%, GREEN +12% (Visualización infrarroja falsa)"};
                case ColorProfile::SubmarinoProfundo:
                    return {0x30, 0xB8, 0x88, "Submarino Profundo / Abisal", "RED -62%, BLUE +44%, GREEN +6% (Compensación de absorción roja del agua)"};
                case ColorProfile::BleachBypass:
                    return {0x70, 0x70, 0x68, "Bleach Bypass / Plata Metálico", "RED -12%, BLUE -12%, GREEN -18% (Desaturado plateado cinematográfico)"};
                case ColorProfile::InfrarrojoSeguridad:
                    return {0xC0, 0x30, 0x40, "Infrarrojo Seguridad / Noche IR", "RED +50%, BLUE -62%, GREEN -50% (Visión nocturna cámara IR)"};
            }
            return {0x80, 0x80, 0x80, "Neutral", ""};
        }

        // Obtener parámetros del Perfil de Iluminación y AGC
        [[nodiscard]] static constexpr LightingPresetConfig get_lighting_config(LightingProfile profile) noexcept {
            switch (profile) {
                case LightingProfile::AltaNitidez:
                    return {0x18, false, 0x00, "Modo Alta Nitidez (Noche / Baja Luz)", "Límite AGC 3x (Reg 0x14=0x18), evita nieve/ruido digital"};
                case LightingProfile::Accion:
                    return {0x78, true, 0x04, "Modo Acción (Deportes / Movimiento)", "Límite AGC 15x (Reg 0x14=0x78), obturador rápido congelante"};
                case LightingProfile::Balanceado:
                    return {0x38, false, 0x00, "Modo Balanceado (Estándar Webcam)", "Límite AGC 8x (Reg 0x14=0x38), balance óptimo exposición/ruido"};
            }
            return {0x38, false, 0x00, "Balanceado", ""};
        }

        // Obtener parámetros del Perfil de Sensibilidad
        [[nodiscard]] static constexpr SensitivityPresetConfig get_sensitivity_config(SensitivityProfile profile) noexcept {
            switch (profile) {
                case SensitivityProfile::UltraSensible:
                    return {0x70, 0x60, 0xD4, "Ultra-Sensible / Rápido", "AEW=0x70, AEB=0x60, VPT=0xD4 (Reacción inmediata a sombras)"};
                case SensitivityProfile::EstableAntiFlicker:
                    return {0x80, 0x50, 0x00, "Estable / Anti-Parpadeo (Flicker)", "AEW=0x80, AEB=0x50, VPT=0x00 (Inmune a luces fluorescentes)"};
                case SensitivityProfile::OscuridadExtrema:
                    return {0x90, 0x40, 0xD4, "Modo Oscuridad Extrema", "AEW=0x90, AEB=0x40, VPT=0xD4 (Tolerancia a sombras profundas)"};
            }
            return {0x78, 0x68, 0xD4, "Estándar", ""};
        }

        // ─────────────────────────────────────────────────────────
        // INTERFAZ DE CAMBIO DINÁMICO EN CALIENTE (C++23 std::expected)
        // ─────────────────────────────────────────────────────────

        // 1. Cambiar Perfil Cromático (RED 0x02, BLUE 0x01, GREEN 0x6A)
        [[nodiscard]] static std::expected<void, I2CError>
        set_color_profile(const DeviceBridge& bridge, ColorProfile profile) noexcept {
            auto cfg = get_color_config(profile);
            std::cout << "  [PRESET COLOR] Aplicando perfil: " << cfg.name << "\n";
            std::cout << "                 RED (0x02)=0x" << std::hex << std::uppercase << static_cast<int>(cfg.red)
                      << " | BLUE (0x01)=0x" << static_cast<int>(cfg.blue)
                      << " | GREEN (0x6A)=0x" << static_cast<int>(cfg.green)
                      << std::dec << std::nouppercase << "\n";

            if (auto r = LightMonitor::write_red_gain(bridge, cfg.red); !r) return std::unexpected(r.error());
            if (auto r = LightMonitor::write_blue_gain(bridge, cfg.blue); !r) return std::unexpected(r.error());
            if (auto r = LightMonitor::write_green_gain(bridge, cfg.green); !r) return std::unexpected(r.error());

            std::cout << "                 ✓ Perfil cromático actualizado en bus I2C.\n";
            return {};
        }

        // 2. Cambiar Perfil de Luz y Ruido (COM9 Reg 0x14 AGC Limit + AECH 0x10)
        [[nodiscard]] static std::expected<void, I2CError>
        set_lighting_profile(const DeviceBridge& bridge, LightingProfile profile) noexcept {
            auto cfg = get_lighting_config(profile);
            std::cout << "  [PRESET LUZ]   Aplicando perfil: " << cfg.name << "\n";
            std::cout << "                 Reg 0x14 (COM9 AGC Ceiling)=0x" << std::hex << std::uppercase
                      << static_cast<int>(cfg.agc_limit_reg14) << std::dec << std::nouppercase << "\n";

            // Escribir límite de ganancia AGC en Reg 0x14
            if (auto r = SensorI2C::write_sensor_reg(bridge, 0x14, cfg.agc_limit_reg14); !r) {
                return std::unexpected(r.error());
            }

            if (cfg.fixed_shutter) {
                std::cout << "                 Fijando obturador AECH (0x10)=0x" << std::hex << std::uppercase
                          << static_cast<int>(cfg.shutter_val) << std::dec << std::nouppercase << "...\n";
                if (auto r = LightMonitor::write_exposure(bridge, cfg.shutter_val); !r) {
                    return std::unexpected(r.error());
                }
            }

            std::cout << "                 ✓ Perfil de iluminación y AGC actualizado en bus I2C.\n";
            return {};
        }

        // 3. Cambiar Perfil de Sensibilidad (AEW 0x24, AEB 0x25, VPT 0x26)
        [[nodiscard]] static std::expected<void, I2CError>
        set_sensitivity_profile(const DeviceBridge& bridge, SensitivityProfile profile) noexcept {
            auto cfg = get_sensitivity_config(profile);
            std::cout << "  [PRESET SENSIBILIDAD] Aplicando perfil: " << cfg.name << "\n";
            std::cout << "                        AEW (0x24)=0x" << std::hex << std::uppercase << static_cast<int>(cfg.aew)
                      << " | AEB (0x25)=0x" << static_cast<int>(cfg.aeb)
                      << " | VPT (0x26)=0x" << static_cast<int>(cfg.vpt)
                      << std::dec << std::nouppercase << "\n";

            if (auto r = LightMonitor::write_aew(bridge, cfg.aew); !r) return std::unexpected(r.error());
            if (auto r = LightMonitor::write_aeb(bridge, cfg.aeb); !r) return std::unexpected(r.error());
            if (auto r = LightMonitor::write_vpt(bridge, cfg.vpt); !r) return std::unexpected(r.error());

            std::cout << "                        ✓ Perfil de sensibilidad actualizado en bus I2C.\n";
            return {};
        }

        // 4. Conmutación Global de Presets
        [[nodiscard]] static std::expected<void, I2CError>
        apply_full_preset(const DeviceBridge& bridge,
                         ColorProfile color,
                         LightingProfile light,
                         SensitivityProfile sens) noexcept
        {
            std::cout << "\n======================================================\n";
            std::cout <<   "  APLICANDO CONFIGURACIÓN INTEGRAL DE PRESETS (EN CALIENTE)\n";
            std::cout <<   "======================================================\n";

            if (auto r = set_color_profile(bridge, color); !r) return r;
            if (auto r = set_lighting_profile(bridge, light); !r) return r;
            if (auto r = set_sensitivity_profile(bridge, sens); !r) return r;

            std::cout << "======================================================\n";
            std::cout << "  ✓ Presets aplicados y sincronizados por el bus I2C.\n";
            std::cout << "======================================================\n\n";
            return {};
        }

        // Demostración de conmutación dinámica en tiempo real
        static void test_preset_switching(const DeviceBridge& bridge) noexcept {
            std::cout << "\n======================================================\n";
            std::cout <<   "  DEMOSTRACIÓN DE CONMUTACIÓN DE PRESETS EN TIEMPO REAL\n";
            std::cout <<   "======================================================\n";

            std::cout << "\n--- [ESCENARIO 1: Perfil Térmico Falso (Mapa de Calor)] ---\n";
            (void)apply_full_preset(bridge, ColorProfile::TermicoFalso, LightingProfile::AltaNitidez, SensitivityProfile::UltraSensible);
            LightMonitor::monitor_live_full_pipeline(bridge, 2, 80);

            std::cout << "\n--- [ESCENARIO 2: Perfil Bleach Bypass (Plata Metálico)] ---\n";
            (void)apply_full_preset(bridge, ColorProfile::BleachBypass, LightingProfile::Accion, SensitivityProfile::OscuridadExtrema);
            LightMonitor::monitor_live_full_pipeline(bridge, 2, 80);

            std::cout << "\n--- [ESCENARIO 3: Perfil Infrarrojo Seguridad IR] ---\n";
            (void)apply_full_preset(bridge, ColorProfile::InfrarrojoSeguridad, LightingProfile::AltaNitidez, SensitivityProfile::UltraSensible);
            LightMonitor::monitor_live_full_pipeline(bridge, 2, 80);

            std::cout << "\n--- [ESCENARIO 4: Perfil Submarino Profundo] ---\n";
            (void)apply_full_preset(bridge, ColorProfile::SubmarinoProfundo, LightingProfile::Balanceado, SensitivityProfile::EstableAntiFlicker);
            LightMonitor::monitor_live_full_pipeline(bridge, 2, 80);

            std::cout << "\n--- [ESCENARIO 5: Restaurar Perfil Neutral Real] ---\n";
            (void)apply_full_preset(bridge, ColorProfile::Neutral, LightingProfile::Balanceado, SensitivityProfile::UltraSensible);
            LightMonitor::monitor_live_full_pipeline(bridge, 2, 80);
        }
    };

} // namespace Genius
