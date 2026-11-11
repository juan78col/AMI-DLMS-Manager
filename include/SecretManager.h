#pragma once

#include <array>
#include <string>
#include <string_view>

namespace ami::security {

class SecretManager final {
public:
    // Expects exactly 64 hexadecimal characters representing 32 random bytes.
    explicit SecretManager(std::string_view master_key_hex);
    ~SecretManager();

    SecretManager(const SecretManager&) = delete;
    SecretManager& operator=(const SecretManager&) = delete;
    SecretManager(SecretManager&&) = delete;
    SecretManager& operator=(SecretManager&&) = delete;

    // AAD should be stable and unique per meter and column, e.g.
    // "ami-secret-v1|<codigomedidor>|authenticationkey".
    [[nodiscard]] std::string encrypt(std::string_view plaintext,
                                      std::string_view aad) const;
    [[nodiscard]] std::string decrypt(std::string_view encoded,
                                      std::string_view aad) const;

private:
    std::array<unsigned char, 32> key_{};
};

} // namespace ami::security
