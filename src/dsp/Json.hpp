// Kleiner JSON-Leser für den Gearbox-State (Objekte, Listen, Texte, Zahlen, true/false/null). Nicht im Audio-Thread.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace offbeat {

struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    const JsonValue* get(const char* key) const; // nullptr, wenn nicht da oder kein Objekt
};

// false bei Syntaxfehler (err sagt, wo)
bool parseJson(const char* text, JsonValue& out, std::string& err);

// Text für JSON maskieren (mit Anführungszeichen)
std::string jsonQuote(const std::string& s);

} // namespace offbeat
