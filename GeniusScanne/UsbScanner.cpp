#include "UsbScanner.h"

#include <iostream>


#pragma comment(lib,"setupapi.lib")
#pragma comment(lib,"winusb.lib")



bool UsbScanner::FindDevice(
    unsigned short vid,
    unsigned short pid)
{

    std::cout
    << "Buscando dispositivo USB...\n";


    if(!BuscarDispositivo(
        vid,
        pid))
    {

        std::cout
        << "Camara no encontrada\n";


        return false;

    }



    std::cout
    << "Dispositivo encontrado\n";


    return true;

}





void UsbScanner::ReadInformation()
{

    std::cout
    << "\n==== INFORMACION USB ====\n";


    std::cout
    << "VID : 0C45\n";


    std::cout
    << "PID : 60B0\n";


    std::cout
    << "\nAnalisis:\n";


    std::cout
    << "- Device Descriptor\n";


    std::cout
    << "- Configuration Descriptor\n";


    std::cout
    << "- Interface Descriptor\n";


    std::cout
    << "- Chip Sonix\n";

}