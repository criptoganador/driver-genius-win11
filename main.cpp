#include <iostream>
#include <iomanip>
#include <windows.h>
#include "device_connector.h"
#include "sensor_detector.h"
#include "sensor_i2c.h"

int main() {
    SetConsoleOutputCP(CP_UTF8);

    Genius::DeviceBridge bridge;
    
    auto connection = bridge.connect();
    if (!connection) {
        std::cout << "[ERROR] Fallo al conectar con la cámara Genius.\n";
        return -1;
    }

    // 1. Verificación del procesador bridge Sonix (ASIC_ID)
    auto asic_id = bridge.send_register_read(Genius::SN9C102::Regs::ASIC_ID);
    if (asic_id) {
        std::cout << "[OK] ASIC_ID = 0x" << std::hex << std::setw(2)
                  << std::setfill('0') << static_cast<int>(*asic_id) << std::dec;
        std::cout << (*asic_id == 0x10 ? " → Sonix SN9C102 confirmado\n" : "\n");
    }

    // 2. Detección rápida del fabricante del sensor (vía SLAVE_ID)
    auto sensor_info = Genius::SensorDetector::detect(bridge);
    if (sensor_info) {
        Genius::SensorDetector::print_report(*sensor_info);
    }

    // 3. Identificación exacta: leer PID/VER directamente del sensor vía I2C
    Genius::SensorI2C::identify_sensor(bridge);

    return 0;
}