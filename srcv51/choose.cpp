#include "choose.h"

bool chooseBookMove(const GameContext& /*game*/, bool /*whiteToMove*/,
                    const EngineSettings& /*cfg*/, Move& /*out*/) {
    // No book yet.
    return false;
}

Move chooseMove(const Move& searched, int /*searchScore*/, int /*depthReached*/,
                bool /*whiteToMove*/, const GameContext& /*game*/,
                const EngineSettings& /*cfg*/) {
    // Full strength, no style: play exactly what the search found.
    return searched;
}
