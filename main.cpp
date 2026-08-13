#include <iostream>
#include <iomanip>
#include <windows.h>
#include "device_connector.h"
#include "sensor_detector.h"
#include "sensor_i2c.h"
#include "sensor_init.h"
#include "bridge_status.h"
#include "light_monitor.h"
#include "camera_presets.h"
#include "sensor_isp.h"
#include "frame_renderer.h"

int main() {
    SetConsoleOutputCP(CP_UTF8);

    Genius::DeviceBridge bridge;
    
    auto connection = bridge.connect();
    if (!connection) {
        std::cout << "[ERROR] Fallo al conectar con la cámara Genius.\n";
        return -1;
    }

    // 1. Verificación del procesador backend Sonix (ASIC_ID)
    auto asic_id = bridge.send_register_read(Genius::SN9C102::Regs::ASIC_ID);
    if (asic_id) {
        std::cout << "[OK] ASIC_ID = 0x" << std::hex << std::setw(2)
                  << std::setfill('0') << static_cast<int>(*asic_id) << std::dec;
        std::cout << (*asic_id == 0x10 ? " → Sonix SN9C102 confirmado\n\n" : "\n");
    }
    // 1.5 Snapshot del estado inicial del puente (antes de cualquier inicializacion)
    //     Muestra en tiempo real las banderas de I2C_CTRL, GPIO, relojes y CTRL_STATUS
    Genius::BridgeStatus::run_full_status(bridge);

    // Para buscar que frecuencia MCLK genera ACK del sensor, descomentar:
    // Genius::BridgeStatus::scan_mclk_frequencies(bridge);


    // 2. PASO 1 (MCLK): Encendido del Reloj Maestro (MCLK) ANTES de cualquier llamada I2C
    auto mclk_res = Genius::SensorInit::enable_master_clock(bridge);
    if (!mclk_res) {
        std::cout << "[ERROR] Fallo al activar el Reloj Maestro (MCLK) del sensor.\n";
        return -1;
    }

    // 3. Detección del sensor CMOS (vía SLAVE_ID) con reloj activo y estabilizado
    auto sensor_info = Genius::SensorDetector::detect(bridge);
    if (sensor_info) {
        Genius::SensorDetector::print_report(*sensor_info);
    }

    // 4.5 Consulta NO BLOQUEANTE de la salud del bus I2C antes de la configuración
    std::cout << "[INFO] Verificando salud del bus I2C (no bloqueante)..." << std::endl;
    if (auto health = Genius::SensorI2C::read_bus_health(bridge)) {
        health->print();
        if (health->busy) {
            std::cout << "[ADVERTENCIA] El bus I2C está ocupado. Esperando liberación..." << std::endl;
        }
    }

    // 5. PASO 2: Configurar secuencia de registros I2C en el sensor
    if (!Genius::SensorInit::configure_sensor(bridge)) {
        std::cout << "[ERROR] Fallo en el proceso de configuración I2C del sensor.\n";
        return -1;
    }

    // 5.5 DIAGNÓSTICO QUAD EN TIEMPO REAL: AECH (0x10) + GAIN (0x00) + BLUE (0x01) + RED (0x02)
    //     - Configura COM8 (0x13) = 0x87 (Piloto automático: AEC + AGC + AWB)
    //     - Configura COM9 (0x14) = 0x38 (Límite máximo de ganancia a 8x)
    //     - Configura BLUE (0x01) y RED (0x02) = 0x80 (Ganancias de color AWB iniciales)
    //     - Imprime simultáneamente obturación (AECH), ganancia (GAIN) y canales de color (BLUE, RED)
    if (!Genius::LightMonitor::setup_auto_exposure(bridge)) {
        std::cout << "[ERROR] Fallo al configurar el piloto automático de luz y color.\n";
        return -1;
    }
    Genius::LightMonitor::monitor_live_full_pipeline(bridge, 10, 150);

    // 5.6 REGISTRO OBJETIVO DE BRILLO (AEW 0x24 / AEB 0x25 / VPT 0x26)
    //     - Configura AEW (0x24) = 0x78 (límite superior ~47% luminosidad)
    //     - Configura AEB (0x25) = 0x68 (límite inferior ~40% luminosidad)
    //     - Configura VPT (0x26) = 0xD4 (zona de ajuste AEC rápido activa)
    //     - Si el lente se tapa (luminosidad < AEB): AEC/AGC disparan
    //       compensación máxima de AECH y GAIN en tiempo real.
    if (!Genius::LightMonitor::setup_brightness_target(bridge)) {
        std::cout << "[ERROR] Fallo al configurar el Registro Objetivo de Brillo.\n";
        return -1;
    }
    // Bucle de Realimentación por Error: Target [AEB..AEW] vs AECH/GAIN activos
    Genius::LightMonitor::monitor_brightness_feedback(bridge, 10, 200);

    // 5.7 SUBSISTEMA DE PERFILES DE CONFIGURACIÓN (PRESETS)
    //     Demostración de conmutación en caliente de perfiles Cromáticos, Luz y Sensibilidad.
    Genius::CameraPresets::test_preset_switching(bridge);

    // 5.8 SUBSISTEMA ISP AVANZADO (CCM 3x3, GAMMA, SATURACIÓN, PEDESTAL)
    //     Programación de la matriz de color cruzada sRGB, tabla Gamma 2.2 y calibración de pedestal negro.
    Genius::SensorISP::test_full_isp_pipeline(bridge);

    // 5.9 MOTOR DE RENDERIZADO 2D (Primitivas Gráficas sobre Framebuffer)
    //     Demostración de las 6 primitivas: Píxel, Línea (Bresenham),
    //     Triángulo (Scanline Fill), Rectángulo, Círculo/Elipse (Punto Medio)
    //     y Polígono/Curva de Bézier (De Casteljau) con overlays de diagnóstico.
    Genius::FrameRenderer::test_full_renderer();

    // 6. PASO 3: Habilitar transmisión de vídeo USB (V_TX_EN = 1)
    if (!Genius::SensorInit::enable_video_stream(bridge)) {
        std::cout << "[ERROR] Fallo al habilitar la transmisión de vídeo USB.\n";
        return -1;
    }

    return 0;
}