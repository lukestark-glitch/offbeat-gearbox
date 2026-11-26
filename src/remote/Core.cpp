#include "Core.hpp"
#include "Json.hpp"
#include "Songs.hpp"

#include <cstdio>
#include <random>
#include <sstream>

namespace offbeat {

namespace {
uint32_t randomLinkId()
{
    std::random_device rd;
    uint32_t v = 0;
    while (v == 0)
        v = rd() & 0x0fffffff;
    return v;
}
} // namespace

Core::Core()
    : linkId(randomLinkId()), server(*this)
{
    defaultSet(shadow);
    current = new Set(shadow);
    for (auto& s : liveSteps)
        s = -1;
}

Core::~Core()
{
    server.setEnabled(false);
    delete pending.exchange(nullptr);
    delete garbage.exchange(nullptr);
    delete current;
}

std::string Core::stateJson()
{
    std::lock_guard<std::mutex> lock(mutex);
    return setToJson(shadow);
}

void Core::publish()
{
    delete pending.exchange(new Set(shadow)); // noch nicht übernommene Kopie verwerfen
    ++rev;
}

void Core::collect() { delete garbage.exchange(nullptr); }

bool Core::setStateJson(const char* json, std::string& err)
{
    std::lock_guard<std::mutex> lock(mutex);
    Set next = shadow;
    if (!applyJsonText(next, json, 0, err))
        return false;
    shadow = next;
    publish();
    collect();
    return true;
}

bool Core::command(const std::string& lines, std::string& err, bool allowParams)
{
    std::istringstream in(lines);
    std::string line;
    bool edited = false, ok = true;
    std::lock_guard<std::mutex> lock(mutex);
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#') // '#': Kommentar/Zähler
            continue;
        if (line.compare(0, 5, "song ") == 0) { // song save NAME | song load NAME (Datei-Zugriff, nie im Audio-Thread)
            std::istringstream sl(line.substr(5));
            std::string op, name, json, e;
            sl >> op;
            std::getline(sl >> std::ws, name);
            bool done = false;
            if (op == "save")
                edited |= done = saveSong(name, withSounds(setToJson(shadow), name, linkId), e); // neue Revision: Song-Liste
            else if (op == "load") {
                done = loadSong(name, json, e) && applyJsonText(shadow, json.c_str(), 0, e);
                if (done)
                    sendSounds(json, linkId);
                edited |= done;
            } else
                e = "song save NAME | song load NAME";
            if (!done) {
                if (err.empty())
                    err = e;
                ok = false;
            }
            continue;
        }
        int id = -1;
        float value = 0.0f;
        std::string e;
        switch (applyCommand(shadow, line, id, value, e)) {
        case CommandResult::Edited:
            edited = true;
            break;
        case CommandResult::Param: {
            if (!allowParams)
                break; // Oberfläche setzt Parameter direkt
            const int h = head.load(), next = (h + 1) % kQueue;
            if (next != tail.load()) {
                queue[h] = { id, value };
                head.store(next);
            }
            break;
        }
        case CommandResult::Error:
            if (err.empty())
                err = e;
            ok = false;
        }
    }
    if (edited)
        publish();
    collect();
    return ok;
}

const Set* Core::acquire()
{
    if (garbage.load() == nullptr) // alte Kopie erst übergeben, wenn die vorige freigegeben ist
        if (Set* next = pending.exchange(nullptr)) {
            garbage.store(current);
            current = next;
        }
    return current;
}

bool Core::popParam(int& id, float& value)
{
    const int t = tail.load();
    if (t == head.load())
        return false;
    id = queue[t].id, value = queue[t].value;
    tail.store((t + 1) % kQueue);
    return true;
}

void Core::publishLive(const float* p, double bpm)
{
    livePlaying = p[kPlaying] >= 0.5f;
    liveFill = p[kFill] >= 0.5f;
    livePattern = (int)p[kPlayingPattern];
    liveQueued = (int)p[kQueuedPattern];
    int mutes = 0;
    for (int t = 0; t < kTracks; ++t) {
        liveSteps[t] = (int)p[kStep1 + t];
        mutes |= (p[kMute1 + t] >= 0.5f) << t;
    }
    liveMutes = mutes;
    liveBpm = (float)bpm;
    liveSwing = p[kSwing];
}

std::string Core::remoteLive()
{
    char buf[256];
    const int mutes = liveMutes;
    int n = std::snprintf(buf, sizeof(buf), "{\"playing\":%s,\"bpm\":%.1f,\"pattern\":%d,\"queued\":%d,\"swing\":%.0f,\"fill\":%s,\"steps\":[",
                          livePlaying ? "true" : "false", (double)liveBpm.load(), livePattern.load(), liveQueued.load(),
                          (double)liveSwing.load(), liveFill ? "true" : "false");
    for (int t = 0; t < kTracks; ++t)
        n += std::snprintf(buf + n, sizeof(buf) - n, "%s%d", t ? "," : "", liveSteps[t].load());
    n += std::snprintf(buf + n, sizeof(buf) - n, "],\"mutes\":[");
    for (int t = 0; t < kTracks; ++t)
        n += std::snprintf(buf + n, sizeof(buf) - n, "%s%d", t ? "," : "", (mutes >> t) & 1);
    std::snprintf(buf + n, sizeof(buf) - n, "]}");
    return buf;
}

} // namespace offbeat

namespace offbeat {

std::string Core::remoteState()
{
    std::string j = stateJson(), list = ",\"songs\":[";
    const std::vector<std::string> songs = listSongs();
    for (size_t i = 0; i < songs.size(); ++i)
        list += (i ? "," : "") + jsonQuote(songs[i]);
    j.insert(j.size() - 1, list + "]");
    return j;
}

} // namespace offbeat
