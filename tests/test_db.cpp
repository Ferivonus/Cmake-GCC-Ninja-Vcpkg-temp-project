#include <catch2/catch_test_macros.hpp>
#include "services/db_service.hpp"

using namespace backend;

TEST_CASE("DbService SQLite CRUD, Iliski ve Guvenlik Testleri", "[db][sqlite]")
{
    SECTION("Bellek ici (:memory:) veritabani basariyla ilklendirilmeli ve ping yaniti vermeli")
    {
        DbService db;
        REQUIRE(db.init(":memory:"));
        CHECK(db.ping());
    }

    SECTION("Oda CRUD operasyonlari eksiksiz calismali")
    {
        DbService db;
        REQUIRE(db.init(":memory:"));

        const std::string ts = "2026-09-19 12:00:00";

        // 1. Oda Olusturma
        int64_t room_id = db.create_room("Genel Sohbet", ts);
        REQUIRE(room_id > 0);

        // Ayni isimle ikinci oda acilamamali (UNIQUE kisiti)
        int64_t dup_id = db.create_room("Genel Sohbet", ts);
        CHECK(dup_id == -1);

        // 2. Oda Bilgisi Getirme
        auto room = db.get_room(room_id);
        REQUIRE(room.has_value());
        CHECK(room->name == "Genel Sohbet");
        CHECK(room->is_open == true);
        CHECK(db.is_room_open(room_id) == true);

        // 3. Oda Guncelleme (Kapatma ve isim degistirme)
        bool updated = db.update_room(room_id, "Arsiv Odasi", false, "2026-09-19 12:05:00");
        CHECK(updated);
        CHECK(db.is_room_open(room_id) == false);

        auto updated_room = db.get_room(room_id);
        REQUIRE(updated_room.has_value());
        CHECK(updated_room->name == "Arsiv Odasi");
        CHECK(updated_room->is_open == false);

        // 4. Oda Listeleme
        db.create_room("Ikinci Oda", ts);
        auto all_rooms = db.get_rooms();
        CHECK(all_rooms.size() == 2);

        // 5. Oda Silme
        CHECK(db.delete_room(room_id));
        CHECK_FALSE(db.get_room(room_id).has_value());
        CHECK(db.get_rooms().size() == 1);
    }

    SECTION("Mesaj kaydi ve Foreign Key kisiti dogrulanmali")
    {
        DbService db;
        REQUIRE(db.init(":memory:"));

        const std::string ts = "2026-09-19 12:10:00";
        int64_t room_id = db.create_room("Yazilim Odasi", ts);
        REQUIRE(room_id > 0);

        // Gecerli odaya mesaj ekleme
        int64_t id1 = db.save_message(room_id, "Ahmet", "Ilk mesaj", ts);
        CHECK(id1 == 1);

        int64_t id2 = db.save_message(room_id, "Mehmet", "Ikinci mesaj", "2026-09-19 12:10:05");
        CHECK(id2 == 2);

        auto messages = db.get_room_messages(room_id);
        REQUIRE(messages.size() == 2);
        CHECK(messages[0].username == "Ahmet");
        CHECK(messages[1].username == "Mehmet");

        // Gecersiz (var olmayan) bir odaya mesaj ekleme -> Foreign Key hatasi ile -1 donmeli
        int64_t invalid_room_msg = db.save_message(9999, "Hacker", "Yetkisiz mesaj", ts);
        CHECK(invalid_room_msg == -1);
    }

    SECTION("ON DELETE CASCADE kurali ile oda silindiginde mesajlar da silinmeli")
    {
        DbService db;
        REQUIRE(db.init(":memory:"));

        int64_t room_id = db.create_room("Gecici Oda", "2026-09-19 12:15:00");
        REQUIRE(room_id > 0);

        db.save_message(room_id, "Ali", "Test mesaji 1", "2026-09-19 12:15:01");
        db.save_message(room_id, "Veli", "Test mesaji 2", "2026-09-19 12:15:02");
        REQUIRE(db.get_room_messages(room_id).size() == 2);

        // Oda silindiginde bagli mesajlar da otomatik silinmeli
        REQUIRE(db.delete_room(room_id));
        CHECK(db.get_room_messages(room_id).empty());
    }

    SECTION("SQL Enjeksiyonu girisimleri parametrik sorgu ile bertaraf edilmeli")
    {
        DbService db;
        REQUIRE(db.init(":memory:"));

        int64_t room_id = db.create_room("Guvenlik Odasi", "2026-09-19 12:20:00");
        REQUIRE(room_id > 0);

        std::string malicious_user = "hacker'; DROP TABLE chat_messages; --";
        std::string malicious_msg = "' OR '1'='1";

        int64_t id = db.save_message(room_id, malicious_user, malicious_msg, "2026-09-19 12:20:01");
        CHECK(id > 0);

        int64_t next_id = db.save_message(room_id, "NormalUser", "Sistem calisiyor", "2026-09-19 12:20:02");
        CHECK(next_id == id + 1);

        auto messages = db.get_room_messages(room_id);
        REQUIRE(messages.size() == 2);
        CHECK(messages[0].username == malicious_user);
    }

    SECTION("Turkce ve ozel karakterler bozulmadan saklanabilmeli")
    {
        DbService db;
        REQUIRE(db.init(":memory:"));

        int64_t room_id = db.create_room("Türkçe Karakter Odası ĞÜŞİÖÇ", "2026-09-19 12:25:00");
        REQUIRE(room_id > 0);

        std::string tr_user = "Çağla Şimşek";
        std::string tr_msg = "Ğüşıöç İÜÖŞÇ / Özel Karakterler: %&'\"<>";

        int64_t id = db.save_message(room_id, tr_user, tr_msg, "2026-09-19 12:25:01");
        CHECK(id > 0);

        auto messages = db.get_room_messages(room_id);
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].username == tr_user);
        CHECK(messages[0].message == tr_msg);
    }

    SECTION("Ilklendirilmemis DB servisi guvenli sekilde hata donmeli")
    {
        DbService uninit_db;
        CHECK(uninit_db.ping() == false);
        CHECK(uninit_db.create_room("Test", "2026-09-19 12:00:00") == -1);
        CHECK(uninit_db.save_message(1, "User", "Test", "2026-09-19 12:00:00") == -1);
        CHECK(uninit_db.get_rooms().empty());
    }
}