#include <iostream>
#include "UsbScanner.h"


int main()
{
    std::cout << "=================================\n";
    std::cout << " GeniusScanner v0.1\n";
    std::cout << " Genius VideoCam Look 317\n";
    std::cout << "=================================\n\n";


    UsbScanner scanner;


    // scanner.EnumerarArbolUSB(0x0C45, 0x60B0);

    std::string instanceId = scanner.ObtenerInstanciaDispositivo(0x0C45, 0x60B0);
    if (!instanceId.empty()) 
    {
        std::string hubPath;
        unsigned long port = 0;
        
        if (scanner.ObtenerHubYPuerto(instanceId, hubPath, port)) 
        {
            scanner.LeerDescriptoresUSB(hubPath, port);
            
            // Nuevo: Intentar leer del Endpoint 0x82 usando WinUSB
            scanner.LeerVideoWinUSB();
        }
    }
    else
    {
        std::cout << "\nCamara NO encontrada\n";
    }


    system("pause");

    return 0;
}