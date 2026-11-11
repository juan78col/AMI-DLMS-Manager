// dlms_profile.h
#pragma once
#include <cstdint>
#include <string>

/**
 * Perfil de cliente DLMS. Todos los enums del stack se guardan como enteros
 * crudos (uint32_t / uint64_t) para que cualquier valor futuro de la norma o
 * de un fabricante quepa sin recompilar.
 */
struct DlmsClientProfile {
    int id = -1;
    int typeclcosem = -1;

    // Direccionamiento
    bool     useLogicalName = true;
    uint32_t interfaceType = 1;   // 0=HDLC, 1=WRAPPER
    uint32_t clientAddress = 1;
    uint32_t serverAddress = 1;
    bool     useServerAddrHelper = false;
    uint32_t helperLogical = 1;
    uint32_t helperPhysical = 0x11;
    uint32_t helperAddress = 0;

    // Autenticación
    uint32_t    authentication = 0;   // 0=NONE,1=LOW,2=HLS,...
    bool        useDevicePassword = true;
    std::string passwordLiteral;

    // Conformance (máscaras crudas — bit_or de la BD)
    bool     hasProposed = false;
    uint64_t proposedConformance = 0;
    bool     hasNegotiated = false;
    uint64_t negotiatedConformance = 0;

    // Servicio / PDU / fechas / seguridad
    uint32_t serviceClass = 0;
    uint32_t maxReceivePduSize = 0;
    bool     hasDateTimeSkips = false;
    uint64_t dateTimeSkips = 0;
    uint32_t security = 0;

    // HDLC
    bool     hasHdlc = false;
    uint32_t hdlcWindowTx = 1;
    uint32_t hdlcWindowRx = 1;
    uint32_t hdlcMaxInfoTx = 0;
    uint32_t hdlcMaxInfoRx = 0;

    // InvokeID
    bool setAutoIncreaseInvokeId = true;
    bool autoIncreaseInvokeId = false;

    // Compatibilidad con el resto del código
    int meterType = 1;
    int typeInterface = 1;
    
    // Seguridad (Configuración del perfil)
    uint32_t securitySuite = 0;
    uint32_t securityLevel = 0; // 0=NONE, 1=AUTH, 2=ENC, 3=AUTH_ENC (mapeado de DLMS_SECURITY)

    // Credenciales cifradas de la entidad resuelta (medidor o DCU).
    std::string systemTitleCifrado;
    std::string cipherKeyCifrada;
    std::string authenticationKeyCifrada;
    std::string dedicatedKeyCifrada;
};