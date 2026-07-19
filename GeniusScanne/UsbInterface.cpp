#include "UsbScanner.h"

#include <windows.h>
#include <setupapi.h>
#include <usbiodef.h>

#include <iostream>
#include <string>


#pragma comment(lib,"setupapi.lib")



void UsbScanner::AnalizarInterfaz(
    HDEVINFO info,
    unsigned short vid,
    unsigned short pid)
{

    std::cout
    << "\n===== INTERFACES USB =====\n";



    char target[64];


    sprintf_s(
        target,
        "VID_%04X&PID_%04X",
        vid,
        pid
    );



    SP_DEVINFO_DATA data;

    data.cbSize =
        sizeof(SP_DEVINFO_DATA);




    for(
        DWORD i = 0;
        SetupDiEnumDeviceInfo(
            info,
            i,
            &data);
        i++)
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



            if(
                hardware.find(target)
                != std::string::npos
                &&
                hardware.find("&MI_")
                != std::string::npos
            )
            {



                std::cout
                << "\n=============================\n";


                std::cout
                << "INTERFACE ENCONTRADA\n";


                std::cout
                << "Hardware ID:\n"
                << hardware
                << "\n";



                std::string path =
                    ObtenerDevicePath(
                        info,
                        data
                    );



                if(!path.empty())
                {

                    std::cout
                    << "Device Path:\n"
                    << path
                    << "\n";



                    /*
                    LeerDescriptoresUSB(
                        path
                    );
                    */


                }
                else
                {

                    std::cout
                    << "No tiene Device Path\n";

                }



                std::cout
                << "=============================\n";

            }


        }


    }



    std::cout
    << "==========================\n";

}