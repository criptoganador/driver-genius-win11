// SonixScanner.cpp
// Analizador de hardware del chip Sonix SN9Cxxx
// Fuente de referencia: sn9c20x.c (gspca, GPL-2.0)
//
// Control Transfer para leer registros:
//   bmRequestType = 0xC1  (IN | Vendor | Interface)
//   bRequest      = 0x00  (Read Register)
//   wValue        = dirección del registro
//   wIndex        = 0x0000
//   wLength       = 1

#include "SonixScanner.h"
#include <setupapi.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <map>

// GUID de la interfaz MI_00 definido en genius-win11.inf
static const GUID GUID_GENIUS_MI00 = {
    0x4e1b39d5, 0x61a6, 0x47a6,
    { 0x9e, 0x92, 0x3b, 0x48, 0xd2, 0xa1, 0x23, 0x45 }
};

SonixScanner::SonixScanner()
    : m_hDevice(INVALID_HANDLE_VALUE)
    , m_hWinUsb(NULL)
{}

SonixScanner::~SonixScanner()
{
    Cerrar();
}

std::string SonixScanner::BuscarDevicePath()
{
    HDEVINFO hInfo = SetupDiGetClassDevsA(
        &GUID_GENIUS_MI00, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (hInfo == INVALID_HANDLE_VALUE) return "";

    SP_DEVICE_INTERFACE_DATA intData = {};
    intData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    std::string result = "";
    if (SetupDiEnumDeviceInterfaces(hInfo, NULL, &GUID_GENIUS_MI00, 0, &intData))
    {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailA(hInfo, &intData, NULL, 0, &reqSize, NULL);
        if (reqSize > 0) {
            SP_DEVICE_INTERFACE_DETAIL_DATA_A* det =
                (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)malloc(reqSize);
            if (det) {
                det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
                if (SetupDiGetDeviceInterfaceDetailA(hInfo, &intData, det, reqSize, NULL, NULL))
                    result = det->DevicePath;
                free(det);
            }
        }
    }
    SetupDiDestroyDeviceInfoList(hInfo);
    return result;
}

bool SonixScanner::Abrir()
{
    std::string path = BuscarDevicePath();
    if (path.empty()) {
        std::cout << "[SonixScanner] ERROR: No se encontro el GUID de WinUSB.\n";
        std::cout << "               Verifica que genius-win11.inf esta instalado.\n";
        return false;
    }

    m_hDevice = CreateFileA(path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

    if (m_hDevice == INVALID_HANDLE_VALUE) {
        std::cout << "[SonixScanner] ERROR CreateFile: " << GetLastError() << "\n";
        return false;
    }

    if (!WinUsb_Initialize(m_hDevice, &m_hWinUsb)) {
        std::cout << "[SonixScanner] ERROR WinUsb_Initialize: " << GetLastError() << "\n";
        CloseHandle(m_hDevice);
        m_hDevice = INVALID_HANDLE_VALUE;
        return false;
    }

    return true;
}

void SonixScanner::Cerrar()
{
    if (m_hWinUsb) { WinUsb_Free(m_hWinUsb); m_hWinUsb = NULL; }
    if (m_hDevice != INVALID_HANDLE_VALUE) { CloseHandle(m_hDevice); m_hDevice = INVALID_HANDLE_VALUE; }
}

bool SonixScanner::LeerRegistro(WORD direccion, BYTE& valorSalida)
{
    if (!m_hWinUsb) return false;

    WINUSB_SETUP_PACKET pkt = {};
    pkt.RequestType = 0xC1; // IN | Vendor | Interface
    pkt.Request     = 0x00; // Read register
    pkt.Value       = direccion;
    pkt.Index       = 0x0000;
    pkt.Length      = 1;

    valorSalida = 0;
    ULONG transferred = 0;
    return WinUsb_ControlTransfer(m_hWinUsb, pkt, &valorSalida, 1, &transferred, NULL) == TRUE;
}

// Interpreta registros conocidos del chip SN9C20x/SN9C1xx
std::string SonixScanner::InterpretarRegistro(WORD dir, BYTE val)
{
    // Mapeo para el SN9C102 / SN9C1xx (rango 0x00 - 0x1F)
    switch (dir) {
        case 0x00: return (val == 0x10) ? "Chip ID OK (SN9C10x)" : "Chip ID Desconocido";
        case 0x01: return "Control Transfer / Start (Bit0: 1=Stop, Bit5: PowerDown)";
        case 0x02: return "Clock / Reset";
        case 0x08: return "I2C Base / Control";
        case 0x09: return "I2C Address / Data";
        case 0x12: return "Window H_START";
        case 0x13: return "Window V_START";
        case 0x15: return "Window Size / Resolution";
        case 0x17: return "SensorClk (Clock del bus pixel)";
        case 0x18: return "Compression Control / Format";
        case 0x19: return "MCKSIZE (Master Clock)";
        case 0x1c: return "Auto-Exposure / ROI";
        
        // Mapeo original por si acaso:
        case 0x1000: return "Control (Si responde 0x10 aqui, el chip ignoro el 0x1000 y leyo 0x00)";
        default:     return "";
    }
}

void SonixScanner::EscanearRango(WORD inicio, WORD fin)
{
    std::cout << "\n===== ESCANEO DE REGISTROS SONIX =====\n";
    std::cout << "Rango: 0x" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase
              << inicio << " - 0x" << std::setw(4) << fin << std::dec << "\n\n";

    int leidos = 0;
    int errores = 0;

    for (WORD reg = inicio; reg <= fin; reg++)
    {
        BYTE val = 0;
        if (LeerRegistro(reg, val))
        {
            std::string nota = InterpretarRegistro(reg, val);

            std::cout << "Reg[0x"
                      << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int)reg
                      << "] = 0x" << std::setw(2) << (int)val
                      << std::dec;

            if (!nota.empty())
                std::cout << "  <- " << nota;

            std::cout << "\n";
            leidos++;
        }
        else
        {
            errores++;
            if (errores <= 3)
                std::cout << "Reg[0x" << std::hex << std::setfill('0') << std::setw(4)
                          << std::uppercase << (int)reg << "] = --- (no responde)\n" << std::dec;
            if (errores == 4)
                std::cout << "(Mas registros no responden - omitiendo...)\n";
        }

        if (leidos > 0 && leidos % 16 == 0)
            std::cout << "\n";
    }

    std::cout << "\n[Resumen] Leidos: " << leidos << "  Errores: " << errores << "\n";
    std::cout << "======================================\n";
}

void SonixScanner::IdentificarChip()
{
    std::cout << "\n===== IDENTIFICACION DEL CHIP SONIX =====\n";

    const WORD REGS_ID[] = {
        0x00, 0x01, 0x02, 0x08, 0x09, 0x12, 0x13, 0x15, 0x17, 0x18, 0x19, 0x1c
    };
    int N = sizeof(REGS_ID) / sizeof(REGS_ID[0]);

    int respuestas = 0;
    for (int i = 0; i < N; i++)
    {
        BYTE val = 0;
        if (LeerRegistro(REGS_ID[i], val))
        {
            std::string nota = InterpretarRegistro(REGS_ID[i], val);
            std::cout << "Reg[0x" << std::hex << std::setfill('0') << std::setw(4)
                      << std::uppercase << (int)REGS_ID[i]
                      << "] = 0x" << std::setw(2) << (int)val << std::dec;
            if (!nota.empty()) std::cout << "  <- " << nota;
            std::cout << "\n";
            respuestas++;
        }
    }

    std::cout << "\n";

    if (respuestas == 0) {
        std::cout << ">> CHIP NO RESPONDE a lecturas con bRequest=0x00 + bmRequestType=0xC1.\n";
        std::cout << "   Posibles causas:\n";
        std::cout << "   1. El chip es SN9C10x y usa bRequest diferente.\n";
        std::cout << "   2. Se necesita enviar bridge_init antes de leer.\n";
        std::cout << "   3. El driver WinUSB no tiene acceso a la interfaz.\n";
    } else {
        std::cout << ">> Chip respondio a " << respuestas << "/" << N << " registros.\n";

        BYTE v0 = 0;
        if (LeerRegistro(0x00, v0)) {
            std::cout << ">> Reg[0x00] = 0x" << std::hex << std::setw(2)
                      << std::setfill('0') << std::uppercase << (int)v0 << std::dec << "\n";
            if (v0 == 0x10)
                std::cout << "   -> CHIP CONFIRMADO: Familia SN9C102 / SN9C105 (Driver sonixb)\n";
            else
                std::cout << "   -> Valor no reconocido. Requiere mas investigacion.\n";
        }
    }

    std::cout << "==========================================\n";
}

void SonixScanner::AnalizarI2C()
{
    std::cout << "\n===== NIVEL 7: ANALISIS DEL BUS I2C =====\n";
    std::cout << "En el chip SN9C102, I2C se maneja en los registros 0x08 - 0x09.\n";
    
    // Escanear rango de I2C en SN9C10x
    EscanearRango(0x08, 0x0F);
    
    std::cout << "=========================================\n";
}

bool SonixScanner::ExportarVolcado(WORD inicio, WORD fin, const std::string& rutaArchivo)
{
    std::ofstream out(rutaArchivo);
    if (!out.is_open()) return false;

    std::cout << "[SonixScanner] Exportando volcado de 0x" << std::hex << std::setfill('0') << std::setw(4) << inicio << " a 0x" << fin << " -> " << rutaArchivo << "\n";

    int leidos = 0;
    for (WORD reg = inicio; reg <= fin; reg++)
    {
        BYTE val = 0;
        if (LeerRegistro(reg, val))
        {
            out << std::hex << std::setfill('0') << std::uppercase 
                << std::setw(4) << (int)reg << "=" << std::setw(2) << (int)val << "\n";
            leidos++;
        }
    }
    out.close();
    std::cout << "[SonixScanner] Volcado completado: " << std::dec << leidos << " registros respondieron.\n";
    return true;
}

void SonixScanner::CompararVolcados(const std::string& archivo1, const std::string& archivo2)
{
    std::cout << "\n===== COMPARANDO VOLCADOS =====\n";
    std::cout << "1: " << archivo1 << "\n2: " << archivo2 << "\n\n";

    auto cargarMapa = [](const std::string& archivo) -> std::map<WORD, BYTE> {
        std::map<WORD, BYTE> m;
        std::ifstream in(archivo);
        std::string linea;
        while (std::getline(in, linea)) {
            if (linea.empty()) continue;
            size_t eq = linea.find('=');
            if (eq != std::string::npos) {
                WORD reg = (WORD)std::stoul(linea.substr(0, eq), nullptr, 16);
                BYTE val = (BYTE)std::stoul(linea.substr(eq + 1), nullptr, 16);
                m[reg] = val;
            }
        }
        return m;
    };

    std::map<WORD, BYTE> m1 = cargarMapa(archivo1);
    std::map<WORD, BYTE> m2 = cargarMapa(archivo2);

    int diffCount = 0;
    for (auto const& [reg, val2] : m2) {
        auto it1 = m1.find(reg);
        if (it1 != m1.end()) {
            BYTE val1 = it1->second;
            if (val1 != val2) {
                std::cout << "Reg[0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << reg 
                          << "] CAMBIO: 0x" << std::setw(2) << (int)val1 << " -> 0x" << std::setw(2) << (int)val2 << "\n";
                diffCount++;
            }
        } else {
            std::cout << "Reg[0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << reg 
                      << "] NUEVO en 2: 0x" << std::setw(2) << (int)val2 << "\n";
            diffCount++;
        }
    }
    std::cout << std::dec << "\nTotal diferencias: " << diffCount << "\n";
    std::cout << "===============================\n";
}


