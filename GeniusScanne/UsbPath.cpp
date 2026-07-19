#include "UsbScanner.h"

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#include <iostream>
#include <string>


#pragma comment(lib,"setupapi.lib")
#pragma comment(lib,"cfgmgr32.lib")



std::string UsbScanner::ObtenerDevicePath(
    HDEVINFO info,
    SP_DEVINFO_DATA data)
{


    char instanceID[1024]={0};



    if(
    CM_Get_Device_IDA(
        data.DevInst,
        instanceID,
        sizeof(instanceID),
        0)
    != CR_SUCCESS)
    {

        return "";

    }



    std::cout
    << "Instance buscando Path:\n"
    << instanceID
    << "\n";



    HDEVINFO hInfo =
    SetupDiGetClassDevsA(
        NULL,
        "USB",
        NULL,
        DIGCF_PRESENT |
        DIGCF_ALLCLASSES
    );



    if(hInfo==INVALID_HANDLE_VALUE)
        return "";



    SP_DEVINFO_DATA dev;


    dev.cbSize =
        sizeof(dev);



    for(
        DWORD i=0;
        SetupDiEnumDeviceInfo(
            hInfo,
            i,
            &dev);
        i++)
    {


        char id[1024]={0};



        if(
        SetupDiGetDeviceRegistryPropertyA(
            hInfo,
            &dev,
            SPDRP_HARDWAREID,
            NULL,
            (PBYTE)id,
            sizeof(id),
            NULL))
        {


            std::string hardware=id;



            if(
            hardware.find("VID_0C45")
            !=std::string::npos
            &&
            hardware.find("PID_60B0")
            !=std::string::npos)
            {



                char path[1024]={0};



                if(
                SetupDiGetDeviceRegistryPropertyA(
                    hInfo,
                    &dev,
                    SPDRP_PHYSICAL_DEVICE_OBJECT_NAME,
                    NULL,
                    (PBYTE)path,
                    sizeof(path),
                    NULL))
                {


                    std::string result =
                        path;


                    SetupDiDestroyDeviceInfoList(
                        hInfo);


                    return result;

                }

            }


        }

    }



    SetupDiDestroyDeviceInfoList(
        hInfo);


    return "";

}
