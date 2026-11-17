#ifndef CHOOSE_H
#define CHOOSE_H

#include <string>
#include <vector>

#include "move.h"
#include "settings.h"

// What the UCI layer knows about the game so far. It is rebuilt from every
// "position" command, so it always matches the board.
//
// Two future features need exactly this: an opening book (match the moves
// played so far against a table of lines) and repetition detection (every
// earlier position can be replayed from it).
struct GameContext {
    bool fromStartPosition = true;
    std::string startFen;                // only meaningful if !fromStartPosition
    std::vector<std::string> moves;      // UCI moves played after the start
};

// Called BEFORE searching. If it returns true, `out` is played immediately
// and no search happens. Always false today. Future home of the opening book.
bool chooseBookMove(const GameContext& game, bool whiteToMove,
                    const EngineSettings& cfg, Move& out);

// Called AFTER searching, with what the search found. Returns the move to
// play. Today it returns the search's own move, unchanged. Future home of
// strength limiting, style preferences and adaptive Elo.
//
// Note for later: the search only reports a score for its best move. To pick
// among several moves, the search must first be extended to return root-move
// scores.
Move chooseMove(const Move& searched, int searchScore, int depthReached,
                bool whiteToMove, const GameContext& game,
                const EngineSettings& cfg);

#endif
