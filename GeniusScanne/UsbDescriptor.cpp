#include <windows.h>
#include <usbioctl.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include "UsbScanner.h"

// Definiciones si faltan en usbspec.h / usbioctl.h
#ifndef USB_STRING_DESCRIPTOR_TYPE
#define USB_STRING_DESCRIPTOR_TYPE 0x03
#endif

#ifndef USB_CONFIGURATION_DESCRIPTOR_TYPE
#define USB_CONFIGURATION_DESCRIPTOR_TYPE 0x02
#endif

#ifndef USB_INTERFACE_DESCRIPTOR_TYPE
#define USB_INTERFACE_DESCRIPTOR_TYPE 0x04
#endif

void UsbScanner::LeerDescriptoresUSB(
    std::string hubPath,
    unsigned long portNumber)
{
    std::cout << "\n===== LECTURA DE DESCRIPTORES USB =====\n";
    std::cout << "Hub Path: " << hubPath << "\n";
    std::cout << "Port:     " << portNumber << "\n";

    HANDLE hHub = CreateFileA(
        hubPath.c_str(),
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hHub == INVALID_HANDLE_VALUE)
    {
        std::cout << "Error: No se pudo abrir el HUB. GetLastError: " << GetLastError() << "\n";
        return;
    }

    // 1. Leer Device Descriptor (usando USB_DESCRIPTOR_REQUEST)
    const DWORD reqSize = sizeof(USB_DESCRIPTOR_REQUEST) + sizeof(USB_DEVICE_DESCRIPTOR);
    BYTE buffer[1024] = { 0 };
    USB_DESCRIPTOR_REQUEST* req = (USB_DESCRIPTOR_REQUEST*)buffer;
    
    req->ConnectionIndex = portNumber;
    
    req->SetupPacket.bmRequest = 0x80; // USB_ENDPOINT_DIRECTION_IN
    req->SetupPacket.bRequest = 0x06;  // USB_REQUEST_GET_DESCRIPTOR
    req->SetupPacket.wValue = (USB_DEVICE_DESCRIPTOR_TYPE << 8); // Type + Index 0
    req->SetupPacket.wIndex = 0;       // Language ID
    req->SetupPacket.wLength = sizeof(USB_DEVICE_DESCRIPTOR);
    
    DWORD bytesReturned = 0;
    BOOL bResult = DeviceIoControl(
        hHub,
        IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
        req,
        reqSize,
        req,
        reqSize,
        &bytesReturned,
        NULL
    );

    if (bResult && bytesReturned >= reqSize)
    {
        USB_DEVICE_DESCRIPTOR* devDesc = (USB_DEVICE_DESCRIPTOR*)(req->Data);
        
        std::cout << "\n================================\n";
        std::cout << "USB DEVICE DESCRIPTOR\n\n";
        
        std::cout << std::hex << std::setfill('0') << std::uppercase;
        std::cout << "VID:\n0x" << std::setw(4) << devDesc->idVendor << "\n\n";
        std::cout << "PID:\n0x" << std::setw(4) << devDesc->idProduct << "\n\n";
        
        std::cout << "USB Version:\n0x" << std::setw(4) << devDesc->bcdUSB << "\n\n";
        
        std::cout << std::dec << std::setfill(' ');
        
        ImprimirStringDescriptor(hHub, portNumber, devDesc->iManufacturer, "Manufacturer");
        ImprimirStringDescriptor(hHub, portNumber, devDesc->iProduct, "Product");
        ImprimirStringDescriptor(hHub, portNumber, devDesc->iSerialNumber, "Serial Number");
        
        std::cout << "================================\n\n";
    }
    
    // 2. Leer Configuration Descriptor e Interfaces UVC
    LeerConfigurationDescriptor(hHub, portNumber);

    CloseHandle(hHub);
}

void UsbScanner::ImprimirStringDescriptor(void* hHubVoid, unsigned long portNumber, unsigned char index, const std::string& label)
{
    if (index == 0) return;

    HANDLE hHub = (HANDLE)hHubVoid;
    const DWORD reqSize = sizeof(USB_DESCRIPTOR_REQUEST) + 255;
    BYTE buffer[1024] = { 0 };
    USB_DESCRIPTOR_REQUEST* req = (USB_DESCRIPTOR_REQUEST*)buffer;
    
    req->ConnectionIndex = portNumber;
    req->SetupPacket.bmRequest = 0x80;
    req->SetupPacket.bRequest = 0x06;  
    req->SetupPacket.wValue = (USB_STRING_DESCRIPTOR_TYPE << 8) | index;
    req->SetupPacket.wIndex = 0x0409; // Language ID (English US)
    req->SetupPacket.wLength = 255;

    DWORD bytesReturned = 0;
    if (DeviceIoControl(hHub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, req, reqSize, req, reqSize, &bytesReturned, NULL))
    {
        BYTE bLength = req->Data[0];
        BYTE bType = req->Data[1];
        
        if (bLength > 2 && bType == USB_STRING_DESCRIPTOR_TYPE)
        {
            int numChars = (bLength - 2) / 2;
            std::wstring wstr((WCHAR*)&req->Data[2], numChars);
            std::string str(wstr.begin(), wstr.end());
            std::cout << label << ":\n" << str << "\n\n";
        }
    }
}

void UsbScanner::LeerConfigurationDescriptor(void* hHubVoid, unsigned long portNumber)
{
    HANDLE hHub = (HANDLE)hHubVoid;
    std::cout << "CONFIGURATION\n\n";
    
    // Leer los 9 bytes de cabecera primero
    const DWORD reqSize1 = sizeof(USB_DESCRIPTOR_REQUEST) + sizeof(USB_CONFIGURATION_DESCRIPTOR);
    BYTE buffer1[1024] = { 0 };
    USB_DESCRIPTOR_REQUEST* req1 = (USB_DESCRIPTOR_REQUEST*)buffer1;
    
    req1->ConnectionIndex = portNumber;
    req1->SetupPacket.bmRequest = 0x80;
    req1->SetupPacket.bRequest = 0x06;
    req1->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | 0;
    req1->SetupPacket.wIndex = 0;
    req1->SetupPacket.wLength = sizeof(USB_CONFIGURATION_DESCRIPTOR);
    
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hHub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, req1, reqSize1, req1, reqSize1, &bytesReturned, NULL))
    {
        std::cout << "Error leyendo cabecera Configuration.\n";
        return;
    }
    
    USB_CONFIGURATION_DESCRIPTOR* cfgDesc = (USB_CONFIGURATION_DESCRIPTOR*)(req1->Data);
    WORD totalLength = cfgDesc->wTotalLength;
    std::cout << "Configuration Value: " << (int)cfgDesc->bConfigurationValue << "\n\n";
    
    // Leer configuracion completa
    DWORD reqSize2 = sizeof(USB_DESCRIPTOR_REQUEST) + totalLength;
    std::vector<BYTE> buffer2(reqSize2, 0);
    USB_DESCRIPTOR_REQUEST* req2 = (USB_DESCRIPTOR_REQUEST*)buffer2.data();
    
    req2->ConnectionIndex = portNumber;
    req2->SetupPacket.bmRequest = 0x80;
    req2->SetupPacket.bRequest = 0x06;
    req2->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | 0;
    req2->SetupPacket.wIndex = 0;
    req2->SetupPacket.wLength = totalLength;
    
    if (DeviceIoControl(hHub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, req2, reqSize2, req2, reqSize2, &bytesReturned, NULL))
    {
        BYTE* dataPtr = req2->Data;
        DWORD offset = 0;
        
        while (offset < totalLength)
        {
            BYTE bLength = dataPtr[offset];
            BYTE bType = dataPtr[offset + 1];
            
            if (bLength == 0) {
                std::cout << "Descriptor inválido de longitud 0 encontrado en el offset " << offset << ". Terminando parseo.\n";
                break;
            }
            
            std::cout << "Offset " << offset << "\n";
            std::cout << "Length: " << (int)bLength << "\n";
            std::cout << "Type:   " << (int)bType << " ";
            
            if (bType == 2) {
                std::cout << "(CONFIGURATION)\n";
            }
            else if (bType == 4) { // INTERFACE
                std::cout << "(INTERFACE)\n";
                if (bLength >= 9) {
                    USB_INTERFACE_DESCRIPTOR* intfDesc = (USB_INTERFACE_DESCRIPTOR*)(dataPtr + offset);
                    std::cout << "  Interface Number: " << (int)intfDesc->bInterfaceNumber << "\n";
                    std::cout << "  Alternate Setting:" << (int)intfDesc->bAlternateSetting << "\n";
                    std::cout << "  Class:            0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int)intfDesc->bInterfaceClass << std::dec << "\n";
                    std::cout << "  Subclass:         0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int)intfDesc->bInterfaceSubClass << std::dec << "\n";
                    std::cout << "  Protocol:         0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int)intfDesc->bInterfaceProtocol << std::dec << "\n";
                    std::cout << "  Endpoints:        " << (int)intfDesc->bNumEndpoints << "\n";
                }
            }
            else if (bType == 5) { // ENDPOINT
                std::cout << "(ENDPOINT)\n";
                if (bLength >= 7) {
                    USB_ENDPOINT_DESCRIPTOR* epDesc = (USB_ENDPOINT_DESCRIPTOR*)(dataPtr + offset);
                    std::cout << "  Endpoint Address: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int)epDesc->bEndpointAddress << std::dec << "\n";
                    std::cout << "  Attributes:       0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int)epDesc->bmAttributes << std::dec << "\n";
                    std::cout << "  Max Packet Size:  " << epDesc->wMaxPacketSize << "\n";
                }
            }
            else if (bType == 0x24) { // CS_INTERFACE
                std::cout << "(CS_INTERFACE)\n";
                if (bLength >= 3) {
                    std::cout << "  Subtype:          0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int)dataPtr[offset + 2] << std::dec << "\n";
                    
                    std::cout << "  Data:             ";
                    for(int i = 3; i < bLength; i++) {
                        std::cout << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int)dataPtr[offset + i] << " ";
                    }
                    std::cout << std::dec << "\n";
                }
            }
            else if (bType == 0x25) { // CS_ENDPOINT
                std::cout << "(CS_ENDPOINT)\n";
                if (bLength >= 3) {
                    std::cout << "  Subtype:          0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int)dataPtr[offset + 2] << std::dec << "\n";
                }
            }
            else {
                std::cout << "(UNKNOWN/VENDOR)\n";
                std::cout << "  Data:             ";
                for(int i = 2; i < bLength; i++) {
                    std::cout << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int)dataPtr[offset + i] << " ";
                }
                std::cout << std::dec << "\n";
            }
            std::cout << "\n";
            
            offset += bLength;
        }
    }
}