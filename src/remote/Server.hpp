// Gearbox Remote: selbst geschriebener Mini-Server für die iPad-Fernbedienung (HTTP/1.1, Server-Sent Events
// Server → iPad, POST iPad → Server). Läuft in einem eigenen Thread, nie im Audio-Thread. Nur private Netze
// (10/8, 172.16/12, 192.168/16, 127/8), PIN nötig, wenige Clients, begrenzte Anfragegröße, liefert nur die
// eingebaute Seite aus (kein Dateizugriff). Endpunkte: GET / · GET /manifest.webmanifest · GET /state ·
// GET /events (SSE: „state“ bei Änderung, „live“ ~30×/s) · POST /cmd (Befehle aus Model.hpp, eine Zeile je Befehl).
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace offbeat {

// Wird aus dem Server-Thread aufgerufen; die Hülle muss das selbst absichern (Mutex, lock-frei zum DSP).
class RemoteHost {
public:
    virtual ~RemoteHost() = default;
    virtual std::string remoteState() = 0; // Set als JSON
    virtual std::string remoteLive() = 0;  // {"playing":…,"steps":[…],…}
    virtual uint32_t remoteRevision() = 0; // ändert sich bei jeder Raster-Änderung
    virtual bool remoteCommand(const std::string& lines, std::string& err) = 0;
    virtual void remoteIdle() {} // ~30×/s, auch wenn der Server aus ist (z. B. altes Raster freigeben)
};

class Server {
public:
    explicit Server(RemoteHost& host);
    ~Server(); // stoppt Thread und Sockets

    void setEnabled(bool on) { enabled = on; } // aus jedem Thread
    int port() const { return activePort; }    // 0 = aus
    int pin() const { return activePin; }
    int clients() const { return clientCount; }

    // Für Tests (gearbox-render --serve)
    void setFirstPort(int p) { firstPort = p; }
    void setFixedPin(int p) { fixedPin = p; }
    void setFakePeer(uint32_t ipv4) { fakePeer = ipv4; } // tut so, als kämen alle Anfragen von dieser Adresse

    static bool isPrivate(uint32_t ipv4); // Host-Byte-Reihenfolge

private:
    void loop();

    RemoteHost& host;
    std::thread thread;
    std::atomic<bool> quit { false }, enabled { false };
    std::atomic<int> activePort { 0 }, activePin { 0 }, clientCount { 0 };
    int firstPort = 8080, fixedPin = 0;
    uint32_t fakePeer = 0;
};

// Erste private IPv4-Adresse dieses Rechners (für die Anzeige http://<IP>:<Port>), leer wenn keine
std::string localAddress();

// Kleiner HTTP-GET an 127.0.0.1 (für die Plugin-Oberfläche: Zustand nach Änderungen vom iPad holen).
bool localGet(int port, const std::string& path, std::string& body, int timeoutMs = 300);

} // namespace offbeat
