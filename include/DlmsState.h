#pragma once
#include <string>
#include <unordered_map>

namespace ami {

    // ─────────────────────────────────────────────────────────────────────
    //  Estados de la conversación DLMS — contrato del protocolo
    //  NO personalizable por cliente. Cambios aquí implican cambios en
    //  los handlers correspondientes en Session.
    // ─────────────────────────────────────────────────────────────────────
    enum class DlmsState : int {
        Idle = 0,    // sin conversación en curso

        // Handshake
        SnrmSent = 1,    // SNRM enviado, esperando UA → dispara AARQ
        AarqSent = 2,    // AARQ enviado, esperando AARE
        AareSent = 3,    // AARE procesado, esperando asociación

        // Desconexión
        DisconnectSent = 8,    // Disconnect enviado, esperando respuesta

        // Respuestas de comandos
        InstantaneousResp = 11,   // respuesta de valores instantáneos
        ProfileGeneric1Resp = 20,   // respuesta 1 de profile generic (estructura)
        ProfileGeneric2Resp = 21,   // respuesta 2 de profile generic (datos)
		DisconnectControl = 50, //enviar comando de desconexión
        DisconnectControlStatus = 60, //estado comando de desconexión
        GetMeterIdResp = 99,   // respuesta de GetIdMeter
        InvocationCounterResp = 101,  // respuesta de invocation counter
    };

    // ─────────────────────────────────────────────────────────────────────
    //  Helpers
    // ─────────────────────────────────────────────────────────────────────

    // ¿Es un estado válido conocido?
    bool isKnownDlmsState(int v);

    // Devuelve el nombre del estado (para logs)
    const char* DlmsStateName(DlmsState s);

} // namespace ami