#include "services/crypto_service.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <cstring>
#include <memory>

namespace crypto
{
    std::string CryptoService::base64_encode(const uint8_t *data, size_t len)
    {
        BIO *bio = BIO_new(BIO_s_mem());
        BIO *b64 = BIO_new(BIO_f_base64());
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
        bio = BIO_push(b64, bio);

        BIO_write(bio, data, static_cast<int>(len));
        BIO_flush(bio);

        BUF_MEM *buffer_ptr = nullptr;
        BIO_get_mem_ptr(bio, &buffer_ptr);
        std::string result(buffer_ptr->data, buffer_ptr->length);

        BIO_free_all(bio);
        return result;
    }

    std::vector<uint8_t> CryptoService::base64_decode(const std::string &encoded)
    {
        BIO *bio = BIO_new_mem_buf(encoded.data(), static_cast<int>(encoded.size()));
        BIO *b64 = BIO_new(BIO_f_base64());
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
        bio = BIO_push(b64, bio);

        std::vector<uint8_t> buffer(encoded.size());
        int decoded_len = BIO_read(bio, buffer.data(), static_cast<int>(buffer.size()));
        BIO_free_all(bio);

        if (decoded_len < 0)
            return {};

        buffer.resize(static_cast<size_t>(decoded_len));
        return buffer;
    }

    // PBKDF2 ile anahtar türetimi (Kaba kuvvet saldırılarına karşı korumalı)
    std::vector<uint8_t> CryptoService::derive_key(const std::string &passphrase,
                                                   const std::string &salt,
                                                   int iterations)
    {
        std::vector<uint8_t> key(32); // 32 bayt = 256 bit AES anahtarı
        int result = PKCS5_PBKDF2_HMAC(
            passphrase.c_str(), static_cast<int>(passphrase.size()),
            reinterpret_cast<const unsigned char *>(salt.c_str()), static_cast<int>(salt.size()),
            iterations,
            EVP_sha256(),
            static_cast<int>(key.size()),
            key.data());

        if (result != 1)
        {
            return {};
        }
        return key;
    }

    std::optional<EncryptedPayload> CryptoService::encrypt(const std::string &plaintext, const std::vector<uint8_t> &key)
    {
        if (key.size() != 32)
            return std::nullopt;

        uint8_t iv[12];
        if (RAND_bytes(iv, sizeof(iv)) != 1)
            return std::nullopt;

        std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
        if (!ctx)
            return std::nullopt;

        if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
            return std::nullopt;

        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, sizeof(iv), nullptr) != 1)
            return std::nullopt;

        if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv) != 1)
            return std::nullopt;

        std::vector<uint8_t> ciphertext(plaintext.size() + 16);
        int out_len = 0;

        if (EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &out_len,
                              reinterpret_cast<const unsigned char *>(plaintext.data()), static_cast<int>(plaintext.size())) != 1)
            return std::nullopt;

        int total_len = out_len;

        if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + total_len, &out_len) != 1)
            return std::nullopt;
        total_len += out_len;
        ciphertext.resize(total_len);

        uint8_t tag[16];
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, sizeof(tag), tag) != 1)
            return std::nullopt;

        EncryptedPayload payload;
        payload.ciphertext_base64 = base64_encode(ciphertext.data(), ciphertext.size());
        payload.iv_base64 = base64_encode(iv, sizeof(iv));
        payload.tag_base64 = base64_encode(tag, sizeof(tag));

        return payload;
    }

    std::optional<std::string> CryptoService::decrypt(const EncryptedPayload &payload, const std::vector<uint8_t> &key)
    {
        if (key.size() != 32)
            return std::nullopt;

        auto ciphertext = base64_decode(payload.ciphertext_base64);
        auto iv = base64_decode(payload.iv_base64);
        auto tag = base64_decode(payload.tag_base64);

        if (iv.size() != 12 || tag.size() != 16)
            return std::nullopt;

        std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
        if (!ctx)
            return std::nullopt;

        if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
            return std::nullopt;

        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) != 1)
            return std::nullopt;

        if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data()) != 1)
            return std::nullopt;

        std::vector<uint8_t> plaintext(ciphertext.size());
        int out_len = 0;

        if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &out_len, ciphertext.data(), static_cast<int>(ciphertext.size())) != 1)
            return std::nullopt;

        int total_len = out_len;

        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()), tag.data()) != 1)
            return std::nullopt;

        if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + total_len, &out_len) <= 0)
        {
            return std::nullopt; // Şifre çözülemedi veya kurcalandı
        }
        total_len += out_len;
        plaintext.resize(total_len);

        return std::string(plaintext.begin(), plaintext.end());
    }
}