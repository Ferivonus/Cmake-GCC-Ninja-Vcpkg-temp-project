#include <catch2/catch_test_macros.hpp>
#include "services/crypto_service.hpp"
#include <string>
#include <vector>

using namespace crypto;

TEST_CASE("CryptoService - PBKDF2 Anahtar Turetme Dogrulamasi", "[crypto][kdf]")
{
    SECTION("Ayni parola ve tuz ile deterministik 32 baytlik anahtar uretilmeli")
    {
        auto key1 = CryptoService::derive_key("GizliParola2026", "SabitTuz", 10000);
        auto key2 = CryptoService::derive_key("GizliParola2026", "SabitTuz", 10000);

        REQUIRE(key1.size() == 32);
        CHECK(key1 == key2);
    }

    SECTION("Farkli parolalar veya tuzlar farkli anahtarlar uretmeli")
    {
        auto keyA = CryptoService::derive_key("ParolaA", "Tuz", 1000);
        auto keyB = CryptoService::derive_key("ParolaB", "Tuz", 1000);
        auto keyC = CryptoService::derive_key("ParolaA", "FarkliTuz", 1000);

        CHECK(keyA != keyB);
        CHECK(keyA != keyC);
    }
}

TEST_CASE("CryptoService - AES-256-GCM Sifreleme ve Desifreleme", "[crypto][aes]")
{
    auto key = CryptoService::derive_key("TestParolasi");
    REQUIRE(key.size() == 32);

    SECTION("Duz metin sifrelenip basariyla geri cozulmeli (Roundtrip)")
    {
        const std::string original = "Bu bir modern C++26 sifreli chat iletisidir.";
        auto enc = CryptoService::encrypt(original, key);

        REQUIRE(enc.has_value());
        CHECK_FALSE(enc->ciphertext_base64.empty());
        CHECK_FALSE(enc->iv_base64.empty());
        CHECK_FALSE(enc->tag_base64.empty());

        auto dec = CryptoService::decrypt(*enc, key);
        REQUIRE(dec.has_value());
        CHECK(*dec == original);
    }

    SECTION("Ayni metin farkli Nonce/IV nedeniyle her seferinde farkli sifreli cikti uretmeli")
    {
        const std::string text = "Ayni Mesaj";
        auto enc1 = CryptoService::encrypt(text, key);
        auto enc2 = CryptoService::encrypt(text, key);

        REQUIRE(enc1.has_value());
        REQUIRE(enc2.has_value());
        CHECK(enc1->iv_base64 != enc2->iv_base64);
        CHECK(enc1->ciphertext_base64 != enc2->ciphertext_base64);
    }

    SECTION("Sifreli metin manipule edildiginde Auth Tag dogrulamasi basarisiz olmali")
    {
        auto enc = CryptoService::encrypt("Guvenlik Testi", key);
        REQUIRE(enc.has_value());

        // Şifreli metnin ilk karakterini bozarak kurcalıyoruz
        enc->ciphertext_base64[0] = (enc->ciphertext_base64[0] == 'A') ? 'B' : 'A';

        auto dec = CryptoService::decrypt(*enc, key);
        CHECK_FALSE(dec.has_value()); // Manipüle edilmiş veri reddedilmeli!
    }

    SECTION("Yanlis anahtarla desifreleme yapilamamali")
    {
        auto wrong_key = CryptoService::derive_key("YanlisParola");
        auto enc = CryptoService::encrypt("Kritik Bilgi", key);
        REQUIRE(enc.has_value());

        auto dec = CryptoService::decrypt(*enc, wrong_key);
        CHECK_FALSE(dec.has_value());
    }
}

TEST_CASE("CryptoService - Base64 Donusturucu Dogrulamasi", "[crypto][base64]")
{
    SECTION("Standart ve ikili bayt dizileri kayipsiz kodlanip cozulmeli")
    {
        const std::string sample = "Hello C++26 / OpenSSL!";
        std::string encoded = CryptoService::base64_encode(reinterpret_cast<const uint8_t *>(sample.data()), sample.size());
        REQUIRE_FALSE(encoded.empty());

        auto decoded_bytes = CryptoService::base64_decode(encoded);
        std::string decoded_str(decoded_bytes.begin(), decoded_bytes.end());
        CHECK(decoded_str == sample);
    }

    SECTION("Gecersiz Base64 verisi guvenle bos vektor donmeli")
    {
        auto res = CryptoService::base64_decode("=== GECERSIZ BASE64 ===");
        // Hata durumunda program çökmemeli
        CHECK(res.empty());
    }
}