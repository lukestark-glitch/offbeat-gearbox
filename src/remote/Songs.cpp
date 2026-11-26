#include "Songs.hpp"
#include "Json.hpp"
#include "../link/OffbeatLink.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

namespace offbeat {

namespace {
void makeDir(const std::string& d)
{
#ifdef _WIN32
    _mkdir(d.c_str());
#else
    mkdir(d.c_str(), 0755);
#endif
}
} // namespace

std::string songDir()
{
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (const char* over = std::getenv("GEARBOX_SONG_DIR")) { // Tests
        makeDir(over);
        return over;
    }
    std::string d = std::string(home ? home : ".") + "/Music";
    makeDir(d);
    d += "/Offbeat Gearbox";
    makeDir(d);
    return d;
}

bool validSongName(const std::string& name)
{
    if (name.empty() || name.size() > 48 || name[0] == '.' || name[0] == ' ')
        return false;
    for (unsigned char c : name)
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    return true;
}

std::vector<std::string> listSongs()
{
    std::vector<std::string> out;
    if (DIR* d = opendir(songDir().c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0 && validSongName(n.substr(0, n.size() - 5)))
                out.push_back(n.substr(0, n.size() - 5));
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool saveSong(const std::string& name, const std::string& json, std::string& err)
{
    if (!validSongName(name)) {
        err = "ungültiger Song-Name";
        return false;
    }
    const std::string path = songDir() + "/" + name + ".json", tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f || std::fwrite(json.data(), 1, json.size(), f) != json.size()) {
        if (f)
            std::fclose(f);
        err = "Song konnte nicht gespeichert werden";
        return false;
    }
    std::fclose(f);
    std::remove(path.c_str()); // Windows: rename überschreibt nicht
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        err = "Song konnte nicht gespeichert werden";
        return false;
    }
    return true;
}

bool loadSong(const std::string& name, std::string& json, std::string& err)
{
    if (!validSongName(name)) {
        err = "ungültiger Song-Name";
        return false;
    }
    FILE* f = std::fopen((songDir() + "/" + name + ".json").c_str(), "rb");
    if (!f) {
        err = "Song nicht gefunden";
        return false;
    }
    json.clear();
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && json.size() < (1u << 20))
        json.append(buf, n);
    std::fclose(f);
    return true;
}

namespace {
link::Registry* registry()
{
    static link::Registry* r = link::open();
    return r;
}

const char* pluginName(uint32_t p)
{
    return p == link::kEngine ? "engine" : p == link::kTopline ? "topline" : p == link::kHorn ? "horn" : nullptr;
}

uint32_t pluginType(const std::string& n)
{
    return n == "engine" ? link::kEngine : n == "topline" ? link::kTopline : n == "horn" ? link::kHorn : 0;
}

bool copyFile(const std::string& from, const std::string& to)
{
    FILE* in = std::fopen(from.c_str(), "rb");
    if (!in)
        return false;
    FILE* out = std::fopen(to.c_str(), "wb");
    bool ok = out != nullptr;
    char buf[65536];
    size_t n;
    while (ok && (n = std::fread(buf, 1, sizeof(buf), in)) > 0)
        ok = std::fwrite(buf, 1, n, out) == n;
    std::fclose(in);
    if (out)
        std::fclose(out);
    return ok;
}
} // namespace

std::string withSounds(const std::string& setJson, const std::string& name, uint32_t linkId)
{
    link::Registry* reg = registry();
    if (!reg || setJson.empty() || setJson.back() != '}')
        return setJson;
    std::string list;
    std::unique_ptr<link::Entry> e(new link::Entry());
    for (int t = 0; t < 8; ++t) {
        const int i = link::find(reg->state, linkId, (uint32_t)t, false);
        if (i < 0 || !link::read(reg->state[i], *e) || link::now() - e->stamp > 2.0 || !pluginName(e->plugin))
            continue;
        std::string extra = e->extra;
        if (e->plugin == link::kHorn) { // Loops in den Song-Ordner kopieren, Pfade umschreiben
            const std::string dir = songDir() + "/" + name + " Loops";
            makeDir(dir);
            std::string out;
            size_t at = 0;
            for (int slot = 0; at <= extra.size(); ++slot) {
                size_t end = extra.find('\n', at);
                if (end == std::string::npos)
                    end = extra.size();
                std::string path = extra.substr(at, end - at);
                if (!path.empty() && path.compare(0, dir.size(), dir) != 0) {
                    const size_t slash = path.find_last_of("/\\");
                    const std::string to = dir + "/" + std::to_string(t + 1) + "-" + std::to_string(slot + 1) + "-"
                                         + path.substr(slash == std::string::npos ? 0 : slash + 1);
                    if (copyFile(path, to))
                        path = to;
                }
                out += (slot ? "\n" : "") + path;
                at = end + 1;
            }
            extra = out;
        }
        char buf[32];
        list += list.empty() ? "" : ",";
        list += "{\"track\":" + std::to_string(t) + ",\"plugin\":\"" + pluginName(e->plugin) + "\",\"values\":[";
        for (int k = 0; k < e->count; ++k) {
            std::snprintf(buf, sizeof(buf), "%s%.9g", k ? "," : "", e->values[k]);
            list += buf;
        }
        list += "],\"extra\":" + jsonQuote(extra) + "}";
    }
    if (list.empty())
        return setJson;
    return setJson.substr(0, setJson.size() - 1) + ",\"sounds\":[" + list + "]}";
}

int sendSounds(const std::string& songJson, uint32_t linkId)
{
    link::Registry* reg = registry();
    JsonValue root;
    std::string err;
    if (!reg || !parseJson(songJson.c_str(), root, err))
        return 0;
    const JsonValue* sounds = root.get("sounds");
    if (!sounds || sounds->type != JsonValue::Array)
        return 0;
    int sent = 0;
    std::unique_ptr<float[]> values(new float[link::kMaxValues]);
    for (const JsonValue& s : sounds->arr) {
        const JsonValue *t = s.get("track"), *p = s.get("plugin"), *v = s.get("values"), *x = s.get("extra");
        if (!t || t->type != JsonValue::Number || !p || p->type != JsonValue::String || !v || v->type != JsonValue::Array)
            continue;
        const uint32_t type = pluginType(p->str);
        const int track = (int)t->num;
        if (!type || track < 0 || track >= 8)
            continue;
        int n = 0;
        for (const JsonValue& f : v->arr)
            if (n < link::kMaxValues && f.type == JsonValue::Number)
                values[n++] = (float)f.num;
        const int i = link::find(reg->request, linkId, (uint32_t)track, true);
        if (i < 0)
            continue;
        link::write(reg->request[i], linkId, (uint32_t)track, type, values.get(), n,
                    x && x->type == JsonValue::String ? x->str.c_str() : nullptr);
        ++sent;
    }
    return sent;
}

} // namespace offbeat
