#include "UsbScanner.h"

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <iostream>
#include <string>

#pragma comment(lib,"setupapi.lib")
#pragma comment(lib,"cfgmgr32.lib")

// GUID para Hubs USB
DEFINE_GUID(GUID_DEVINTERFACE_USB_HUB, 0xf18a0e88, 0xc30c, 0x11d0, 0x88, 0x15, 0x00, 0xa0, 0xc9, 0x06, 0xbe, 0xd8);

bool UsbScanner::ObtenerHubYPuerto(
    std::string deviceInstanceId,
    std::string& outHubPath,
    unsigned long& outPortNumber)
{
    std::cout << "\n===== ANALIZANDO HUB PADRE Y PUERTO =====\n";
    std::cout << "Dispositivo USB (Hijo): " << deviceInstanceId << "\n";

    DEVINST devInst;
    if (CM_Locate_DevNodeA(&devInst, (DEVINSTID_A)deviceInstanceId.c_str(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
    {
        std::cout << "Error: No se pudo localizar el DevNode del dispositivo.\n";
        return false;
    }

    // 1. Obtener Puerto (SPDRP_ADDRESS)
    HDEVINFO hInfo = SetupDiGetClassDevsA(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hInfo != INVALID_HANDLE_VALUE)
    {
        SP_DEVINFO_DATA data;
        data.cbSize = sizeof(SP_DEVINFO_DATA);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(hInfo, i, &data); i++)
        {
            char instanceID[1024] = {0};
            if (CM_Get_Device_IDA(data.DevInst, instanceID, sizeof(instanceID), 0) == CR_SUCCESS)
            {
                if (deviceInstanceId == instanceID)
                {
                    DWORD address = 0;
                    if (SetupDiGetDeviceRegistryPropertyA(hInfo, &data, SPDRP_ADDRESS, NULL, (PBYTE)&address, sizeof(address), NULL))
                    {
                        outPortNumber = address;
                        std::cout << "Port Number (SPDRP_ADDRESS): " << outPortNumber << "\n";
                    }
                    break;
                }
            }
        }
        SetupDiDestroyDeviceInfoList(hInfo);
    }

    // 2. Obtener Hub Padre
    DEVINST parentDevInst;
    if (CM_Get_Parent(&parentDevInst, devInst, 0) != CR_SUCCESS)
    {
        std::cout << "Error: No se pudo obtener el Hub Padre.\n";
        return false;
    }

    char parentInstanceID[1024] = { 0 };
    if (CM_Get_Device_IDA(parentDevInst, parentInstanceID, sizeof(parentInstanceID), 0) != CR_SUCCESS)
    {
        std::cout << "Error: No se pudo obtener el ID del Hub Padre.\n";
        return false;
    }

    std::cout << "Hub Parent Instance: " << parentInstanceID << "\n";

    // 3. Obtener el DevicePath del Hub (para CreateFile)
    HDEVINFO hHubInfo = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_USB_HUB, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hHubInfo != INVALID_HANDLE_VALUE)
    {
        SP_DEVICE_INTERFACE_DATA intData;
        intData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hHubInfo, NULL, &GUID_DEVINTERFACE_USB_HUB, i, &intData); i++)
        {
            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetailA(hHubInfo, &intData, NULL, 0, &requiredSize, NULL);

            if (requiredSize > 0)
            {
                SP_DEVICE_INTERFACE_DETAIL_DATA_A* detailData = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)malloc(requiredSize);
                if (detailData)
                {
                    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
                    SP_DEVINFO_DATA devData;
                    devData.cbSize = sizeof(SP_DEVINFO_DATA);

                    if (SetupDiGetDeviceInterfaceDetailA(hHubInfo, &intData, detailData, requiredSize, NULL, &devData))
                    {
                        char hubInstanceID[1024] = { 0 };
                        if (CM_Get_Device_IDA(devData.DevInst, hubInstanceID, sizeof(hubInstanceID), 0) == CR_SUCCESS)
                        {
                            if (std::string(hubInstanceID) == parentInstanceID)
                            {
                                outHubPath = detailData->DevicePath;
                                std::cout << "Hub Device Path (Para CreateFile):\n" << outHubPath << "\n";
                                free(detailData);
                                break;
                            }
                        }
                    }
                    free(detailData);
                }
            }
        }
        SetupDiDestroyDeviceInfoList(hHubInfo);
    }

    if (outHubPath.empty())
    {
        std::cout << "No se pudo encontrar el Device Path del Hub.\n";
        return false;
    }

    std::cout << "========================================\n";
    return true;
}
