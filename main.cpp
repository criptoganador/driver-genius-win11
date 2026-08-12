#include <iostream>
#include <iomanip>
#include <windows.h>
#include "device_connector.h"

int main() {
    // Configurar la consola de Windows para mostrar caracteres UTF-8
    SetConsoleOutputCP(CP_UTF8);

    Genius::DeviceBridge bridge;
    
    auto connection = bridge.connect();
    if (!connection) {
        std::cout << "[ERROR] Fallo al conectar con la cámara Genius.\n";
        return -1;
    }

    // 1. Lectura del Registro ASIC_ID (0x00) según Datasheet SN9C102
    std::cout << "\n[INFO] Leyendo registro 0x00 (ASIC_ID) según Datasheet SN9C102...\n";
    auto asic_id = bridge.send_register_read(Genius::SN9C102::Regs::ASIC_ID);
    if (asic_id) {
        std::cout << "  -> ¡Lectura exitosa del Chip! ASIC_ID = 0x"
                  << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(*asic_id) << std::dec;
        if (*asic_id == 0x10) {
            std::cout << " (Coincide con SN9C102 / SN9C1xx original)\n";
        } else {
            std::cout << "\n";
        }
    }

    // 2. Lectura del Registro SYS_CONTROL (0x01)
    std::cout << "[INFO] Leyendo registro 0x01 (SYS_CONTROL)...\n";
    auto sys_ctrl = bridge.send_register_read(Genius::SN9C102::Regs::SYS_CONTROL);
    if (sys_ctrl) {
        std::cout << "  -> Valor de SYS_CONTROL: 0x"
                  << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(*sys_ctrl) << std::dec << "\n";
    }

    // 3. Lectura de registros avanzados SN9C20x (0x1000 y 0x1061)
    std::cout << "[INFO] Leyendo registro 0x1000 (Control principal SN9C20x)...\n";
    auto reg_1000 = bridge.send_register_read(0x1000);
    if (reg_1000) {
        std::cout << "  -> Valor del registro 0x1000: 0x"
                  << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(*reg_1000) << std::dec << "\n";
    }

    return 0;
}