#pragma once
// SonixScanner.h
// Analizador de hardware Sonix SN9Cxxx
// Lee registros internos del chip vía WinUSB + Control Transfer
// para identificar el ASIC, versión y configuración del bridge

#include <windows.h>
#include <winusb.h>
#include <string>
#include <vector>

struct SonixRegInfo
{
    WORD  direccion;
    BYTE  valor;
    bool  valida;
};

class SonixScanner
{
public:
    SonixScanner();
    ~SonixScanner();

    // Abre el handle WinUSB de la cámara
    bool Abrir();
    void Cerrar();

    // Lee un solo registro del chip
    bool LeerRegistro(WORD direccion, BYTE& valorSalida);

    // Escanea un rango completo de registros e imprime resultados
    void EscanearRango(WORD inicio, WORD fin);

    // Vuelca un rango completo a un archivo de texto (solo los que responden)
    bool ExportarVolcado(WORD inicio, WORD fin, const std::string& rutaArchivo);

    // Compara dos volcados previos y muestra las diferencias
    static void CompararVolcados(const std::string& archivo1, const std::string& archivo2);

    // Análisis específico: identifica el chip por registros conocidos
    void IdentificarChip();

    // Nivel 7: Análisis del bus I2C interno (Sensor)
    void AnalizarI2C();

private:

    HANDLE                  m_hDevice;
    WINUSB_INTERFACE_HANDLE m_hWinUsb;

    std::string BuscarDevicePath();
    std::string InterpretarRegistro(WORD dir, BYTE val);
};
