#include "Server.hpp"
#include "RemotePage.h" // page.html und manifest.webmanifest als C-Arrays (xxd -i), siehe Makefile

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define closeSock closesocket
#define pollSock WSAPoll
#define strncasecmp _strnicmp
static void setNonBlocking(sock_t s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
static bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <ifaddrs.h>
#include <strings.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define closeSock ::close
#define pollSock ::poll
static void setNonBlocking(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace offbeat {

namespace {
constexpr int kMaxConnections = 8, kMaxStreams = 4;    // gleichzeitig offen / davon SSE
constexpr size_t kMaxHeader = 8192, kMaxBody = 65536;  // Größenbegrenzung der Anfragen
constexpr size_t kMaxPendingOut = 1 << 20;             // langsamer Client: Verbindung trennen
constexpr int kTickMs = 33;                            // ~30 Hz

#ifdef _WIN32
struct WinsockInit {
    WinsockInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WinsockInit() { WSACleanup(); }
};
void ensureNet() { static WinsockInit init; }
#else
void ensureNet() {}
#endif

void noSigPipe(sock_t s)
{
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
    (void)s;
#endif
}

struct Conn {
    sock_t fd = BAD_SOCK;
    uint32_t peer = 0;
    std::string in, out;
    bool stream = false, closeAfter = false;
    uint32_t sentRevision = 0;
};

std::string response(int code, const char* reason, const char* type, const std::string& body, const char* extra = "")
{
    char head[400];
    std::snprintf(head, sizeof(head),
                  "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n"
                  "X-Content-Type-Options: nosniff\r\n%sConnection: close\r\n\r\n",
                  code, reason, type, body.size(), extra);
    return head + body;
}

std::string queryValue(const std::string& target, const char* key)
{
    const size_t q = target.find('?');
    if (q == std::string::npos)
        return "";
    const std::string k = std::string(key) + "=";
    for (size_t at = q + 1; at < target.size();) {
        const size_t end = std::min(target.find('&', at), target.size());
        if (target.compare(at, k.size(), k) == 0)
            return target.substr(at + k.size(), end - at - k.size());
        at = end + 1;
    }
    return "";
}

std::string headerValue(const std::string& head, const char* name)
{
    const std::string n = std::string("\r\n") + name + ":";
    for (size_t at = 0; (at = head.find("\r\n", at)) != std::string::npos; at += 2) {
        if (strncasecmp(head.c_str() + at, n.c_str(), n.size()) == 0) {
            size_t v = at + n.size();
            while (v < head.size() && head[v] == ' ')
                ++v;
            return head.substr(v, head.find("\r\n", v) - v);
        }
    }
    return "";
}

std::string sseEvent(const char* name, const std::string& data) { return std::string("event: ") + name + "\ndata: " + data + "\n\n"; }
} // namespace

bool Server::isPrivate(uint32_t ip)
{
    return (ip >> 24) == 10 || (ip >> 24) == 127 || (ip >> 20) == ((172u << 4) | 1) || (ip >> 16) == ((192u << 8) | 168);
}

Server::Server(RemoteHost& h)
    : host(h)
{
    thread = std::thread([this] { loop(); });
}

Server::~Server()
{
    quit = true;
    if (thread.joinable())
        thread.join();
}

void Server::loop()
{
    ensureNet();
    sock_t listener = BAD_SOCK;
    std::vector<Conn> conns;
    std::mt19937 rng((uint32_t)std::random_device {}());
    int pin = 0, failures = 0;
    auto blockedUntil = std::chrono::steady_clock::time_point();
    auto nextTick = std::chrono::steady_clock::now();

    auto closeAll = [&] {
        for (Conn& c : conns)
            closeSock(c.fd);
        conns.clear();
        if (listener != BAD_SOCK)
            closeSock(listener);
        listener = BAD_SOCK;
        activePort = 0, clientCount = 0;
    };
    auto open = [&] {
        for (int p = firstPort; p < firstPort + 20 && listener == BAD_SOCK; ++p) { // belegt: nächster Port
            sock_t s = socket(AF_INET, SOCK_STREAM, 0);
            if (s == BAD_SOCK)
                return;
            int one = 1; // Port nach Neustart sofort wieder nutzbar, aber nie doppelt belegt
#ifdef _WIN32
            setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&one, sizeof(one));
#else
            setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
            sockaddr_in a {};
            a.sin_family = AF_INET;
            a.sin_port = htons((uint16_t)p);
            a.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bind(s, (sockaddr*)&a, sizeof(a)) == 0 && listen(s, 8) == 0) {
                setNonBlocking(s);
                listener = s;
                activePort = p;
            } else {
                closeSock(s);
            }
        }
        pin = fixedPin ? fixedPin : 1000 + (int)(rng() % 9000);
        activePin = pin;
        failures = 0;
    };

    // Antwort auf eine vollständige Anfrage; false = Anfrage noch unvollständig
    auto handle = [&](Conn& c) -> bool {
        const size_t headEnd = c.in.find("\r\n\r\n");
        if (headEnd == std::string::npos) {
            if (c.in.size() > kMaxHeader)
                c.out = response(431, "Request Header Fields Too Large", "text/plain", "zu groß\n"), c.closeAfter = true;
            return c.closeAfter;
        }
        const std::string head = c.in.substr(0, headEnd + 2);
        const size_t length = (size_t)std::strtoul(headerValue(head, "Content-Length").c_str(), nullptr, 10);
        if (length > kMaxBody) {
            c.out = response(413, "Payload Too Large", "text/plain", "zu groß\n"), c.closeAfter = true;
            return true;
        }
        if (c.in.size() < headEnd + 4 + length)
            return false;
        const std::string body = c.in.substr(headEnd + 4, length);
        char method[8] = "", target[1024] = "";
        std::sscanf(head.c_str(), "%7s %1023s", method, target);
        const std::string t = target, path = t.substr(0, t.find('?'));
        c.closeAfter = true;

        if (!isPrivate(c.peer)) {
            c.out = response(403, "Forbidden", "text/plain", "nur aus privaten Netzen\n");
            return true;
        }
        if (!std::strcmp(method, "GET") && (path == "/" || path == "/index.html")) {
            c.out = response(200, "OK", "text/html; charset=utf-8", std::string((const char*)page_html, page_html_len));
            return true;
        }
        if (!std::strcmp(method, "GET") && path == "/manifest.webmanifest") {
            c.out = response(200, "OK", "application/manifest+json", std::string((const char*)manifest_webmanifest, manifest_webmanifest_len));
            return true;
        }
        if (path != "/state" && path != "/events" && path != "/cmd" && path != "/live") {
            c.out = response(404, "Not Found", "text/plain", "nicht gefunden\n");
            return true;
        }
        // PIN (Abfrage ?pin= oder Kopfzeile X-Pin); nach vielen Fehlversuchen eine Minute gesperrt
        const auto now = std::chrono::steady_clock::now();
        if (now < blockedUntil) {
            c.out = response(429, "Too Many Requests", "text/plain", "zu viele falsche PINs, eine Minute warten\n");
            return true;
        }
        std::string given = queryValue(t, "pin");
        if (given.empty())
            given = headerValue(head, "X-Pin");
        if (given.empty() || std::atoi(given.c_str()) != pin || given.size() != 4) {
            if (++failures >= 10)
                blockedUntil = now + std::chrono::seconds(60), failures = 0;
            c.out = response(403, "Forbidden", "text/plain", "PIN falsch\n");
            return true;
        }
        if (!std::strcmp(method, "GET") && path == "/live") { // Laufzustand (Pattern, Play) für Offbeat Clutch
            c.out = response(200, "OK", "application/json", host.remoteLive());
            return true;
        }
        if (!std::strcmp(method, "GET") && path == "/state") {
            c.out = response(200, "OK", "application/json", host.remoteState());
            return true;
        }
        if (!std::strcmp(method, "POST") && path == "/cmd") {
            std::string err;
            c.out = host.remoteCommand(body, err) ? response(204, "No Content", "text/plain", "")
                                                   : response(400, "Bad Request", "text/plain", err + "\n");
            return true;
        }
        if (!std::strcmp(method, "GET") && path == "/events") {
            int streams = 0;
            for (const Conn& o : conns)
                streams += o.stream;
            if (streams >= kMaxStreams) {
                c.out = response(503, "Service Unavailable", "text/plain", "zu viele Geräte verbunden\n");
                return true;
            }
            c.stream = true, c.closeAfter = false;
            c.sentRevision = host.remoteRevision();
            c.out = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\nConnection: keep-alive\r\n\r\n"
                    "retry: 1000\n\n" + sseEvent("state", host.remoteState()) + sseEvent("live", host.remoteLive());
            return true;
        }
        c.out = response(405, "Method Not Allowed", "text/plain", "nicht erlaubt\n");
        return true;
    };

    while (!quit) {
        if (enabled && listener == BAD_SOCK)
            open();
        else if (!enabled && listener != BAD_SOCK)
            closeAll();

        std::vector<pollfd> fds;
        if (listener != BAD_SOCK)
            fds.push_back({ listener, POLLIN, 0 });
        for (const Conn& c : conns)
            fds.push_back({ c.fd, (short)(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0 });
        const int wait = (int)std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(nextTick - std::chrono::steady_clock::now()).count());
        if (fds.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(wait));
        else
            pollSock(fds.data(), (unsigned long)fds.size(), wait);

        size_t fi = 0;
        if (listener != BAD_SOCK) {
            if (fds[fi++].revents & POLLIN)
                for (;;) {
                    sockaddr_in a {};
                    socklen_t len = sizeof(a);
                    sock_t s = accept(listener, (sockaddr*)&a, &len);
                    if (s == BAD_SOCK)
                        break;
                    if ((int)conns.size() >= kMaxConnections) {
                        closeSock(s);
                        continue;
                    }
                    setNonBlocking(s);
                    noSigPipe(s);
                    int one = 1;
                    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
                    Conn c;
                    c.fd = s;
                    c.peer = fakePeer ? fakePeer : ntohl(a.sin_addr.s_addr);
                    conns.push_back(c);
                }
        }
        for (size_t i = 0; i < conns.size() && fi < fds.size(); ++i, ++fi) {
            Conn& c = conns[i];
            if (fds[fi].fd != c.fd)
                break; // neue Verbindung aus diesem Durchlauf: nächstes Mal
            if (fds[fi].revents & (POLLIN | POLLHUP | POLLERR)) {
                char buf[4096];
                const int n = (int)recv(c.fd, buf, sizeof(buf), 0);
                if (n <= 0 && !(n < 0 && wouldBlock()))
                    c.closeAfter = true, c.out.clear(), c.stream = false;
                else if (n > 0 && !c.stream) {
                    c.in.append(buf, (size_t)n);
                    handle(c);
                }
            }
        }
        // senden
        for (Conn& c : conns)
            while (!c.out.empty()) {
                const int n = (int)send(c.fd, c.out.data(), (int)std::min<size_t>(c.out.size(), 65536), MSG_NOSIGNAL);
                if (n > 0)
                    c.out.erase(0, (size_t)n);
                else {
                    if (!(n < 0 && wouldBlock()))
                        c.out.clear(), c.closeAfter = true, c.stream = false;
                    break;
                }
            }
        // fertige schließen
        for (size_t i = 0; i < conns.size();)
            if ((conns[i].closeAfter && conns[i].out.empty() && !conns[i].stream) || conns[i].out.size() > kMaxPendingOut) {
                closeSock(conns[i].fd);
                conns.erase(conns.begin() + (long)i);
            } else
                ++i;

        if (std::chrono::steady_clock::now() >= nextTick) { // Takt: live/state an verbundene Geräte
            nextTick = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTickMs);
            host.remoteIdle();
            int streams = 0;
            std::string live, state;
            const uint32_t rev = host.remoteRevision();
            for (Conn& c : conns)
                if (c.stream) {
                    ++streams;
                    if (c.sentRevision != rev) {
                        if (state.empty())
                            state = host.remoteState();
                        c.out += sseEvent("state", state);
                        c.sentRevision = rev;
                    }
                    if (live.empty())
                        live = host.remoteLive();
                    c.out += sseEvent("live", live);
                }
            clientCount = streams;
        }
    }
    closeAll();
}

std::string localAddress()
{
    ensureNet();
    std::string best;
#ifdef _WIN32
    char name[256];
    addrinfo hints {}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (gethostname(name, sizeof(name)) == 0 && getaddrinfo(name, nullptr, &hints, &res) == 0) {
        for (addrinfo* r = res; r; r = r->ai_next) {
            const uint32_t ip = ntohl(((sockaddr_in*)r->ai_addr)->sin_addr.s_addr);
            if (Server::isPrivate(ip) && (ip >> 24) != 127) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
                best = buf;
                break;
            }
        }
        freeaddrinfo(res);
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* i = list; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET)
                continue;
            const uint32_t ip = ntohl(((sockaddr_in*)i->ifa_addr)->sin_addr.s_addr);
            if (Server::isPrivate(ip) && (ip >> 24) != 127) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
                best = buf;
                if (!std::strncmp(i->ifa_name, "en", 2)) // WLAN/Ethernet bevorzugen
                    break;
            }
        }
        freeifaddrs(list);
    }
#endif
    return best;
}

bool localGet(int port, const std::string& path, std::string& body, int timeoutMs)
{
    ensureNet();
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == BAD_SOCK)
        return false;
    noSigPipe(s);
#ifdef _WIN32
    DWORD tv = (DWORD)timeoutMs;
#else
    timeval tv { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
#endif
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
    sockaddr_in a {};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::string resp;
    if (connect(s, (sockaddr*)&a, sizeof(a)) == 0) {
        const std::string req = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
        if (send(s, req.data(), (int)req.size(), MSG_NOSIGNAL) == (int)req.size()) {
            char buf[8192];
            for (int n; (n = (int)recv(s, buf, sizeof(buf), 0)) > 0 && resp.size() < (4u << 20);)
                resp.append(buf, (size_t)n);
        }
    }
    closeSock(s);
    const size_t headEnd = resp.find("\r\n\r\n");
    if (resp.compare(0, 12, "HTTP/1.1 200") != 0 || headEnd == std::string::npos)
        return false;
    body = resp.substr(headEnd + 4);
    return true;
}

} // namespace offbeat
