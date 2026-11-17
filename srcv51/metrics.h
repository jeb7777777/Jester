#ifndef METRICS_H
#define METRICS_H

#include <string>

// Everything worth recording about one finished search.
struct MoveRecord {
    std::string fen;             // the position that was searched
    bool whiteToMove = true;

    long long clockMs = -1;      // our remaining time; -1 = not given ("go depth N")
    long long oppClockMs = -1;   // opponent's remaining time; -1 = not given
    long long incMs = -1;        // our increment per move; -1 = not given
    long long budgetMs = 0;      // time the engine allowed itself; 0 = no limit

    int depth = 0;               // last COMPLETED iteration
    int score = 0;               // side to move's point of view, search's own
                                 // encoding (mate scores are near +-MATE_SCORE)
    std::string best;            // best move in UCI notation (e.g. "e2e4")

    unsigned long long nodes = 0;
    long long timeMs = 0;        // measured wall-clock time of the whole search
};

// Call on "ucinewgame". Starts a new game id, but only if the current game
// has already logged something, so the usual ucinewgame-at-startup does not
// leave an empty game behind.
void metricsNewGame();

// Appends one JSON line to the metrics file. Never throws and never prints
// to stdout (stdout belongs to the UCI protocol). If the file cannot be
// opened the call silently does nothing.
//
// Output file: "jester_metrics.jsonl" in the engine's working directory.
// Set the environment variable JESTER_METRICS_FILE to use another path, or
// to "off" to disable logging.
void metricsLogMove(const MoveRecord& record);

#endif
