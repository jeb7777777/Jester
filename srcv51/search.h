#ifndef SEARCH_H
#define SEARCH_H

#include "move.h"

// Limits for iterativeSearch.
struct SearchLimits {
    // Deepest iteration to attempt.
    int maxDepth = 64;

    // Wall-clock budget in milliseconds. 0 means no time limit.
    long long moveTimeMs = 0;
};

// Fixed-depth search with no time limit.
// Scores are from the point of view of the side to move: positive means
// the side to move is better.
bool findBestMove(
    int depth,
    bool whiteToMove,
    Move& bestMove,
    int& bestScore
);

// Iterative deepening: searches depth 1, 2, 3, ... until maxDepth is
// reached, time runs out, or a forced mate is found. Always returns the
// best move of the last COMPLETED depth. Returns false only if there are
// no legal moves. bestScore is from the side to move's point of view.
// With printInfo, prints UCI "info" lines as it goes.
bool iterativeSearch(
    bool whiteToMove,
    const SearchLimits& limits,
    Move& bestMove,
    int& bestScore,
    int& depthReached,
    bool printInfo = false
);

// Resizes the transposition table (the UCI "Hash" option) and empties it.
// The size is rounded DOWN to a power of two of 16-byte entries, so asking
// for 100 MB gives 64 MB. Returns the size actually used in megabytes, or 0
// if the memory could not be allocated (the old table is then kept).
int setHashSizeMB(int megabytes);

// Forgets everything the search has learned (transposition table, killer
// moves, history). Call on "ucinewgame" so that games stay independent.
void clearSearchMemory();

extern unsigned long long nodesSearched;

#endif
