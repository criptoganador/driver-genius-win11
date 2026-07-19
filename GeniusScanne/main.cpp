#include <iostream>
#include "UsbScanner.h"
#include "SonixScanner.h"


int main()
{
    std::cout << "=================================\n";
    std::cout << " GeniusScanner v0.2\n";
    std::cout << " Genius VideoCam Look 317\n";
    std::cout << "=================================\n\n";


    UsbScanner scanner;

    std::string instanceId = scanner.ObtenerInstanciaDispositivo(0x0C45, 0x60B0);
    if (!instanceId.empty())
    {
        std::string hubPath;
        unsigned long port = 0;

        if (scanner.ObtenerHubYPuerto(instanceId, hubPath, port))
        {
            // Escaneo USB (Hub + Descriptores)
            scanner.LeerDescriptoresUSB(hubPath, port);
        }
    }
    else
    {
        std::cout << "\nCamara NO encontrada\n";
        system("pause");
        return 1;
    }

    // --------------------------------------------------
    // Fase 2: Analizador de hardware Sonix
    // Lee registros internos del chip via WinUSB
    // --------------------------------------------------
    std::cout << "\n";
    SonixScanner sonix;

    if (sonix.Abrir())
    {
        // Identificacion rapida del chip
        sonix.IdentificarChip();

        // Escaneo del rango principal del bridge SN9C1xx (0x00 - 0x1F)
        sonix.EscanearRango(0x00, 0x1F);

        // Analisis I2C
        sonix.AnalizarI2C();

        // Nivel Avanzado: Volcar y comparar registros
        std::cout << "\n[main] Exportando mapa completo de registros (0x00 - 0x30)...\n";
        sonix.ExportarVolcado(0x00, 0x30, "dump_base.txt");
        
        // Descomenta esto para comparar dos volcados una vez que los tengas
        // SonixScanner::CompararVolcados("scan_off.txt", "scan_on.txt");

        sonix.Cerrar();
    }
    else
    {
        std::cout << "[main] No se pudo abrir WinUSB para el analisis Sonix.\n";
    }


    system("pause");
    return 0;
}