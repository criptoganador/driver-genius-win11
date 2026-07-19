#pragma once

#include <windows.h>
#include <setupapi.h>
#include <string>


class UsbScanner
{

public:

    bool FindDevice(
        unsigned short vid,
        unsigned short pid
    );

    void EnumerarArbolUSB(
        unsigned short vid,
        unsigned short pid
    );


    void ReadInformation();

    std::string ObtenerInstanciaDispositivo(
        unsigned short vid,
        unsigned short pid
    );

    bool ObtenerHubYPuerto(
        std::string deviceInstanceId,
        std::string& outHubPath,
        unsigned long& outPortNumber
    );

    void LeerDescriptoresUSB(
        std::string hubPath,
        unsigned long portNumber
    );

    void LeerVideoWinUSB();

private:


    bool BuscarDispositivo(
        unsigned short vid,
        unsigned short pid
    );


    void AnalizarInterfaz(
        HDEVINFO info,
        unsigned short vid,
        unsigned short pid
    );


    void MostrarDetallesInterfaz(
        HDEVINFO info,
        SP_DEVINFO_DATA data
    );

    void ImprimirStringDescriptor(
        void* hHub, 
        unsigned long portNumber, 
        unsigned char index, 
        const std::string& label
    );

    void LeerConfigurationDescriptor(
        void* hHub,
        unsigned long portNumber
    );


    std::string ObtenerDevicePath(
        HDEVINFO info,
        SP_DEVINFO_DATA data
    );


};