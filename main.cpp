#include <iostream>
#include <iomanip>
#include <windows.h>
#include "device_connector.h"
#include "sensor_detector.h"
#include "sensor_i2c.h"
#include "sensor_init.h"
#include "bridge_status.h"

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

    // 6. PASO 3: Habilitar transmisión de vídeo USB (V_TX_EN = 1)
    if (!Genius::SensorInit::enable_video_stream(bridge)) {
        std::cout << "[ERROR] Fallo al habilitar la transmisión de vídeo USB.\n";
        return -1;
    }

    return 0;
}