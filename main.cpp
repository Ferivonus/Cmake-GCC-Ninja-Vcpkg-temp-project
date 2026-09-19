#include "config/app_config.hpp"
#include "backend/backend.hpp"
#include "client/ws_client.hpp"
#include <CLI/CLI.hpp>
#include <iostream>
#include <string>
#include <cstdint>

int main(int argc, char *argv[])
{
    AppLog::init();

    CLI::App app{"Modern C++26 Cloud & Chat Sistemi"};
    app.require_subcommand(1);
    app.footer(
        "PowerShell Otomasyon Betigi (build.ps1) ile Calistirma:\n"
        "  -Server / -Client kullanildiginda, exe zaten build edilmisse OTOMATIK olarak\n"
        "  yeniden build alinmaz (calisan bir sunucuyu etkilemez), sadece calistirilir.\n"
        "  Build henuz alinmamissa ise otomatik olarak once build alinir.\n\n"
        "    Sunucu Baslatma          : .\\build.ps1 -Server\n"
        "    Ozel Port Sunucu         : .\\build.ps1 -Server -Port 8080\n"
        "    Istemci Baglantisi       : .\\build.ps1 -Client Ahmet -Target 127.0.0.1\n"
        "    Onion Adresine Baglanma  : .\\build.ps1 -Client Ahmet -Target ex4mp1e...onion -ProxyPort 9150\n"
        "    Tor Istemcisi (Duz IP)   : .\\build.ps1 -Client Anonim -Target 1.2.3.4 -Tor -ProxyPort 9150\n"
        "    Release Modu             : .\\build.ps1 release -Server\n"
        "    Temiz Derleme (Clean)    : .\\build.ps1 -Clean -Server\n"
        "    Zorla Yeniden Build      : .\\build.ps1 -Rebuild -Server\n"
        "    Build'e Hic Dokunma      : .\\build.ps1 -SkipBuild -Server\n");

    unsigned short server_port = 8080;
    std::string server_ip = "127.0.0.1";

    CLI::App *server_cmd = app.add_subcommand("server", "Sunucu modunda baslatir (HTTP API + WebSocket).");
    server_cmd->add_option("port", server_port, "Dinlenecek port (Varsayilan: 8080)")
        ->check(CLI::Range(1, 65535));
    server_cmd->add_option("ip", server_ip, "Dinlenecek IP adresi (Varsayilan: 127.0.0.1)");
    server_cmd->footer("Ornek: server 8080 127.0.0.1");

    std::string client_username = "Anonim";
    std::string client_host = "127.0.0.1";
    unsigned short client_port = 8080;
    int64_t client_room_id = 1;
    bool use_tor = false;
    std::string proxy_host = "127.0.0.1";
    unsigned short proxy_port = 9050;

    CLI::App *client_cmd = app.add_subcommand("client", "Istemci modunda interaktif sohbet baslatir.");
    client_cmd->add_option("kullanici_adi", client_username, "Kullanici adi (Varsayilan: Anonim)");
    client_cmd->add_option("hedef_ip_domain", client_host, "Hedef IP/domain/.onion adresi (Varsayilan: 127.0.0.1)");
    client_cmd->add_option("hedef_port", client_port, "Hedef port (Varsayilan: 8080)")
        ->check(CLI::Range(1, 65535));
    client_cmd->add_option("-r,--room", client_room_id, "Baglanilacak baslangic oda ID (Varsayilan: 1)")
        ->check(CLI::PositiveNumber);
    client_cmd->add_flag("--tor", use_tor, "Baglantiyi Tor SOCKS5 uzerinden gecmeye zorlar");
    client_cmd->add_option("--proxy-host", proxy_host, "Tor vekil host adresi (Varsayilan: 127.0.0.1)");
    client_cmd->add_option("--proxy-port", proxy_port, "Tor vekil portu (Varsayilan: 9050, Tor Browser: 9150)")
        ->check(CLI::Range(1, 65535));
    client_cmd->footer(
        "Dogrudan Ikili Calistirma Ornekleri:\n"
        "  Dogrudan Baglanti    : client Ahmet 127.0.0.1 8080 -r 1\n"
        "  Tor Servisi (9050)   : client Ahmet 127.0.0.1 8080 --tor -r 2\n"
        "  Tor Browser (9150)   : client Ahmet ex4mp1e...onion 8080 --proxy-port 9150\n"
        "  Uzak Tor Proxy       : client Ahmet ex4mp1e...onion 8080 --proxy-host 192.168.1.10 --proxy-port 9050");

    CLI11_PARSE(app, argc, argv);

    if (server_cmd->parsed())
    {
        AppLog::info("Sunucu modu baslatiliyor...");
        backend::CloudServer server(server_ip, server_port);
        server.run();
    }
    else if (client_cmd->parsed())
    {
        const bool is_onion = client_host.find(".onion") != std::string::npos;
        const bool proxy_explicit = client_cmd->count("--proxy-host") > 0 || client_cmd->count("--proxy-port") > 0;
        if (is_onion || proxy_explicit)
        {
            use_tor = true;
        }

        if (use_tor)
        {
            AppLog::info("Istemci baslatiliyor (Tor Aktif -> Vekil: " +
                         proxy_host + ":" + std::to_string(proxy_port) + " | Hedef Oda: #" +
                         std::to_string(client_room_id) + ")...");
        }
        else
        {
            AppLog::info("Istemci baslatiliyor (Dogrudan Baglanti -> Hedef Oda: #" +
                         std::to_string(client_room_id) + ")...");
        }

        client::WsClient client(client_host, client_port, client_username, client_room_id, use_tor, proxy_host, proxy_port);
        client.run_interactive();
    }

    return 0;
}