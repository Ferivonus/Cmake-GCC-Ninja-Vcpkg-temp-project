#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace crypto
{
    struct EncryptedPayload
    {
        std::string ciphertext_base64;
        std::string iv_base64;
        std::string tag_base64;
    };

    class CryptoService
    {
    public:
        CryptoService() = default;

        // PBKDF2 (HMAC-SHA256) ile güvenli 32 baytlık (256-bit) AES anahtarı türetir
        static std::vector<uint8_t> derive_key(const std::string &passphrase,
                                               const std::string &salt = "ModernAppChatSalt_2026",
                                               int iterations = 100000);

        // Düz metni AES-256-GCM ile şifreler
        static std::optional<EncryptedPayload> encrypt(const std::string &plaintext, const std::vector<uint8_t> &key);

        // Şifreli metni çözer ve doğrular
        static std::optional<std::string> decrypt(const EncryptedPayload &payload, const std::vector<uint8_t> &key);

        // Base64 Yardımcıları
        static std::string base64_encode(const uint8_t *data, size_t len);
        static std::vector<uint8_t> base64_decode(const std::string &encoded);
    };
}