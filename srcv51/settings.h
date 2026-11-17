#ifndef SETTINGS_H
#define SETTINGS_H

#include <iosfwd>
#include <random>
#include <string>

// Everything about how Jester can be configured from outside (GUI,
// lichess-bot, cutechess).
//
// DESIGN RULE: every default below reproduces the engine as it played before
// this file existed. Nothing here changes a move unless a later feature reads
// the setting AND someone switches it on. Options that do nothing yet are
// accepted by "setoption" (so tests and configs can already set them) but are
// NOT listed in the reply to "uci", so a GUI cannot offer a switch that does
// nothing. When a feature goes live, flip its `advertised` flag in
// settings.cpp.

// ---- Opponent (stored only; nothing reads it yet) ----------------------

enum class OpponentKind { Unknown, Human, Computer };

struct OpponentInfo {
    std::string title;                         // "" when none
    int elo = -1;                              // -1 when unknown
    OpponentKind kind = OpponentKind::Unknown;
    std::string name;
};

// ---- Settings -----------------------------------------------------------

enum class PlayMode { Serious, Jester };

struct EngineSettings {
    // Transposition table size in megabytes, as actually allocated (the
    // search rounds the requested size down to a power of two).
    int  hashMB = 16;                          // Hash

    // Strength limiting. Not implemented yet.
    bool limitStrength = false;                // UCI_LimitStrength
    int  elo = 1500;                           // UCI_Elo (range is a placeholder
                                               // until the levels are calibrated)

    // Personality. Not implemented yet.
    PlayMode mode = PlayMode::Serious;         // Jester_Mode
    bool useBook = false;                      // Jester_Book

    // Seed for engineRng(). Fixed by default so runs are reproducible.
    unsigned long long seed = 1;               // Seed

    // Filled from UCI_Opponent when the GUI / lichess-bot sends it.
    OpponentInfo opponent;                     // UCI_Opponent
};

// The one global instance.
EngineSettings& settings();

// ---- "setoption" ----------------------------------------------------------

enum class SetOptionResult {
    Applied,          // known and advertised: value stored
    NotAvailableYet,  // known but not advertised: value stored, no effect yet
    UnknownOption,    // no such option
    BadValue          // wrong type or out of range: nothing changed
};

// `args` is positioned just after the word "setoption", i.e. it holds
// "name <id> [value <x>]". The option name may contain spaces. Names are
// matched case-insensitively. `optionName` receives the name as typed, for
// messages.
SetOptionResult handleSetOption(std::istream& args, std::string& optionName);

// Writes one "option name ..." line for each advertised option.
void printUciOptions(std::ostream& out);

// Non-standard debugging aid: prints every setting.
void printSettings(std::ostream& out);

// ---- Randomness -------------------------------------------------------------

// The only random number source any feature should use. Seeded from
// settings().seed (and re-seeded when the Seed option is set), so a given seed
// always produces the same games.
std::mt19937_64& engineRng();

#endif
