#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <initguid.h>

#include <expected>
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <iostream>
#include <iomanip>

DEFINE_GUID(GUID_DEVINTERFACE_GENIUS_CAM,
    0x15B22409, 0x5FEF, 0x4481, 0x9E, 0xFD, 0x6F, 0x4E, 0x85, 0x88, 0x25, 0x12);

DEFINE_GUID(GUID_DEVCLASS_CAMERA_GENIUS,
    0xca3e7ab9, 0xb4c3, 0x4ae6, 0x82, 0x51, 0x57, 0x9e, 0xf9, 0x33, 0x89, 0x0f);

DEFINE_GUID(GUID_DEVINTERFACE_WINUSB_GENERIC,
    0x88BAE032, 0x5A81, 0x49F0, 0xBC, 0x3D, 0xA4, 0xFF, 0x13, 0x82, 0x16, 0xD6);

namespace Genius {

    // Registros Oficiales del procesador Sonix SN9C102
    namespace SN9C102 {
        namespace Regs {
            constexpr std::uint16_t ASIC_ID     = 0x0000; // Retorna 0x10
            constexpr std::uint16_t SYS_CONTROL = 0x0001; // Power down, V_TX_EN, LED, KEY, 24M/12M
            constexpr std::uint16_t GPIO        = 0x0002; // GPIO[1:0]
            constexpr std::uint16_t I2C_CTRL    = 0x0008; // Control del bus I2C (Velocidad, RD/WR, Status)
            constexpr std::uint16_t SLAVE_ID    = 0x0009; // ID I2C del sensor CMOS
            constexpr std::uint16_t I2C_DATA0   = 0x000A; // Buffer de datos I2C (0x0A a 0x0E)
            constexpr std::uint16_t CTRL_STATUS = 0x000F; // Estado y control
            constexpr std::uint16_t GAIN_R_B    = 0x0010; // Ganancia canales Rojo (0:3) y Azul (4:7)
            constexpr std::uint16_t GAIN_G      = 0x0011; // Ganancia canal Verde (0:3)
            constexpr std::uint16_t H_START     = 0x0012; // Píxel de inicio HSYNC
            constexpr std::uint16_t V_START     = 0x0013; // Línea de inicio VSYNC
            constexpr std::uint16_t OFFSET      = 0x0014; // Offset de datos de imagen
            constexpr std::uint16_t H_SIZE      = 0x0015; // Tamaño horizontal de píxeles
            constexpr std::uint16_t V_SIZE_CLK  = 0x0016; // Tamaño vertical, LQ_SEL, SEN_RATE, SEN_CLK_EN
            constexpr std::uint16_t TIMING_SCAL = 0x0017; // Flancos de reloj, SCAL (1/1, 1/2, 1/4), CMP_MODE
            constexpr std::uint16_t SYNC_CLK_OUT= 0x0018; // Control de sincronización y reloj PCK
            constexpr std::uint16_t MCK_HO_SIZE= 0x0019; // Divisor de reloj máster y HO_SIZE
            constexpr std::uint16_t VO_SIZE     = 0x001A; // VO_SIZE
            constexpr std::uint16_t AE_STRX     = 0x001B; // Auto-Exposición Inicio X
            constexpr std::uint16_t AE_STRY     = 0x001C; // Auto-Exposición Inicio Y
            constexpr std::uint16_t AE_ENDX     = 0x001D; // Auto-Exposición Fin X
            constexpr std::uint16_t AE_ENDY     = 0x001E; // Auto-Exposición Fin Y
        }
    }

    enum class ConnectionError : std::uint8_t {
        DeviceNotFound,
        PathResolutionFailed,
        FileAccessDenied,
        WinUsbInitFailed
    };

    struct DeviceInfoListDeleter {
        void operator()(HDEVINFO h) const noexcept {
            if (h != INVALID_HANDLE_VALUE && h != nullptr) {
                SetupDiDestroyDeviceInfoList(h);
            }
        }
    };
    using UniqueDeviceInfoList = std::unique_ptr<std::remove_pointer_t<HDEVINFO>, DeviceInfoListDeleter>;

    struct HandleDeleter {
        void operator()(HANDLE h) const noexcept {
            if (h != INVALID_HANDLE_VALUE && h != nullptr) {
                CloseHandle(h);
            }
        }
    };
    using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

    struct WinUsbDeleter {
        void operator()(WINUSB_INTERFACE_HANDLE h) const noexcept {
            if (h != nullptr) {
                WinUsb_Free(h);
            }
        }
    };
    using UniqueWinUsbHandle = std::unique_ptr<void, WinUsbDeleter>;

    class DeviceBridge {
    public:
        constexpr DeviceBridge() noexcept = default;

        [[nodiscard]] std::expected<void, ConnectionError> connect() noexcept {
            auto device_path = resolve_device_path();
            if (!device_path) {
                return std::unexpected(ConnectionError::DeviceNotFound);
            }

            std::cout << "[INFO] Ruta del dispositivo encontrada: " << *device_path << "\n";

            HANDLE raw_file_handle = CreateFileA(
                device_path->c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                nullptr
            );

            if (raw_file_handle == INVALID_HANDLE_VALUE) {
                std::cerr << "Error al abrir el archivo del dispositivo. Código OS: " << GetLastError() << "\n";
                return std::unexpected(ConnectionError::FileAccessDenied);
            }
            m_file_handle.reset(raw_file_handle);

            WINUSB_INTERFACE_HANDLE raw_winusb_handle = nullptr;
            if (!WinUsb_Initialize(m_file_handle.get(), &raw_winusb_handle)) {
                std::cerr << "Error al inicializar WinUSB. Código OS: " << GetLastError() << "\n";
                return std::unexpected(ConnectionError::WinUsbInitFailed);
            }
            m_winusb_handle.reset(raw_winusb_handle);

            std::cout << "Canal WinUSB abierto y vinculado correctamente con la cámara.\n";
            return {};
        }

        [[nodiscard]] std::expected<void, ConnectionError> send_register_write(std::uint16_t reg, std::uint8_t value) const noexcept {
            auto* handle = static_cast<WINUSB_INTERFACE_HANDLE>(m_winusb_handle.get());
            if (!handle) {
                return std::unexpected(ConnectionError::WinUsbInitFailed);
            }

            WINUSB_SETUP_PACKET setup_packet{};
            setup_packet.RequestType = 0x41; // OUT | Vendor | Interface
            setup_packet.Request = 0x08;     // Write register
            setup_packet.Value = reg;
            setup_packet.Index = 0;
            setup_packet.Length = 1;

            ULONG bytes_written = 0;
            BOOL success = WinUsb_ControlTransfer(
                handle,
                setup_packet,
                &value,
                1,
                &bytes_written,
                nullptr
            );

            if (!success || bytes_written != 1) {
                std::cerr << "Error en WinUsb_ControlTransfer (Escritura). Código OS: " << GetLastError() << "\n";
                return std::unexpected(ConnectionError::WinUsbInitFailed);
            }

            return {};
        }

        [[nodiscard]] std::expected<std::uint8_t, ConnectionError> send_register_read(std::uint16_t reg) const noexcept {
            auto* handle = static_cast<WINUSB_INTERFACE_HANDLE>(m_winusb_handle.get());
            if (!handle) {
                return std::unexpected(ConnectionError::WinUsbInitFailed);
            }

            WINUSB_SETUP_PACKET setup_packet{};
            setup_packet.RequestType = 0xC1; // IN | Vendor | Interface
            setup_packet.Request = 0x00;     // Read register
            setup_packet.Value = reg;
            setup_packet.Index = 0;
            setup_packet.Length = 1;

            std::uint8_t value = 0;
            ULONG bytes_read = 0;
            BOOL success = WinUsb_ControlTransfer(
                handle,
                setup_packet,
                &value,
                1,
                &bytes_read,
                nullptr
            );

            if (!success || bytes_read != 1) {
                std::cerr << "Error en WinUsb_ControlTransfer (Lectura). Código OS: " << GetLastError() << "\n";
                return std::unexpected(ConnectionError::WinUsbInitFailed);
            }

            return value;
        }

        [[nodiscard]] constexpr PVOID get_winusb_interface() const noexcept {
            return m_winusb_handle.get();
        }

    private:
        UniqueHandle m_file_handle{nullptr};
        UniqueWinUsbHandle m_winusb_handle{nullptr};

        [[nodiscard]] std::optional<std::string> resolve_device_path() const noexcept {
            const GUID search_guids[] = {
                GUID_DEVINTERFACE_GENIUS_CAM,
                GUID_DEVCLASS_CAMERA_GENIUS,
                GUID_DEVINTERFACE_WINUSB_GENERIC
            };

            for (const auto& guid : search_guids) {
                HDEVINFO raw_info = SetupDiGetClassDevsA(
                    &guid,
                    nullptr,
                    nullptr,
                    DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
                );

                if (raw_info == INVALID_HANDLE_VALUE) continue;
                UniqueDeviceInfoList device_info_list(raw_info);

                SP_DEVICE_INTERFACE_DATA interface_data{};
                interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

                for (DWORD index = 0; SetupDiEnumDeviceInterfaces(device_info_list.get(), nullptr, &guid, index, &interface_data); ++index) {
                    DWORD required_size = 0;
                    SetupDiGetDeviceInterfaceDetailA(device_info_list.get(), &interface_data, nullptr, 0, &required_size, nullptr);

                    if (required_size == 0) continue;

                    std::vector<std::uint8_t> detail_buffer(required_size);
                    auto* detail_data = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_A>(detail_buffer.data());
                    detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

                    if (SetupDiGetDeviceInterfaceDetailA(device_info_list.get(), &interface_data, detail_data, required_size, nullptr, nullptr)) {
                        std::string path(detail_data->DevicePath);
                        if (path.find("vid_0c45&pid_60b0") != std::string::npos ||
                            path.find("VID_0C45&PID_60B0") != std::string::npos ||
                            path.find("0c45") != std::string::npos) {
                            return path;
                        }
                    }
                }
            }

            return std::nullopt;
        }
    };
}
