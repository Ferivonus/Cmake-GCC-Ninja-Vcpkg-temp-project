#include "config/app_config.hpp"
#include "backend/backend.hpp"
#include "client/ws_client.hpp"
#include <iostream>
#include <string>
#include <vector>

void print_usage(const char *program_name)
{
    std::cout << "\nKullanim Parametreleri:\n"
              << "  1) Sunucu Modu:\n"
              << "     " << program_name << " server [port] [ip]\n"
              << "     Ornek: " << program_name << " server 8080 0.0.0.0\n\n"
              << "  2) Istemci Modu:\n"
              << "     " << program_name << " client [kullanici_adi] [hedef_ip/domain] [hedef_port] [secenekler]\n\n"
              << "     Secenekler:\n"
              << "       --tor                  Baglantiyi Tor SOCKS5 uzerinden gecmeye zorlar\n"
              << "       --proxy-host <ip>      Tor vekil host adresi (Varsayilan: 127.0.0.1)\n"
              << "       --proxy-port <port>    Tor vekil portu (Varsayilan: 9050, Tor Browser: 9150)\n\n"
              << "     Dogrudan Ikili Calistirma Ornekleri:\n"
              << "       Dogrudan Baglanti    : " << program_name << " client Ahmet 127.0.0.1 8080\n"
              << "       Tor Servisi (9050)   : " << program_name << " client Ahmet 127.0.0.1 8080 --tor\n"
              << "       Tor Browser (9150)   : " << program_name << " client Ahmet ex4mp1e...onion 8080 --proxy-port 9150\n"
              << "       Uzak Tor Proxy       : " << program_name << " client Ahmet ex4mp1e...onion 8080 --proxy-host 192.168.1.10 --proxy-port 9050\n\n"
              << "  3) PowerShell Otomasyon Betigi (build.ps1) ile Calistirma:\n"
              << "     Derleme, test ve calistirma sureclerini yonetmek icin 'build.ps1' kullanabilirsiniz.\n"
              << "     -Server / -Client kullanildiginda, exe zaten build edilmisse OTOMATIK olarak\n"
              << "     yeniden build alinmaz (calisan bir sunucuyu etkilemez), sadece calistirilir.\n"
              << "     Build henuz alinmamissa ise otomatik olarak once build alinir.\n\n"
              << "       Sunucu Baslatma          : .\\build.ps1 -Server\n"
              << "       Ozel Port Sunucu         : .\\build.ps1 -Server -Port 8080\n"
              << "       Istemci Baglantisi       : .\\build.ps1 -Client Ahmet -Target 127.0.0.1\n"
              << "       Onion Adresine Baglanma  : .\\build.ps1 -Client Ahmet -Target ex4mp1e...onion -ProxyPort 9150\n"
              << "       Tor Istemcisi (Duz IP)   : .\\build.ps1 -Client Anonim -Target 1.2.3.4 -Tor -ProxyPort 9150\n"
              << "       Release Modu             : .\\build.ps1 release -Server\n"
              << "       Temiz Derleme (Clean)    : .\\build.ps1 -Clean -Server\n"
              << "       Zorla Yeniden Build      : .\\build.ps1 -Rebuild -Server\n"
              << "       Build'e Hic Dokunma      : .\\build.ps1 -SkipBuild -Server\n\n";
}

static bool parse_port(const std::string &str, unsigned short &out_port)
{
    try
    {
        size_t idx = 0;
        int val = std::stoi(str, &idx);
        if (idx != str.size() || val <= 0 || val > 65535)
        {
            return false;
        }
        out_port = static_cast<unsigned short>(val);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

int main(int argc, char *argv[])
{
    AppLog::init();

    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "--help" || mode == "-h")
    {
        print_usage(argv[0]);
        return 0;
    }

    if (mode == "server")
    {
        unsigned short port = 8080;
        std::string ip = "0.0.0.0";

        if (argc >= 3 && !parse_port(argv[2], port))
        {
            AppLog::error("Gecersiz sunucu portu: " + std::string(argv[2]));
            return 1;
        }
        if (argc >= 4)
        {
            ip = argv[3];
        }

        AppLog::info("Sunucu modu baslatiliyor...");
        backend::CloudServer server(ip, port);
        server.run();
    }
    else if (mode == "client")
    {
        std::string username = "Anonim";
        std::string host = "127.0.0.1";
        unsigned short port = 8080;

        bool use_tor = false;
        std::string proxy_host = "127.0.0.1";
        unsigned short proxy_port = 9050;

        std::vector<std::string> positional_args;

        for (int i = 2; i < argc; ++i)
        {
            std::string arg = argv[i];

            if (arg == "--tor")
            {
                use_tor = true;
            }
            else if (arg == "--proxy-host")
            {
                if (i + 1 >= argc)
                {
                    AppLog::error("--proxy-host parametresi bir IP veya domain degeri gerektirir.");
                    return 1;
                }
                proxy_host = argv[++i];
                use_tor = true;
            }
            else if (arg.starts_with("--proxy-host="))
            {
                proxy_host = arg.substr(13);
                use_tor = true;
            }
            else if (arg == "--proxy-port")
            {
                if (i + 1 >= argc)
                {
                    AppLog::error("--proxy-port parametresi bir port degeri gerektirir.");
                    return 1;
                }
                if (!parse_port(argv[++i], proxy_port))
                {
                    AppLog::error("Gecersiz proxy portu: " + std::string(argv[i]));
                    return 1;
                }
                use_tor = true;
            }
            else if (arg.starts_with("--proxy-port="))
            {
                if (!parse_port(arg.substr(13), proxy_port))
                {
                    AppLog::error("Gecersiz proxy portu: " + arg.substr(13));
                    return 1;
                }
                use_tor = true;
            }
            else if (!arg.starts_with("--"))
            {
                positional_args.push_back(arg);
            }
            else
            {
                AppLog::error("Bilinmeyen secenek: " + arg);
                print_usage(argv[0]);
                return 1;
            }
        }

        if (positional_args.size() >= 1)
        {
            username = positional_args[0];
        }
        if (positional_args.size() >= 2)
        {
            host = positional_args[1];
        }
        if (positional_args.size() >= 3 && !parse_port(positional_args[2], port))
        {
            AppLog::error("Gecersiz hedef port: " + positional_args[2]);
            return 1;
        }

        if (host.find(".onion") != std::string::npos)
        {
            use_tor = true;
        }

        if (use_tor)
        {
            AppLog::info("Istemci modu baslatiliyor (Tor Rotasi Aktif -> Vekil: " +
                         proxy_host + ":" + std::to_string(proxy_port) + ")...");
        }
        else
        {
            AppLog::info("Istemci modu baslatiliyor (Dogrudan Baglanti)...");
        }

        client::WsClient client(host, port, username, use_tor, proxy_host, proxy_port);
        client.run_interactive();
    }
    else
    {
        AppLog::error("Gecersiz mod secimi: " + mode);
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}