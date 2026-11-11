#include "SecretManager.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ami::security {
namespace {
constexpr std::size_t kNonceSize = 12;
constexpr std::size_t kTagSize = 16;

using CtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::vector<unsigned char> decodeHex(std::string_view in) {
    if (in.size() != 64) {
        throw std::invalid_argument("AMI_MASTER_KEY_HEX must contain exactly 64 hex characters");
    }
    std::vector<unsigned char> out(32);
    for (std::size_t i = 0; i < 32; ++i) {
        const int hi = hexValue(in[i * 2]);
        const int lo = hexValue(in[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            OPENSSL_cleanse(out.data(), out.size());
            throw std::invalid_argument("AMI_MASTER_KEY_HEX contains a non-hexadecimal character");
        }
        out[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return out;
}

std::string base64Encode(const unsigned char* data, std::size_t len) {
    if (len == 0) return {};
    if (len > static_cast<std::size_t>(INT_MAX)) {
        throw std::length_error("Input too large for base64 encoding");
    }
    std::string out(4 * ((len + 2) / 3), '\0');
    const int n = EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(out.data()), data, static_cast<int>(len));
    if (n < 0) throw std::runtime_error("Base64 encoding failed");
    out.resize(static_cast<std::size_t>(n));
    return out;
}

std::vector<unsigned char> base64Decode(std::string_view in) {
    if (in.empty()) return {};
    if (in.size() % 4 != 0 || in.size() > static_cast<std::size_t>(INT_MAX)) {
        throw std::invalid_argument("Invalid base64 field");
    }
    std::vector<unsigned char> out((in.size() / 4) * 3);
    const int n = EVP_DecodeBlock(out.data(),
                                  reinterpret_cast<const unsigned char*>(in.data()),
                                  static_cast<int>(in.size()));
    if (n < 0) throw std::invalid_argument("Invalid base64 field");
    std::size_t actual = static_cast<std::size_t>(n);
    if (!in.empty() && in.back() == '=') --actual;
    if (in.size() > 1 && in[in.size() - 2] == '=') --actual;
    out.resize(actual);
    return out;
}

std::array<unsigned char, 32> parseKey(std::string_view hex) {
    auto raw = decodeHex(hex);
    std::array<unsigned char, 32> key{};
    std::copy(raw.begin(), raw.end(), key.begin());
    OPENSSL_cleanse(raw.data(), raw.size());
    return key;
}

void addAad(EVP_CIPHER_CTX* ctx, std::string_view aad, bool encrypting) {
    if (aad.size() > static_cast<std::size_t>(INT_MAX)) {
        throw std::length_error("AAD too large");
    }
    int out_len = 0;
    const int ok = encrypting
        ? EVP_EncryptUpdate(ctx, nullptr, &out_len,
                            reinterpret_cast<const unsigned char*>(aad.data()),
                            static_cast<int>(aad.size()))
        : EVP_DecryptUpdate(ctx, nullptr, &out_len,
                            reinterpret_cast<const unsigned char*>(aad.data()),
                            static_cast<int>(aad.size()));
    if (ok != 1) throw std::runtime_error("AES-GCM AAD processing failed");
}
} // namespace

SecretManager::SecretManager(std::string_view master_key_hex)
    : key_(parseKey(master_key_hex)) {}

SecretManager::~SecretManager() {
    OPENSSL_cleanse(key_.data(), key_.size());
}

std::string SecretManager::encrypt(std::string_view plaintext,
                                   std::string_view aad) const {
    if (plaintext.size() > static_cast<std::size_t>(INT_MAX)) {
        throw std::length_error("Plaintext too large");
    }

    std::array<unsigned char, kNonceSize> nonce{};
    std::array<unsigned char, kTagSize> tag{};
    if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
        throw std::runtime_error("Secure nonce generation failed");
    }

    CtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx) throw std::runtime_error("Could not allocate OpenSSL cipher context");

    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                            static_cast<int>(nonce.size()), nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key_.data(), nonce.data()) != 1) {
        throw std::runtime_error("AES-256-GCM initialization failed");
    }

    addAad(ctx.get(), aad, true);
    std::vector<unsigned char> ciphertext(plaintext.size() + 16);
    int written = 0;
    int final_written = 0;

    if (!plaintext.empty() &&
        EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &written,
                          reinterpret_cast<const unsigned char*>(plaintext.data()),
                          static_cast<int>(plaintext.size())) != 1) {
        OPENSSL_cleanse(ciphertext.data(), ciphertext.size());
        throw std::runtime_error("AES-GCM encryption failed");
    }
    if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + written, &final_written) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG,
                            static_cast<int>(tag.size()), tag.data()) != 1) {
        OPENSSL_cleanse(ciphertext.data(), ciphertext.size());
        throw std::runtime_error("AES-GCM finalization failed");
    }
    ciphertext.resize(static_cast<std::size_t>(written + final_written));

    const std::string result = "v1:" + base64Encode(nonce.data(), nonce.size()) + ":" +
                               base64Encode(ciphertext.data(), ciphertext.size()) + ":" +
                               base64Encode(tag.data(), tag.size());
    if (!ciphertext.empty()) OPENSSL_cleanse(ciphertext.data(), ciphertext.size());
    return result;
}

std::string SecretManager::decrypt(std::string_view encoded,
                                   std::string_view aad) const {
    if (!encoded.starts_with("v1:")) {
        throw std::invalid_argument("Unsupported secret format version");
    }
    const auto p1 = encoded.find(':', 3);
    const auto p2 = p1 == std::string_view::npos ? p1 : encoded.find(':', p1 + 1);
    if (p1 == std::string_view::npos || p2 == std::string_view::npos ||
        encoded.find(':', p2 + 1) != std::string_view::npos) {
        throw std::invalid_argument("Malformed encrypted secret");
    }

    auto nonce = base64Decode(encoded.substr(3, p1 - 3));
    auto ciphertext = base64Decode(encoded.substr(p1 + 1, p2 - p1 - 1));
    auto tag = base64Decode(encoded.substr(p2 + 1));

    if (nonce.size() != kNonceSize || tag.size() != kTagSize) {
        throw std::invalid_argument("Malformed encrypted secret lengths");
    }
    if (ciphertext.size() > static_cast<std::size_t>(INT_MAX)) {
        throw std::length_error("Ciphertext too large");
    }

    CtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx) throw std::runtime_error("Could not allocate OpenSSL cipher context");

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                            static_cast<int>(nonce.size()), nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key_.data(), nonce.data()) != 1) {
        throw std::runtime_error("AES-256-GCM initialization failed");
    }

    addAad(ctx.get(), aad, false);
    std::string plaintext(ciphertext.size() + 16, '\0');
    int written = 0;
    int final_written = 0;

    if (!ciphertext.empty() &&
        EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(plaintext.data()),
                          &written, ciphertext.data(),
                          static_cast<int>(ciphertext.size())) != 1) {
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        throw std::runtime_error("AES-GCM decryption failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG,
                            static_cast<int>(tag.size()), tag.data()) != 1 ||
        EVP_DecryptFinal_ex(ctx.get(),
                            reinterpret_cast<unsigned char*>(plaintext.data()) + written,
                            &final_written) != 1) {
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        throw std::runtime_error("Secret authentication failed (wrong key, AAD, or modified data)");
    }

    plaintext.resize(static_cast<std::size_t>(written + final_written));
    OPENSSL_cleanse(nonce.data(), nonce.size());
    if (!ciphertext.empty()) OPENSSL_cleanse(ciphertext.data(), ciphertext.size());
    OPENSSL_cleanse(tag.data(), tag.size());
    return plaintext;
}

} // namespace ami::security
