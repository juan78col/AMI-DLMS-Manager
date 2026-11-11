#include "DlmsState.h"

namespace ami {

    bool isKnownDlmsState(int v) {
        switch (static_cast<DlmsState>(v)) {
        case DlmsState::Idle:
        case DlmsState::SnrmSent:
        case DlmsState::AarqSent:
        case DlmsState::AareSent:
        case DlmsState::DisconnectSent:
        case DlmsState::InstantaneousResp:
        case DlmsState::ProfileGeneric1Resp:
        case DlmsState::ProfileGeneric2Resp:
        case DlmsState::GetMeterIdResp:
        case DlmsState::InvocationCounterResp:
            return true;
        }
        return false;
    }

    const char* DlmsStateName(DlmsState s) {
        switch (s) {
        case DlmsState::Idle:                  return "Idle";
        case DlmsState::SnrmSent:              return "SnrmSent";
        case DlmsState::AarqSent:              return "AarqSent";
        case DlmsState::AareSent:              return "AareSent";
        case DlmsState::DisconnectSent:        return "DisconnectSent";
        case DlmsState::InstantaneousResp:     return "InstantaneousResp";
        case DlmsState::ProfileGeneric1Resp:   return "ProfileGeneric1Resp";
        case DlmsState::ProfileGeneric2Resp:   return "ProfileGeneric2Resp";
        case DlmsState::GetMeterIdResp:        return "GetMeterIdResp";
        case DlmsState::InvocationCounterResp: return "InvocationCounterResp";
        }
        return "Unknown";
    }

} // namespace ami