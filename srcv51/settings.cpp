#include "settings.h"

#include "search.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

namespace {

// ---- Small text helpers ---------------------------------------------------

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    const char* whitespace = " \t\r\n";
    const std::size_t first = s.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return "";
    }
    const std::size_t last = s.find_last_not_of(whitespace);
    return s.substr(first, last - first + 1);
}

// Whole string must be a base-10 integer.
bool parseInt(const std::string& text, long long& out) {
    if (text.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') {
        return false;
    }
    out = value;
    return true;
}

const char* kindText(OpponentKind kind) {
    switch (kind) {
        case OpponentKind::Human:    return "human";
        case OpponentKind::Computer: return "computer";
        case OpponentKind::Unknown:  break;
    }
    return "unknown";
}

// ---- Option table ---------------------------------------------------------

enum class OptionType { Check, Spin, Combo, String };

struct OptionDef {
    const char* name;
    OptionType type;
    const char* defaultText;
    long long minValue;                 // spin only
    long long maxValue;                 // spin only
    std::vector<std::string> vars;      // combo only
    bool advertised;                    // listed in the reply to "uci"?
    // Receives a value that has already been checked and normalised
    // ("true"/"false", a plain decimal number, or the canonical combo entry).
    void (*apply)(EngineSettings&, const std::string&);
};

// UCI_Opponent value: "<title|none> <rating|none> <computer|human> <name...>".
// Parsed leniently: anything unreadable just stays "unknown".
void applyOpponent(EngineSettings& s, const std::string& value) {
    if (value.empty() || value == "<empty>") {
        s.opponent = OpponentInfo();
        return;
    }

    std::istringstream in(value);
    std::string title, rating, kind;
    in >> title >> rating >> kind;

    std::string name;
    std::getline(in, name);

    OpponentInfo info;
    info.title = (lower(title) == "none") ? std::string() : title;

    long long elo = 0;
    info.elo = parseInt(rating, elo) ? static_cast<int>(elo) : -1;

    const std::string k = lower(kind);
    if (k == "human")         info.kind = OpponentKind::Human;
    else if (k == "computer") info.kind = OpponentKind::Computer;

    info.name = trim(name);
    s.opponent = info;
}

// Hash value: transposition table size in megabytes. The search rounds it
// down to a power of two, so the size actually in use is stored (and
// reported) rather than the size that was asked for.
void applyHash(EngineSettings& s, const std::string& value) {
    const int requested = std::stoi(value);
    const int actual = setHashSizeMB(requested);

    if (actual == 0) {
        std::cout << "info string Hash: could not allocate " << requested
                  << " MB, keeping the current table" << std::endl;
        return;
    }

    s.hashMB = actual;
    if (actual != requested) {
        std::cout << "info string Hash rounded down to " << actual
                  << " MB (sizes are powers of two)" << std::endl;
    }
}

const std::vector<OptionDef>& optionTable() {
    static const std::vector<OptionDef> table = {
        // Transposition table size. The default (16 MB) is what the engine
        // used before this option existed.
        { "Hash", OptionType::Spin, "16", 1, 1024, {}, true,
          applyHash },

        // Advertised now: it only stores what it is told, so it is safe, and it
        // lets us see who the bot is playing.
        { "UCI_Opponent", OptionType::String, "<empty>", 0, 0, {}, true,
          applyOpponent },

        // Planned. Standard names so GUIs, cutechess and lichess-bot already
        // know how to set them.
        { "UCI_LimitStrength", OptionType::Check, "false", 0, 0, {}, false,
          [](EngineSettings& s, const std::string& v) { s.limitStrength = (v == "true"); } },
        { "UCI_Elo", OptionType::Spin, "1500", 400, 3000, {}, false,
          [](EngineSettings& s, const std::string& v) { s.elo = std::stoi(v); } },

        // Planned: personality.
        { "Jester_Mode", OptionType::Combo, "Serious", 0, 0, { "Serious", "Jester" }, false,
          [](EngineSettings& s, const std::string& v) {
              s.mode = (v == "Jester") ? PlayMode::Jester : PlayMode::Serious;
          } },
        { "Jester_Book", OptionType::Check, "false", 0, 0, {}, false,
          [](EngineSettings& s, const std::string& v) { s.useBook = (v == "true"); } },

        // Planned: reproducible randomness.
        { "Seed", OptionType::Spin, "1", 0, 2147483647LL, {}, false,
          [](EngineSettings& s, const std::string& v) {
              s.seed = static_cast<unsigned long long>(std::stoll(v));
              engineRng().seed(s.seed);
          } },
    };
    return table;
}

}  // namespace

// ---- Public ---------------------------------------------------------------

EngineSettings& settings() {
    static EngineSettings instance;
    return instance;
}

std::mt19937_64& engineRng() {
    static std::mt19937_64 rng(settings().seed);
    return rng;
}

SetOptionResult handleSetOption(std::istream& args, std::string& optionName) {

    optionName.clear();

    std::string token;
    args >> token;
    if (lower(token) != "name") {
        return SetOptionResult::UnknownOption;
    }

    // The name runs up to the word "value" (or the end of the line).
    std::string value;
    while (args >> token) {
        if (token == "value") {
            std::getline(args, value);
            value = trim(value);
            break;
        }
        optionName += (optionName.empty() ? "" : " ") + token;
    }

    const OptionDef* def = nullptr;
    for (const OptionDef& candidate : optionTable()) {
        if (lower(candidate.name) == lower(optionName)) {
            def = &candidate;
            break;
        }
    }
    if (def == nullptr) {
        return SetOptionResult::UnknownOption;
    }

    // Check the value against the option's type and normalise it, so the
    // apply functions never see anything unexpected.
    std::string normalized = value;
    switch (def->type) {
        case OptionType::Check: {
            const std::string v = lower(value);
            if (v != "true" && v != "false") {
                return SetOptionResult::BadValue;
            }
            normalized = v;
            break;
        }
        case OptionType::Spin: {
            long long n = 0;
            if (!parseInt(value, n) || n < def->minValue || n > def->maxValue) {
                return SetOptionResult::BadValue;
            }
            normalized = std::to_string(n);
            break;
        }
        case OptionType::Combo: {
            bool found = false;
            for (const std::string& var : def->vars) {
                if (lower(var) == lower(value)) {
                    normalized = var;
                    found = true;
                    break;
                }
            }
            if (!found) {
                return SetOptionResult::BadValue;
            }
            break;
        }
        case OptionType::String:
            break;
    }

    def->apply(settings(), normalized);
    return def->advertised ? SetOptionResult::Applied
                           : SetOptionResult::NotAvailableYet;
}

void printUciOptions(std::ostream& out) {
    for (const OptionDef& d : optionTable()) {
        if (!d.advertised) {
            continue;
        }
        out << "option name " << d.name << " type ";
        switch (d.type) {
            case OptionType::Check:
                out << "check default " << d.defaultText;
                break;
            case OptionType::Spin:
                out << "spin default " << d.defaultText
                    << " min " << d.minValue << " max " << d.maxValue;
                break;
            case OptionType::Combo:
                out << "combo default " << d.defaultText;
                for (const std::string& var : d.vars) {
                    out << " var " << var;
                }
                break;
            case OptionType::String:
                out << "string default " << d.defaultText;
                break;
        }
        out << "\n";
    }
}

void printSettings(std::ostream& out) {
    const EngineSettings& s = settings();
    out << "Hash              = " << s.hashMB << " MB\n"
        << "UCI_LimitStrength = " << (s.limitStrength ? "true" : "false") << "\n"
        << "UCI_Elo           = " << s.elo << "\n"
        << "Jester_Mode       = " << (s.mode == PlayMode::Jester ? "Jester" : "Serious") << "\n"
        << "Jester_Book       = " << (s.useBook ? "true" : "false") << "\n"
        << "Seed              = " << s.seed << "\n"
        << "UCI_Opponent      = title '" << s.opponent.title
        << "' elo " << s.opponent.elo
        << " kind " << kindText(s.opponent.kind)
        << " name '" << s.opponent.name << "'\n";
}
