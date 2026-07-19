#include "UsbScanner.h"

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#include <iostream>
#include <string>


#pragma comment(lib,"setupapi.lib")



std::string UsbScanner::ObtenerInstanciaDispositivo(
    unsigned short vid,
    unsigned short pid)
{
    char target[64];
    sprintf_s(target, "VID_%04X&PID_%04X", vid, pid);

    HDEVINFO info = SetupDiGetClassDevsA(NULL, "USB", NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (info == INVALID_HANDLE_VALUE) return "";

    SP_DEVINFO_DATA data;
    data.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(info, i, &data); i++)
    {
        char hwid[1024] = {0};
        if (SetupDiGetDeviceRegistryPropertyA(info, &data, SPDRP_HARDWAREID, NULL, (PBYTE)hwid, sizeof(hwid), NULL))
        {
            std::string hardware = hwid;
            if (hardware.find(target) != std::string::npos && hardware.find("&MI_") == std::string::npos)
            {
                char instanceID[1024] = {0};
                if (CM_Get_Device_IDA(data.DevInst, instanceID, sizeof(instanceID), 0) == CR_SUCCESS)
                {
                    SetupDiDestroyDeviceInfoList(info);
                    return instanceID;
                }
            }
        }
    }
    SetupDiDestroyDeviceInfoList(info);
    return "";
}

bool UsbScanner::BuscarDispositivo(
    unsigned short vid,
    unsigned short pid)
{

    char target[64];


    sprintf_s(
        target,
        "VID_%04X&PID_%04X",
        vid,
        pid
    );



    std::cout
    << "Buscando: "
    << target
    << "\n";



    HDEVINFO info =
        SetupDiGetClassDevsA(
            NULL,
            "USB",
            NULL,
            DIGCF_PRESENT |
            DIGCF_ALLCLASSES
        );



    if(info == INVALID_HANDLE_VALUE)
    {

        std::cout
        << "Error obteniendo dispositivos USB\n";


        return false;

    }




    SP_DEVINFO_DATA data;

    data.cbSize =
        sizeof(SP_DEVINFO_DATA);





    for(
        DWORD i = 0;
        SetupDiEnumDeviceInfo(
            info,
            i,
            &data);
        i++
    )
    {


        char id[1024]={0};



        if(
        SetupDiGetDeviceRegistryPropertyA(
            info,
            &data,
            SPDRP_HARDWAREID,
            NULL,
            (PBYTE)id,
            sizeof(id),
            NULL))
        {


            std::string hardware =
                id;



            /*
                Buscamos solamente
                el dispositivo padre.

                Aceptado:

                USB\VID_0C45&PID_60B0


                Ignorado:

                USB\VID_0C45&PID_60B0&MI_00

                USB\VID_0C45&PID_60B0&MI_01
            */



            if(
                hardware.find(target)
                != std::string::npos
                &&
                hardware.find("&MI_")
                == std::string::npos
            )
            {


                std::cout
                << "\n=============================\n";


                std::cout
                << "DISPOSITIVO USB PADRE\n";


                std::cout
                << "Hardware ID:\n"
                << hardware
                << "\n";


                std::cout
                << "=============================\n";



                /*
                    Pasamos al módulo:

                    UsbInterface.cpp

                    donde buscamos:

                    MI_00
                    MI_01

                */


                AnalizarInterfaz(
                    info,
                    vid,
                    pid
                );




                SetupDiDestroyDeviceInfoList(
                    info
                );



                return true;


            }


        }


    }




    SetupDiDestroyDeviceInfoList(
        info
    );



    return false;

}