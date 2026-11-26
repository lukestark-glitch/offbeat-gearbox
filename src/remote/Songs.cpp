#include "Songs.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

} // namespace offbeat
