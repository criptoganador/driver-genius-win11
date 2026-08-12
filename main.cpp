#include <iostream>
#include <iomanip>
#include <windows.h>
#include "device_connector.h"
#include "sensor_detector.h"
#include "sensor_i2c.h"
#include "sensor_init.h"

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
        std::cout << (*asic_id == 0x10 ? " → Sonix SN9C102 confirmado\n" : "\n");
    }

    // 2. Detección del sensor CMOS (vía SLAVE_ID)
    auto sensor_info = Genius::SensorDetector::detect(bridge);
    if (sensor_info) {
        Genius::SensorDetector::print_report(*sensor_info);
    }

    // 3. Diagnóstico e identificación directa I2C
    Genius::SensorI2C::identify_sensor(bridge);

    // 4. Inicializar el sensor SOI968 / OV7660 y activar transmisión de vídeo
    if (!Genius::SensorInit::initialize(bridge)) {
        std::cout << "[ERROR] Fallo en el proceso de inicialización del sensor.\n";
        return -1;
    }

    return 0;
}