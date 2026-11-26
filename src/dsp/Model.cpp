#include "Model.hpp"
#include "Json.hpp"
#include "Params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace offbeat {

const int kVelocityValues[4] = { 0, 50, 90, 110 };

namespace {
const char kVelChars[] = ".gxX";

struct DefaultTrack {
    const char* name;
    int note;
    const char* steps;
    bool melody;
};
// Noten wie die Vorschau von Offbeat Engine/Topline (F-Moll, Hard Bounce)
const DefaultTrack kDefaults[kTracks] = {
    { "Kick", 29, "X...X...X...X...", false },     // F1 auf jedem Schlag
    { "Bass", 41, "..x...x...x...x.", true },      // F2 auf dem Offbeat
    { "Hat", 54, "g.x.g.x.g.x.g.x.", false },      // F#3
    { "Open Hat", 58, "..........x.....", false },  // A#3
    { "Stab", 65, "......x.......x.", true },      // F4
    { "Pad", 53, "................", true },       // F3
    { "Lead", 77, "................", true },      // F5
    { "Perc", 49, "................", false },
};

int velFromChar(char c)
{
    const char* at = std::strchr(kVelChars, c);
    return at && c ? (int)(at - kVelChars) : -1;
}

void copyName(char* dst, const std::string& src)
{
    std::snprintf(dst, sizeof(TrackConf::name), "%s", src.c_str());
}

bool readTrackConf(TrackConf& t, const JsonValue& v)
{
    if (v.type != JsonValue::Object)
        return false;
    if (const JsonValue* x = v.get("name"); x && x->type == JsonValue::String) copyName(t.name, x->str);
    if (const JsonValue* x = v.get("channel"); x && x->type == JsonValue::Number) t.channel = std::clamp((int)x->num, 1, 16);
    if (const JsonValue* x = v.get("note"); x && x->type == JsonValue::Number) t.note = std::clamp((int)x->num, 0, 127);
    if (const JsonValue* x = v.get("gate"); x && x->type == JsonValue::Number) t.gate = std::clamp((float)x->num, 0.05f, 0.95f);
    if (const JsonValue* x = v.get("volume"); x && x->type == JsonValue::Number) t.volume = std::clamp((float)x->num, 0.0f, 1.0f);
    if (const JsonValue* x = v.get("solo"); x && x->type == JsonValue::Bool) t.solo = x->b;
    if (const JsonValue* x = v.get("melody"); x && x->type == JsonValue::Bool) t.melody = x->b;
    return true;
}

bool readPattern(Pattern& p, const JsonValue& v, std::string& err)
{
    const JsonValue* tracks = v.get("tracks");
    if (!tracks || tracks->type != JsonValue::Array) {
        err = "Pattern ohne \"tracks\"";
        return false;
    }
    for (int t = 0; t < kTracks && t < (int)tracks->arr.size(); ++t) {
        const JsonValue& tv = tracks->arr[t];
        PatternTrack& pt = p.tracks[t];
        if (const JsonValue* x = tv.get("length"); x && x->type == JsonValue::Number)
            pt.length = std::clamp((int)x->num, 1, kMaxSteps);
        if (const JsonValue* x = tv.get("steps"); x && x->type == JsonValue::String)
            for (int s = 0; s < kMaxSteps; ++s) {
                const int vel = s < (int)x->str.size() ? velFromChar(x->str[s]) : kVelOff;
                if (vel < 0) {
                    err = "unbekanntes Zeichen in \"steps\" (erlaubt: . g x X)";
                    return false;
                }
                pt.steps[s].vel = (uint8_t)vel;
            }
        if (const JsonValue* x = tv.get("ties"); x && x->type == JsonValue::String)
            for (int s = 0; s < kMaxSteps; ++s)
                pt.steps[s].tie = s < (int)x->str.size() && x->str[s] == '~';
        if (const JsonValue* x = tv.get("keys"); x && x->type == JsonValue::Object) { // {"Schritt":[Versatz, …]}
            for (Step& st : pt.steps)
                st.keys = 0;
            for (const auto& kv : x->obj) {
                const int s = std::atoi(kv.first.c_str());
                if (s < 0 || s >= kMaxSteps || kv.second.type != JsonValue::Array)
                    continue;
                for (const JsonValue& k : kv.second.arr)
                    if (k.type == JsonValue::Number)
                        pt.steps[s].keys |= keyBit((int)k.num);
            }
        }
    }
    return true;
}

void writePattern(std::string& o, const Pattern& p)
{
    o += "{\"tracks\":[";
    for (int t = 0; t < kTracks; ++t) {
        const PatternTrack& pt = p.tracks[t];
        std::string steps(kMaxSteps, '.'), ties(kMaxSteps, '.');
        for (int s = 0; s < kMaxSteps; ++s)
            steps[s] = kVelChars[pt.steps[s].vel & 3], ties[s] = pt.steps[s].tie ? '~' : '.';
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%s{\"length\":%d,\"steps\":\"", t ? "," : "", pt.length);
        o += buf + steps + "\",\"ties\":\"" + ties + "\"";
        std::string keys;
        for (int s = 0; s < kMaxSteps; ++s)
            if (pt.steps[s].keys) {
                std::snprintf(buf, sizeof(buf), "%s\"%d\":[", keys.empty() ? "" : ",", s);
                keys += buf;
                bool first = true;
                for (int k = 0; k < kKeyCount; ++k)
                    if (pt.steps[s].keys >> k & 1) {
                        std::snprintf(buf, sizeof(buf), "%s%d", first ? "" : ",", k + kKeyLow);
                        keys += buf;
                        first = false;
                    }
                keys += "]";
            }
        if (!keys.empty())
            o += ",\"keys\":{" + keys + "}";
        o += "}";
    }
    o += "]}";
}
} // namespace

void defaultSet(Set& s)
{
    s = Set();
    for (int t = 0; t < kTracks; ++t) {
        TrackConf& c = s.tracks[t];
        std::snprintf(c.name, sizeof(c.name), "%s", kDefaults[t].name);
        c.channel = t + 1;
        c.note = kDefaults[t].note;
        c.melody = kDefaults[t].melody;
        PatternTrack& pt = s.patterns[0].tracks[t];
        for (int k = 0; k < 16; ++k)
            pt.steps[k].vel = (uint8_t)velFromChar(kDefaults[t].steps[k]);
    }
}

std::string setToJson(const Set& s)
{
    std::string o = "{\"gearbox\":1,\"tracks\":[";
    for (int t = 0; t < kTracks; ++t) {
        const TrackConf& c = s.tracks[t];
        char buf[200];
        std::snprintf(buf, sizeof(buf), "%s{\"name\":%s,\"channel\":%d,\"note\":%d,\"gate\":%g,\"volume\":%g,\"solo\":%s,\"melody\":%s}",
                      t ? "," : "", jsonQuote(c.name).c_str(), c.channel, c.note, c.gate, c.volume, c.solo ? "true" : "false",
                      c.melody ? "true" : "false");
        o += buf;
    }
    o += "],\"patterns\":[";
    for (int p = 0; p < kPatterns; ++p) {
        if (p)
            o += ",";
        writePattern(o, s.patterns[p]);
    }
    return o + "]}";
}

std::string patternToJson(const Set& s, int pattern)
{
    std::string o = "{\"pattern\":";
    writePattern(o, s.patterns[std::clamp(pattern, 0, kPatterns - 1)]);
    return o + "}";
}

bool applyJsonText(Set& s, const char* json, int targetPattern, std::string& err)
{
    JsonValue root;
    if (!parseJson(json, root, err))
        return false;
    Set next = s; // erst alles prüfen, dann übernehmen
    if (const JsonValue* pat = root.get("pattern")) {
        if (!readPattern(next.patterns[std::clamp(targetPattern, 0, kPatterns - 1)], *pat, err))
            return false;
    } else if (root.get("patterns") || root.get("tracks")) {
        if (const JsonValue* tracks = root.get("tracks"); tracks && tracks->type == JsonValue::Array)
            for (int t = 0; t < kTracks && t < (int)tracks->arr.size(); ++t)
                readTrackConf(next.tracks[t], tracks->arr[t]);
        if (const JsonValue* pats = root.get("patterns"); pats && pats->type == JsonValue::Array)
            for (int p = 0; p < kPatterns && p < (int)pats->arr.size(); ++p)
                if (!readPattern(next.patterns[p], pats->arr[p], err))
                    return false;
    } else {
        err = "weder \"pattern\" noch \"patterns\" gefunden";
        return false;
    }
    s = next;
    return true;
}

CommandResult applyCommand(Set& s, const std::string& line, int& paramId, float& paramValue, std::string& err)
{
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    auto idx = [&](int& v, int max, const char* what) {
        if (!(in >> v) || v < 0 || v >= max) {
            err = std::string(what) + " fehlt oder außerhalb";
            return false;
        }
        return true;
    };
    int p, t, st;
    double x;
    if (cmd == "step" || cmd == "tie") {
        if (!idx(p, kPatterns, "Pattern") || !idx(t, kTracks, "Spur") || !idx(st, kMaxSteps, "Schritt") || !(in >> x)) {
            if (err.empty()) err = "Wert fehlt";
            return CommandResult::Error;
        }
        Step& step = s.patterns[p].tracks[t].steps[st];
        if (cmd == "step")
            step.vel = (uint8_t)std::clamp((int)x, 0, 3);
        else
            step.tie = x >= 0.5;
        return CommandResult::Edited;
    }
    if (cmd == "keys") { // keys P T S [Versatz,Versatz,…] – leer = Spur-Note
        if (!idx(p, kPatterns, "Pattern") || !idx(t, kTracks, "Spur") || !idx(st, kMaxSteps, "Schritt"))
            return CommandResult::Error;
        std::string list;
        in >> list;
        uint64_t keys = 0;
        for (size_t at = 0; at < list.size();) {
            const size_t end = std::min(list.find(',', at), list.size());
            keys |= keyBit(std::atoi(list.substr(at, end - at).c_str()));
            at = end + 1;
        }
        s.patterns[p].tracks[t].steps[st].keys = keys;
        return CommandResult::Edited;
    }
    if (cmd == "length" || cmd == "rotate") {
        if (!idx(p, kPatterns, "Pattern") || !idx(t, kTracks, "Spur") || !(in >> x)) {
            if (err.empty()) err = "Wert fehlt";
            return CommandResult::Error;
        }
        PatternTrack& pt = s.patterns[p].tracks[t];
        if (cmd == "length")
            pt.length = std::clamp((int)x, 1, kMaxSteps);
        else if (x > 0) // rechts: letzter Schritt wird erster
            std::rotate(pt.steps, pt.steps + pt.length - 1, pt.steps + pt.length);
        else
            std::rotate(pt.steps, pt.steps + 1, pt.steps + pt.length);
        return CommandResult::Edited;
    }
    if (cmd == "clear") { // clear P [T]
        if (!idx(p, kPatterns, "Pattern"))
            return CommandResult::Error;
        int only = -1;
        in >> only;
        for (int k = 0; k < kTracks; ++k)
            if (only < 0 || only == k)
                for (Step& step : s.patterns[p].tracks[k].steps)
                    step = Step();
        return CommandResult::Edited;
    }
    if (cmd == "copy") { // copy VON NACH
        int q;
        if (!idx(p, kPatterns, "Pattern") || !idx(q, kPatterns, "Ziel"))
            return CommandResult::Error;
        s.patterns[q] = s.patterns[p];
        return CommandResult::Edited;
    }
    if (cmd == "name") {
        if (!idx(t, kTracks, "Spur"))
            return CommandResult::Error;
        std::string name;
        std::getline(in >> std::ws, name);
        copyName(s.tracks[t].name, name);
        return CommandResult::Edited;
    }
    if (cmd == "note" || cmd == "channel" || cmd == "gate" || cmd == "volume" || cmd == "solo" || cmd == "melody") {
        if (!idx(t, kTracks, "Spur") || !(in >> x)) {
            if (err.empty()) err = "Wert fehlt";
            return CommandResult::Error;
        }
        TrackConf& c = s.tracks[t];
        if (cmd == "note") c.note = std::clamp((int)x, 0, 127);
        else if (cmd == "channel") c.channel = std::clamp((int)x, 1, 16);
        else if (cmd == "gate") c.gate = std::clamp((float)x, 0.05f, 0.95f);
        else if (cmd == "volume") c.volume = std::clamp((float)x, 0.0f, 1.0f);
        else if (cmd == "solo") c.solo = x >= 0.5;
        else c.melody = x >= 0.5;
        return CommandResult::Edited;
    }
    if (cmd == "mute") {
        if (!idx(t, kTracks, "Spur") || !(in >> x)) {
            if (err.empty()) err = "Wert fehlt";
            return CommandResult::Error;
        }
        paramId = kMute1 + t;
        paramValue = x >= 0.5 ? 1.0f : 0.0f;
        return CommandResult::Param;
    }
    const struct { const char* name; int id; } kParamCmds[] = {
        { "pattern", kPattern }, { "play", kPlay }, { "fill", kFill }, { "swing", kSwing }, { "bpm", kBpm },
    };
    for (const auto& pc : kParamCmds)
        if (cmd == pc.name) {
            if (!(in >> x)) {
                err = "Wert fehlt";
                return CommandResult::Error;
            }
            const ParamInfo& info = kParams[pc.id];
            paramId = pc.id;
            paramValue = std::clamp((float)x, info.min, info.max);
            if (info.flags & (kFlagInt | kFlagBool))
                paramValue = std::round(paramValue);
            return CommandResult::Param;
        }
    err = "unbekannter Befehl: " + cmd;
    return CommandResult::Error;
}

} // namespace offbeat
