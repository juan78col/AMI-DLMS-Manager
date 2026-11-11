#pragma once
#include <string>
#include <unordered_map>

namespace ami {

    // ─────────────────────────────────────────────────────────────────────
    //  Contrato del protocolo — NO personalizable por cliente
    // ─────────────────────────────────────────────────────────────────────
    enum class CommandType : int {
        Unknown = -1,

        // DLMS
        InstantaneousValues = 0,    // C01
        ProfileGeneric = 1,    // C02
        Reconnections = 2,    // C03
        PowerLimit = 3,    // C04
        TariffChange = 4,    // C05
        ProfileChange = 5,    // C06
        ProfileTimeChange = 6,    // C07
        CustomObisRead = 7,    // X01
        MeterIdValidator = 8,    // C08
        EventProfilesByDate = 9,    // C09
        CustomObisWrite = 10,   // X02

        // RF (concentrador)
        RfStopReading = 100,
        RfClearMeasurement = 101,
        RfClearTask = 102,
        RfF33 = 103,
        RfF38 = 104,
        RfF39 = 105,
        RfF40 = 106,
        RfReadVersion = 107,
        RfReadNetworkParam = 108,
        RfSetNetworkParam = 109,
        RfAddNetPoint = 110,
        RfSetMeasurementPoint = 111,
        RfAddBroadcastPoint = 112,
        RfStartNetworking = 113,
        RfReadNodeLevel = 114,
        RfStopBuild = 115,
    };

    // Tabla: código de protocolo ("C01") → tipo estándar
    const std::unordered_map<std::string, CommandType>& protocolCodes();

    // Helper: resuelve un código de protocolo, o Unknown si no existe
    CommandType resolveProtocolCode(const std::string& code);

    // Helper: ¿es un comando RF?
    inline bool isRfCommand(CommandType t) {
        int v = static_cast<int>(t);
        return v >= 100 && v <= 200;
    }

} // namespace ami