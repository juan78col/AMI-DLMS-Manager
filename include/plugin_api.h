#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct Heartbeat {
    std::string manufacturer_id;   // "inhemeter", "microstar", etc.
    std::string device_id;
    int device_type;               // 0 = DCU, 1 = Medidor Directo
	std::vector<uint8_t> ack_payload; // Payload opcional a enviar como ACK tras identificar el dispositivo
};

class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual std::string name() const = 0;

    // ¿Este plugin soporta este tipo de dispositivo (0 o 1)?
    virtual bool supports_device_type(int type) const = 0;

    virtual std::vector<std::vector<uint8_t>> magic_bytes() const = 0;
    virtual std::optional<Heartbeat> parse(std::span<const uint8_t> data) const = 0;

    // ACK opcional a enviar tras identificar
    //virtual std::vector<uint8_t> ack_payload() const { return {}; }
};