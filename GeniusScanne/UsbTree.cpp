#include "UsbScanner.h"
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <iostream>
#include <string>

#pragma comment(lib,"setupapi.lib")
#pragma comment(lib,"cfgmgr32.lib")

void UsbScanner::EnumerarArbolUSB(
    unsigned short vid,
    unsigned short pid)
{
    std::cout << "\n===== ARBOL USB COMPLETO (" << std::hex << vid << ":" << pid << ") =====\n";

    char target[64];
    sprintf_s(target, "VID_%04X&PID_%04X", vid, pid);

    // 1. Enumeramos TODOS los dispositivos del sistema
    HDEVINFO info = SetupDiGetClassDevsA(
        NULL, 
        NULL, 
        NULL, 
        DIGCF_PRESENT | DIGCF_ALLCLASSES
    );

    if (info == INVALID_HANDLE_VALUE) return;

    SP_DEVINFO_DATA data;
    data.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(info, i, &data); i++)
    {
        char hwid[1024] = { 0 };
        if (SetupDiGetDeviceRegistryPropertyA(
            info, &data, SPDRP_HARDWAREID, NULL, (PBYTE)hwid, sizeof(hwid), NULL))
        {
            std::string hardware = hwid;
            
            // Verificamos si contiene nuestro VID/PID
            if (hardware.find(target) != std::string::npos)
            {
                std::cout << "\n----------------------------------------\n";
                
                // Device Instance
                char instanceID[1024] = { 0 };
                if (CM_Get_Device_IDA(data.DevInst, instanceID, sizeof(instanceID), 0) == CR_SUCCESS) {
                    std::cout << "Device Instance: " << instanceID << "\n";
                }

                // Hardware ID
                std::cout << "Hardware ID:     " << hardware << "\n";

                // Class GUID
                char classGuid[256] = { 0 };
                if (SetupDiGetDeviceRegistryPropertyA(info, &data, SPDRP_CLASSGUID, NULL, (PBYTE)classGuid, sizeof(classGuid), NULL)) {
                    std::cout << "Class GUID:      " << classGuid << "\n";
                }

                // Nombre de la clase (para mayor claridad)
                char className[256] = { 0 };
                if (SetupDiGetDeviceRegistryPropertyA(info, &data, SPDRP_CLASS, NULL, (PBYTE)className, sizeof(className), NULL)) {
                    std::cout << "Class Name:      " << className << "\n";
                }

                // Service
                char service[256] = { 0 };
                if (SetupDiGetDeviceRegistryPropertyA(info, &data, SPDRP_SERVICE, NULL, (PBYTE)service, sizeof(service), NULL)) {
                    std::cout << "Service:         " << service << "\n";
                } else {
                    std::cout << "Service:         (None)\n";
                }

                // Device Path (PDO) - Frecuentemente no es el usado para CreateFile
                char pdoName[1024] = { 0 };
                if (SetupDiGetDeviceRegistryPropertyA(info, &data, SPDRP_PHYSICAL_DEVICE_OBJECT_NAME, NULL, (PBYTE)pdoName, sizeof(pdoName), NULL)) {
                    std::cout << "Device Path(PDO):" << pdoName << "\n";
                }
            }
        }
    }
    SetupDiDestroyDeviceInfoList(info);

    // 2. Buscamos específicamente las Interfaces de Dispositivo USB para CreateFile
    // GUID_DEVINTERFACE_USB_DEVICE permite obtener la ruta '\\?\usb#...' correcta
    std::cout << "\n--- INTERFACES REGISTRADAS (Para CreateFile USB RAW) ---\n";
    
    GUID guidUsbDevice = { 0xA5DCBF10, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED } };
    
    HDEVINFO hIntInfo = SetupDiGetClassDevsA(&guidUsbDevice, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hIntInfo != INVALID_HANDLE_VALUE)
    {
        SP_DEVICE_INTERFACE_DATA intData;
        intData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hIntInfo, NULL, &guidUsbDevice, i, &intData); i++)
        {
            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetailA(hIntInfo, &intData, NULL, 0, &requiredSize, NULL);

            if (requiredSize > 0)
            {
                SP_DEVICE_INTERFACE_DETAIL_DATA_A* detailData = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)malloc(requiredSize);
                if (detailData) 
                {
                    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
                    
                    SP_DEVINFO_DATA devData;
                    devData.cbSize = sizeof(SP_DEVINFO_DATA);

                    if (SetupDiGetDeviceInterfaceDetailA(hIntInfo, &intData, detailData, requiredSize, NULL, &devData))
                    {
                        std::string path = detailData->DevicePath;
                        
                        // Convertir path a mayúsculas para comparar con target
                        std::string upperPath = path;
                        for(auto &c: upperPath) c = toupper(c);
                        
                        if (upperPath.find(target) != std::string::npos)
                        {
                            std::cout << "\nInterface Path (Usa esto en CreateFile!):\n" << path << "\n";
                            
                            char hwid[1024] = { 0 };
                            if (SetupDiGetDeviceRegistryPropertyA(hIntInfo, &devData, SPDRP_HARDWAREID, NULL, (PBYTE)hwid, sizeof(hwid), NULL)) {
                                std::cout << "Pertenece a: " << hwid << "\n";
                            }
                        }
                    }
                    free(detailData);
                }
            }
        }
        SetupDiDestroyDeviceInfoList(hIntInfo);
    }

    std::cout << "========================================\n";
}
