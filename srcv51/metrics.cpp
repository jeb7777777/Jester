#include "metrics.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#define METRICS_GETPID _getpid
#else
#include <unistd.h>
#define METRICS_GETPID getpid
#endif

// Must match MATE_SCORE / MATE_BOUND in search.cpp. They are repeated here
// (rather than shared) so that the search code does not have to change.
static const int MATE_SCORE = 100000;
static const int MATE_BOUND = MATE_SCORE - 1000;

static long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Used only to make the game id unique across engine processes. lichess-bot
// starts a fresh engine process for every game.
static const long long sessionStartMs = nowMs();

static int gameNumber = 0;     // bumped by metricsNewGame()
static int searchCount = 0;    // searches logged in the current game

static const std::string& metricsPath() {
    static const std::string path = [] {
        const char* env = std::getenv("JESTER_METRICS_FILE");
        return std::string(env && *env ? env : "jester_metrics.jsonl");
    }();
    return path;
}

static std::string currentGameId() {
    std::ostringstream id;
    id << sessionStartMs << '-' << METRICS_GETPID() << '-' << gameNumber;
    return id.str();
}

void metricsNewGame() {
    if (searchCount > 0) {
        gameNumber++;
        searchCount = 0;
    }
}

void metricsLogMove(const MoveRecord& r) {

    if (metricsPath() == "off") {
        return;
    }

    searchCount++;

    // Hand-written JSON. The only strings are a FEN and a UCI move, which
    // contain no quotes or backslashes, so no escaping is needed.
    std::ostringstream line;
    line << "{\"ts\":" << nowMs()
         << ",\"game\":\"" << currentGameId() << '"'
         << ",\"n\":" << searchCount
         << ",\"side\":\"" << (r.whiteToMove ? 'w' : 'b') << '"'
         << ",\"fen\":\"" << r.fen << '"'
         << ",\"best\":\"" << r.best << '"'
         << ",\"depth\":" << r.depth;

    if (std::abs(r.score) >= MATE_BOUND) {
        // |score| = MATE_SCORE - plies; same conversion as the UCI info line.
        const int plies = MATE_SCORE - std::abs(r.score);
        const int movesToMate = (plies + 1) / 2;
        line << ",\"mate\":" << (r.score > 0 ? movesToMate : -movesToMate);
    }
    else {
        line << ",\"cp\":" << r.score;
    }

    const unsigned long long nps =
        r.timeMs > 0 ? r.nodes * 1000ULL / static_cast<unsigned long long>(r.timeMs) : 0;

    line << ",\"nodes\":" << r.nodes
         << ",\"time_ms\":" << r.timeMs
         << ",\"nps\":" << nps
         << ",\"budget_ms\":" << r.budgetMs;

    if (r.clockMs >= 0)    line << ",\"clock_ms\":" << r.clockMs;
    if (r.oppClockMs >= 0) line << ",\"opp_clock_ms\":" << r.oppClockMs;
    if (r.incMs >= 0)      line << ",\"inc_ms\":" << r.incMs;

    line << "}\n";

    // Open, append, close: one small write per move, so nothing is held
    // open between moves and a crash loses at most the line in flight.
    std::ofstream out(metricsPath(), std::ios::app);
    if (out) {
        out << line.str();
    }
}
