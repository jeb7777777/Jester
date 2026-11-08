#include "uci.h"

#include "board.h"
#include "evaluation.h"
#include "perft.h"
#include "search.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// self-explantory.
static const char* ENGINE_NAME = "Jester v4";
static const char* ENGINE_AUTHOR = "jeb_7777";

// Whose turn it is in the current position. The board itself does not
// store this, so the UCI layer keeps track of it.
static bool whiteToMove = true;

static void setStartPosition() {
    initializeBoard();
    whiteToMove = true;
}

// position startpos [moves m1 m2 ...]
// position fen <fen fields> [moves m1 m2 ...]
static void handlePosition(std::istringstream& args) {

    std::string token;
    args >> token;

    bool sawMoves = false;

    if (token == "startpos") {
        setStartPosition();
        if (args >> token && token == "moves") {
            sawMoves = true;
        }
    }
    else if (token == "fen") {
        std::string fen;
        while (args >> token) {
            if (token == "moves") {
                sawMoves = true;
                break;
            }
            fen += token + " ";
        }

        bool white = true;
        if (!loadFEN(fen, white)) {
            std::cout << "info string invalid FEN: " << fen << std::endl;
            return;
        }
        whiteToMove = white;
    }
    else {
        return;
    }

    if (!sawMoves) {
        return;
    }

    std::string text;
    while (args >> text) {

        Move move;
        if (!parseMove(text, whiteToMove, move)) {
            std::cout << "info string illegal move: " << text << std::endl;
            return;
        }

        UndoInfo undo;
        applyMove(move, whiteToMove, undo);
        whiteToMove = !whiteToMove;
    }
}

// go depth N | movetime MS | wtime X btime Y [winc A binc B] [movestogo N]
//    | infinite
static void handleGo(std::istringstream& args) {

    long long wtime = -1, btime = -1, winc = 0, binc = 0, movetime = -1;
    int movestogo = 0;
    int depth = -1;

    std::string token;
    while (args >> token) {
        if      (token == "depth")     args >> depth;
        else if (token == "movetime")  args >> movetime;
        else if (token == "wtime")     args >> wtime;
        else if (token == "btime")     args >> btime;
        else if (token == "winc")      args >> winc;
        else if (token == "binc")      args >> binc;
        else if (token == "movestogo") args >> movestogo;
        // Anything else (infinite, ponder, nodes, ...) is ignored.
    }

    SearchLimits limits;

    if (depth >= 1) {
        limits.maxDepth = depth;
    }

    if (movetime >= 0) {
        limits.moveTimeMs = std::max(1LL, movetime);
    }
    else if (wtime >= 0 || btime >= 0) {
        // Spend a fraction of the remaining time plus most of the increment.
        const long long mine = std::max(0LL, whiteToMove ? wtime : btime);
        const long long inc  = whiteToMove ? winc : binc;
        // Never plan to spend everything on one move, even when it is the
        // last move before the next time control (divisor floor of 2).
        const int divisor = movestogo > 0 ? std::max(2, std::min(movestogo, 40)) : 30;

        long long budget = mine / divisor + inc * 3 / 4;

        // Keep a safety margin for process and GUI latency: at least 50 ms,
        // and 5% of the remaining time on longer clocks.
        const long long safety = std::max(50LL, mine / 20);
        budget = std::min(budget, mine - safety);
        limits.moveTimeMs = std::max(10LL, budget);
    }
    else if (depth < 1) {
        // "go" or "go infinite" with no other limit. This engine searches
        // on the same thread that reads commands, so it cannot be stopped
        // with "stop". Cap the search instead of running forever.
        limits.moveTimeMs = 5000;
    }

    Move best;
    int score = 0;
    int depthReached = 0;

    if (!iterativeSearch(whiteToMove, limits, best, score, depthReached, true)) {
        std::cout << "bestmove 0000" << std::endl;   // no legal moves
        return;
    }

    std::cout << "bestmove " << moveToUCI(best) << std::endl;
}

// Non-standard debugging command: show the board and basic facts.
static void handleDisplay() {

    printBoard(std::cout);
    std::cout << "FEN: " << toFEN(whiteToMove) << "\n"
              << "Side to move: " << (whiteToMove ? "white" : "black") << "\n"
              << "In check: " << (isKingInCheck(whiteToMove) ? "yes" : "no")
              << std::endl;
}

// Non-standard debugging command: list the legal moves.
static void handleMoves() {

    const std::vector<Move> moves = generateLegalMoves(whiteToMove);

    std::cout << "moves " << moves.size() << ":";
    for (const Move& m : moves) {
        std::cout << " " << moveToUCI(m);
    }
    std::cout << std::endl;
}

// Non-standard debugging command: static evaluation, from White's view.
static void handleEval() {
    std::cout << "eval " << evaluatePosition() << std::endl;
}

// Non-standard debugging command: list only the capture/promotion moves
// that quiescence search would consider.
static void handleCaptures() {

    const std::vector<Move> moves = generateLegalMoves(whiteToMove, true);

    std::cout << "captures " << moves.size() << ":";
    for (const Move& m : moves) {
        std::cout << " " << moveToUCI(m);
    }
    std::cout << std::endl;
}

void uciLoop() {

    setStartPosition();

    std::string line;

    while (std::getline(std::cin, line)) {

        // Windows GUIs may send "\r\n".
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::istringstream args(line);
        std::string command;
        args >> command;

        if (command.empty()) {
            continue;
        }

        if (command == "uci") {
            std::cout << "id name " << ENGINE_NAME << "\n"
                      << "id author " << ENGINE_AUTHOR << "\n"
                      << "uciok" << std::endl;
        }
        else if (command == "isready") {
            std::cout << "readyok" << std::endl;
        }
        else if (command == "ucinewgame") {
            setStartPosition();
        }
        else if (command == "position") {
            handlePosition(args);
        }
        else if (command == "go") {
            handleGo(args);
        }
        else if (command == "quit") {
            break;
        }
        else if (command == "stop" || command == "setoption" ||
                 command == "debug" || command == "ponderhit") {
            // Nothing to do: searches are never running while we read.
        }
        else if (command == "d") {
            handleDisplay();
        }
        else if (command == "moves") {
            handleMoves();
        }
        else if (command == "captures") {
            handleCaptures();
        }
        else if (command == "eval") {
            handleEval();
        }
        else if (command == "perft") {
            int depth = 1;
            args >> depth;
            if (depth >= 1) {
                perftDivide(depth, whiteToMove);
            }
        }
        else if (command == "perftsuite") {
            int depth = 4;
            args >> depth;
            runPerftSuite(depth);
        }
        else {
            std::cout << "info string unknown command: " << command << std::endl;
        }
    }
}
