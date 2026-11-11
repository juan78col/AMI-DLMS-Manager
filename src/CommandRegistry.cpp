#include "CommandRegistry.h"

namespace ami {

    const std::unordered_map<std::string, CommandType>& protocolCodes() {
        static const std::unordered_map<std::string, CommandType> table = {
            // DLMS
            { "C01", CommandType::InstantaneousValues },
            { "C02", CommandType::ProfileGeneric },
            { "C03", CommandType::Reconnections },
            { "C04", CommandType::PowerLimit },
            { "C05", CommandType::TariffChange },
            { "C06", CommandType::ProfileChange },
            { "C07", CommandType::ProfileTimeChange },
            { "X01", CommandType::CustomObisRead },
            { "C08", CommandType::MeterIdValidator },
            { "C09", CommandType::EventProfilesByDate },
            { "X02", CommandType::CustomObisWrite },

            // RF
            { "RF1",  CommandType::RfStopReading },
            { "RF2",  CommandType::RfClearMeasurement },
            { "RF3",  CommandType::RfClearTask },
            { "RF4",  CommandType::RfF33 },
            { "RF5",  CommandType::RfF38 },
            { "RF6",  CommandType::RfF39 },
            { "RF7",  CommandType::RfF40 },
            { "RF8",  CommandType::RfReadVersion },
            { "RF9",  CommandType::RfReadNetworkParam },
            { "RF10", CommandType::RfSetNetworkParam },
            { "RF11", CommandType::RfAddNetPoint },
            { "RF12", CommandType::RfSetMeasurementPoint },
            { "RF13", CommandType::RfAddBroadcastPoint },
            { "RF14", CommandType::RfStartNetworking },
            { "RF15", CommandType::RfReadNodeLevel },
            { "RF16", CommandType::RfStopBuild },
        };
        return table;
    }

    CommandType resolveProtocolCode(const std::string& code) {
        const auto& table = protocolCodes();
        auto it = table.find(code);
        return (it != table.end()) ? it->second : CommandType::Unknown;
    }

} // namespace ami