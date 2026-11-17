#include "bench.h"

#include "board.h"
#include "search.h"

#include <chrono>
#include <iostream>

namespace {

struct BenchPosition {
    const char* name;
    const char* fen;
};

// Same positions as the perft suite: they cover the opening, a tactical
// middlegame, an endgame, promotions and castling-heavy structures.
const BenchPosition BENCH_POSITIONS[] = {
    {"Start position",
     "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
    {"Kiwipete",
     "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
    {"Position 3 (endgame)",
     "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"},
    {"Position 4",
     "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"},
    {"Position 5",
     "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"},
    {"Position 6",
     "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"},
};

}  // namespace

unsigned long long runBench(int depth) {

    unsigned long long totalNodes = 0;
    double totalSeconds = 0.0;

    for (const BenchPosition& p : BENCH_POSITIONS) {

        bool whiteToMove = true;
        if (!loadFEN(p.fen, whiteToMove)) {
            std::cout << "bench: FEN failed to load: " << p.name << std::endl;
            continue;
        }

        Move best;
        int score = 0;

        const auto t0 = std::chrono::steady_clock::now();
        // Fixed depth, no deadline: the node count is fully deterministic.
        const bool ok = findBestMove(depth, whiteToMove, best, score);
        const auto t1 = std::chrono::steady_clock::now();

        const double secs = std::chrono::duration<double>(t1 - t0).count();
        const unsigned long long nodes = nodesSearched;

        totalNodes += nodes;
        totalSeconds += secs;

        std::cout << p.name << ": ";
        if (ok) {
            std::cout << "best " << moveToUCI(best) << " score " << score << " ";
        }
        std::cout << "nodes " << nodes << " time " << secs << "s" << std::endl;
    }

    const unsigned long long nps =
        totalSeconds > 0.0
            ? static_cast<unsigned long long>(totalNodes / totalSeconds)
            : 0;

    std::cout << "\n==========================="
              << "\nDepth          : " << depth
              << "\nTotal time (s) : " << totalSeconds
              << "\nNodes searched : " << totalNodes
              << "\nNodes/second   : " << nps << std::endl;

    return totalNodes;
}
