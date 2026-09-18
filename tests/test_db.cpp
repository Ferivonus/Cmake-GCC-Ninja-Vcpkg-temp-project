#include <catch2/catch_test_macros.hpp>
#include "services/db_service.hpp"

TEST_CASE("DbService SQLite CRUD ve Guvenlik Testleri", "[db][sqlite]")
{
    SECTION("Bellek ici (:memory:) veritabani basariyla ilklendirilmeli")
    {
        DbService db;
        REQUIRE(db.init(":memory:"));
    }

    SECTION("Mesaj kaydi basarili olmali ve sirali ID donmeli")
    {
        DbService db;
        REQUIRE(db.init(":memory:"));

        int64_t id1 = db.save_message("Ahmet", "Ilk mesaj", "2026-09-18 10:00:00");
        CHECK(id1 == 1);

        int64_t id2 = db.save_message("Mehmet", "Ikinci mesaj", "2026-09-18 10:00:05");
        CHECK(id2 == 2);
    }

    SECTION("SQL Enjeksiyonu (SQL Injection) girisimleri parametrik sorgu ile bertaraf edilmeli")
    {
        DbService db;
        REQUIRE(db.init(":memory:"));

        std::string malicious_user = "hacker'; DROP TABLE chat_messages; --";
        std::string malicious_msg = "' OR '1'='1";

        int64_t id = db.save_message(malicious_user, malicious_msg, "2026-09-18 10:01:00");
        CHECK(id > 0);

        int64_t next_id = db.save_message("NormalUser", "Sistem ayakta", "2026-09-18 10:01:10");
        CHECK(next_id == id + 1);
    }

    SECTION("Turkce ve ozel karakterler bozulmadan saklanabilmeli")
    {
        DbService db;
        REQUIRE(db.init(":memory:"));

        std::string tr_user = "Çağla Şimşek";
        std::string tr_msg = "Ğüşıöç İÜÖŞÇ / Özel Karakterler: %&'\"<>";

        int64_t id = db.save_message(tr_user, tr_msg, "2026-09-18 10:02:00");
        CHECK(id > 0);
    }

    SECTION("Ilklendirilmemis (uninitialized) DB servisi guvenli sekilde -1 donmeli")
    {
        DbService uninit_db;
        int64_t id = uninit_db.save_message("User", "Test", "2026-09-18 10:00:00");
        CHECK(id == -1);
    }
}