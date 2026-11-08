#include "perft.h"
#include "board.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static bool sameState(const PositionState& a, const PositionState& b) {
    return a.castlingRights == b.castlingRights
        && a.enPassantRow   == b.enPassantRow
        && a.enPassantCol   == b.enPassantCol
        && a.halfmoveClock  == b.halfmoveClock
        && a.fullmoveNumber == b.fullmoveNumber;
}

static bool undoBrokenReported = false;

unsigned long long perft(int depth, bool whiteToMove, bool selfCheck) {

    if (depth == 0) return 1;

    std::vector<Move> moves = generateLegalMoves(whiteToMove);

    // Bulk counting: at the last ply the number of legal moves IS the
    // number of leaves, so there is no need to play them.
    if (depth == 1) return moves.size();

    unsigned long long nodes = 0;

    for (const Move& move : moves) {

        char savedBoard[8][8];
        PositionState savedState;
        if (selfCheck) {
            std::memcpy(savedBoard, board, sizeof(board));
            savedState = positionState;
        }

        UndoInfo undo;
        applyMove(move, whiteToMove, undo);

        nodes += perft(depth - 1, !whiteToMove, selfCheck);

        unmakeMove(move, undo);

        if (selfCheck && !undoBrokenReported &&
            (std::memcmp(savedBoard, board, sizeof(board)) != 0 ||
             !sameState(savedState, positionState))) {

            undoBrokenReported = true;
            std::cout << "UNDO BROKEN after move " << moveToUCI(move)
                      << ". Board now:\n";
            printBoard(std::cout);
        }
    }

    return nodes;
}

void perftDivide(int depth, bool whiteToMove) {

    std::vector<Move> moves = generateLegalMoves(whiteToMove);
    unsigned long long total = 0;

    for (const Move& move : moves) {

        UndoInfo undo;
        applyMove(move, whiteToMove, undo);

        unsigned long long n = perft(depth - 1, !whiteToMove);

        unmakeMove(move, undo);

        std::cout << moveToUCI(move) << ": " << n << "\n";
        total += n;
    }

    std::cout << "\nMoves: " << moves.size()
              << "  Nodes: " << total << std::endl;
}

namespace {

struct PerftCase {
    const char* name;
    const char* fen;
    std::vector<unsigned long long> expected;   // expected[i] is depth i+1
};

}

bool runPerftSuite(int maxDepth) {

    // Reference counts: https://www.chessprogramming.org/Perft_Results
    const std::vector<PerftCase> cases = {
        {"Start position",
         "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
         {20, 400, 8902, 197281, 4865609}},

        {"Kiwipete",
         "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         {48, 2039, 97862, 4085603}},

        {"Position 3",
         "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
         {14, 191, 2812, 43238, 674624}},

        {"Position 4",
         "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
         {6, 264, 9467, 422333}},

        {"Position 4 (mirrored, Black to move)",
         "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1",
         {6, 264, 9467, 422333}},

        {"Position 5",
         "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
         {44, 1486, 62379, 2103487}},

        {"Position 6",
         "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
         {46, 2079, 89890, 3894594}},
    };

    bool allPassed = true;

    for (const PerftCase& c : cases) {

        std::cout << "== " << c.name << " ==" << std::endl;

        for (size_t i = 0; i < c.expected.size(); i++) {

            const int depth = static_cast<int>(i) + 1;
            if (depth > maxDepth) break;

            bool whiteToMove = true;
            if (!loadFEN(c.fen, whiteToMove)) {
                std::cout << "FEN failed to load" << std::endl;
                allPassed = false;
                break;
            }

            auto t0 = std::chrono::steady_clock::now();
            // Self-check is on: it costs a little but reports exactly
            // which move breaks make/unmake.
            unsigned long long got = perft(depth, whiteToMove, true);
            auto t1 = std::chrono::steady_clock::now();

            double secs = std::chrono::duration<double>(t1 - t0).count();
            bool ok = (got == c.expected[i]);

            std::cout << "  depth " << depth << ": " << got
                      << " (expected " << c.expected[i] << ") "
                      << (ok ? "PASS" : "FAIL")
                      << "  " << secs << "s" << std::endl;

            if (!ok) {
                allPassed = false;
                break;   // deeper depths won't help; use perftDivide
            }
        }
        std::cout << "\n";
    }

    std::cout << (allPassed ? "ALL PERFT TESTS PASSED" : "SOME PERFT TESTS FAILED")
              << std::endl;

    return allPassed;
}
