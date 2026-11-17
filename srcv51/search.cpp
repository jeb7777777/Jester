#include "search.h"
#include "board.h"
#include "evaluation.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <utility>
#include <vector>

// A side that is checkmated scores -(MATE_SCORE - ply), where ply is the
// distance from the root. A mate found sooner therefore scores higher, so
// the engine prefers faster mates and delays being mated.
const int MATE_SCORE = 100000;

// Anything beyond this magnitude is a mate score, not a material score.
const int MATE_BOUND = MATE_SCORE - 1000;

// "Infinity" for the alpha-beta window. Deliberately NOT INT_MIN/INT_MAX:
// negamax negates scores, and -INT_MIN overflows.
const int INF = 1000000;

// Score of a drawn position (repetition or the fifty-move rule). A named
// constant so that a "contempt" setting can replace it later.
const int DRAW_SCORE = 0;

// Counts the number of search positions visited.
unsigned long long nodesSearched = 0;

// Where the current search started, as an index into the board's position
// history. Needed to tell a repetition inside the search from one that
// involves the game played so far (see isRepeatedPosition).
static int searchRootIndex = 0;

// ---- Time control (private to this file) ----
static bool useDeadline = false;
static bool searchStopped = false;
static std::chrono::steady_clock::time_point searchDeadline;

// Looks at the clock only every 1024 nodes; reading it is not free.
// Once the deadline passes, every search function unwinds immediately.
static inline bool shouldStop() {
    if (useDeadline && (nodesSearched & 1023) == 0 &&
        std::chrono::steady_clock::now() >= searchDeadline) {
        searchStopped = true;
    }
    return searchStopped;
}

// ---------------------------------------------------------------------
// Preallocated storage: one move list per ply
// ---------------------------------------------------------------------
//
// Allocating a std::vector at every node costs real time, and filling a
// large array on every call costs even more. Since the search only ever
// has ONE node active per ply, each ply can own a permanent list that is
// simply emptied and refilled.

static const int MAX_PLY = 256;

static MoveList movePool[MAX_PLY];
static int scorePool[MAX_PLY][MOVELIST_CAPACITY];

// ---------------------------------------------------------------------
// Compact move encoding
// ---------------------------------------------------------------------
//
// The transposition table, the killer table and the history table all need to
// remember a move cheaply. A move fits in 15 bits:
//   bits 0-5   from square (row * 8 + col)
//   bits 6-11  to square
//   bits 12-14 promotion: 0 none, 1 queen, 2 rook, 3 bishop, 4 knight
// The value 0 (a8 to a8) is never a real move, so it means "no move".

static inline unsigned short packMove(const Move& m) {
    int promo = 0;
    switch (m.promotion) {
        case 'q': promo = 1; break;
        case 'r': promo = 2; break;
        case 'b': promo = 3; break;
        case 'n': promo = 4; break;
        default:  break;
    }
    return static_cast<unsigned short>(
        (m.fromRow * 8 + m.fromCol) | ((m.toRow * 8 + m.toCol) << 6) | (promo << 12));
}

// ---------------------------------------------------------------------
// Transposition table
// ---------------------------------------------------------------------
//
// The same position is reached by many different move orders. The table
// remembers what the search learned about a position (keyed by its Zobrist
// hash) so that a second visit can reuse it.
//
// What is stored depends on how the search ended at that node:
//   EXACT  the score is the true value (it fell inside the alpha-beta window)
//   LOWER  a beta cutoff happened: the true value is AT LEAST this score
//   UPPER  every move failed low: the true value is AT MOST this score
// A stored entry can end the search of the same position at once (a "cutoff")
// when it was searched at least as deep and its bound is usable for the
// current window. Even when it is too shallow for that, its best move is
// tried first, which makes alpha-beta prune far more.
//
// Mate scores are relative to the root (MATE_SCORE - ply). The same mate seen
// from a different depth must give a different number, so mate scores are
// stored relative to the NODE (see scoreToTT / scoreFromTT).
//
// Known limitation, shared by every engine with this design: whether a
// position is a repetition depends on the path that led to it, but the table
// is keyed by the position alone. The effect is small and accepted.

enum : unsigned char { BOUND_NONE = 0, BOUND_EXACT = 1, BOUND_LOWER = 2, BOUND_UPPER = 3 };

struct TTEntry {
    unsigned long long key;     // full hash, to detect index collisions
    int score;                  // node-relative for mate scores
    unsigned short move;        // packMove() of the best move, 0 if none
    signed char depth;          // remaining depth the entry was searched to
    unsigned char bound;        // BOUND_*; BOUND_NONE marks an empty slot
};

// Default size in megabytes: the default of the UCI "Hash" option.
static const int TT_DEFAULT_MB = 16;

// The table. Its number of entries is always a power of two, so the index is
// simply hash & ttMask (ttMask = entries - 1). Each entry is 16 bytes. A
// vector of zero-filled entries is exactly "all slots empty".
static std::vector<TTEntry> ttTable;
static size_t ttMask = 0;

static void clearTranspositionTable() {
    std::memset(ttTable.data(), 0, ttTable.size() * sizeof(TTEntry));
}

int setHashSizeMB(int megabytes) {

    if (megabytes < 1) {
        megabytes = 1;
    }

    // Largest power of two of entries that fits in the requested size.
    const size_t wanted = (static_cast<size_t>(megabytes) << 20) / sizeof(TTEntry);
    size_t entries = 1;
    while (entries * 2 <= wanted) {
        entries *= 2;
    }

    // Same size as now: just empty it, no reallocation.
    if (entries == ttTable.size()) {
        clearTranspositionTable();
        return static_cast<int>((entries * sizeof(TTEntry)) >> 20);
    }

    try {
        // Allocate the new table first, so a failure leaves the old one intact.
        std::vector<TTEntry> fresh(entries);
        ttTable.swap(fresh);
        ttMask = entries - 1;
    }
    catch (const std::bad_alloc&) {
        return 0;
    }

    return static_cast<int>((entries * sizeof(TTEntry)) >> 20);
}

// Gives the table its default size before main() runs. (Defined after the
// table above, so the vector already exists when this runs.)
static const int ttInitialSize = setHashSizeMB(TT_DEFAULT_MB);

static inline int scoreToTT(int score, int ply) {
    if (score >= MATE_BOUND)  return score + ply;
    if (score <= -MATE_BOUND) return score - ply;
    return score;
}

static inline int scoreFromTT(int score, int ply) {
    if (score >= MATE_BOUND)  return score - ply;
    if (score <= -MATE_BOUND) return score + ply;
    return score;
}

static inline const TTEntry* ttLookup(unsigned long long hash) {
    const TTEntry& e = ttTable[hash & ttMask];
    return (e.bound != BOUND_NONE && e.key == hash) ? &e : nullptr;
}

static inline void ttStore(unsigned long long hash, int depth, int score,
                           unsigned char bound, unsigned short move) {
    TTEntry& e = ttTable[hash & ttMask];

    if (e.bound != BOUND_NONE && e.key == hash) {
        // Same position seen before. Never replace a deeper result with a
        // shallower one, and keep the old best move if we have none to offer
        // (a fail-low node knows no best move).
        if (depth < e.depth) {
            return;
        }
        if (move == 0) {
            move = e.move;
        }
    }

    e.key = hash;
    e.score = score;
    e.move = move;
    e.depth = static_cast<signed char>(depth);
    e.bound = bound;
}

// ---------------------------------------------------------------------
// Killer moves and history heuristic
// ---------------------------------------------------------------------
//
// Captures are ordered by MVV-LVA, but quiet moves have no natural order, so
// without help they are tried in the order the generator finds them. Two cheap
// ideas fix that, both learned from beta cutoffs (a quiet move that refuted
// the opponent once is likely to do it again):
//
//   Killers   two quiet moves per ply that most recently caused a cutoff at
//             that ply. A sibling position at the same ply often has the same
//             refutation.
//   History   a counter per (side, from, to) that grows with depth * depth
//             every time that quiet move causes a cutoff anywhere in the tree.
//
// Neither is needed for correctness: they only change the ORDER moves are
// tried in, never which moves exist.

static unsigned short killers[MAX_PLY][2];
static int history[2][64][64];

static const int HISTORY_LIMIT = 16384;   // counters are halved beyond this

// Ordering score tiers (higher is searched first). Captures from
// moveOrderScore() are always >= 10000 + 94, so they stay above the quiet tiers.
static const int TT_MOVE_SCORE = 1000000;
static const int KILLER1_SCORE = 5000;
static const int KILLER2_SCORE = 4000;
// History scores are history >> 4, i.e. at most 1024.

static void clearOrderingTables() {
    std::memset(killers, 0, sizeof(killers));
    std::memset(history, 0, sizeof(history));
}

// Forget everything learned so far: the transposition table, killers and
// history. Called by the UCI layer on "ucinewgame", so one game never
// influences the next one when a GUI reuses the same engine process.
void clearSearchMemory() {
    clearTranspositionTable();
    clearOrderingTables();
}

// Called when a QUIET move caused a beta cutoff.
static void rewardQuietMove(const Move& move, bool whiteToMove, int depth, int ply) {

    const unsigned short packed = packMove(move);

    if (killers[ply][0] != packed) {
        killers[ply][1] = killers[ply][0];
        killers[ply][0] = packed;
    }

    int& h = history[whiteToMove ? 0 : 1][move.fromRow * 8 + move.fromCol]
                    [move.toRow * 8 + move.toCol];
    h += depth * depth;

    if (h > HISTORY_LIMIT) {
        // Keep the counters comparable: age everything instead of letting
        // one hot move climb forever.
        int* all = &history[0][0][0];
        for (int i = 0; i < 2 * 64 * 64; i++) {
            all[i] >>= 1;
        }
    }
}

// ---------------------------------------------------------------------
// Evaluation from the side to move's point of view
// ---------------------------------------------------------------------

// evaluatePosition() is always from White's point of view. Negamax needs
// "good for whoever is to move", so flip the sign for Black.
static inline int evaluateRelative(bool whiteToMove) {
    const int score = evaluatePosition();
    return whiteToMove ? score : -score;
}

// ---------------------------------------------------------------------
// Move ordering: MVV-LVA (Most Valuable Victim, Least Valuable Attacker)
// ---------------------------------------------------------------------

static int pieceRank(char piece) {
    switch (piece | 32) {      // lower-case
        case 'p': return 1;
        case 'n': return 2;
        case 'b': return 3;
        case 'r': return 4;
        case 'q': return 5;
        case 'k': return 6;
        default:  return 0;
    }
}

// Higher scores are searched first. Must be called BEFORE the move is made.
static int moveOrderScore(const Move& move) {

    const char attacker = board[move.fromRow][move.fromCol];
    char victim = board[move.toRow][move.toCol];

    // A pawn moving diagonally onto an empty square is en passant.
    if (victim == '.' && (attacker == 'P' || attacker == 'p') &&
        move.fromCol != move.toCol) {
        victim = 'p';
    }

    int score = 0;

    if (victim != '.') {
        // Captures first. A big victim taken by a small attacker scores
        // highest (pawn takes queen); the attacker rank breaks ties.
        score += 10000 + 100 * pieceRank(victim) - pieceRank(attacker);
    }

    if (move.promotion == 'q') {
        score += 9000;
    }
    else if (move.promotion != '\0') {
        score -= 100;   // underpromotions go after quiet moves
    }

    return score;
}

// Used for the root move list only (a plain vector, sorted once).
static void orderMoves(std::vector<Move>& moves) {

    std::vector<std::pair<int, Move>> scored;
    scored.reserve(moves.size());

    for (const Move& move : moves) {
        scored.push_back(std::make_pair(moveOrderScore(move), move));
    }

    // Stable: moves with equal scores keep their generation order.
    std::stable_sort(scored.begin(), scored.end(),
        [](const std::pair<int, Move>& a, const std::pair<int, Move>& b) {
            return a.first > b.first;
        });

    for (size_t i = 0; i < moves.size(); i++) {
        moves[i] = scored[i].second;
    }
}

static inline void scoreMoves(const MoveList& list, int* scores) {
    for (int i = 0; i < list.count; i++) {
        scores[i] = moveOrderScore(list[i]);
    }
}

// Full ordering for the main search: MVV-LVA for captures and promotions
// (unchanged), then the table move first, then killers and history for quiet
// moves. Quiescence keeps using plain scoreMoves above.
//
// A quiet move is exactly one that moveOrderScore() scores 0 (no victim and
// no promotion), so no second board lookup is needed to recognise it.
static void scoreMovesOrdered(const MoveList& list, int* scores,
                              bool whiteToMove, int ply, unsigned short ttMove) {

    const int side = whiteToMove ? 0 : 1;

    for (int i = 0; i < list.count; i++) {

        const Move& move = list[i];
        int score = moveOrderScore(move);

        if (score == 0) {
            const unsigned short packed = packMove(move);

            if (packed == killers[ply][0]) {
                score = KILLER1_SCORE;
            }
            else if (packed == killers[ply][1]) {
                score = KILLER2_SCORE;
            }
            else {
                score = history[side][move.fromRow * 8 + move.fromCol]
                               [move.toRow * 8 + move.toCol] >> 4;
            }
        }

        // The table move goes first whatever kind of move it is. It only
        // matters if it is in the list, so a hash collision cannot inject
        // an impossible move.
        if (ttMove != 0 && packMove(move) == ttMove) {
            score = TT_MOVE_SCORE;
        }

        scores[i] = score;
    }
}

// Selection-sort step: puts the best remaining move at position i.
//
// Why not sort the whole list? Most nodes end after trying only a move
// or two (a beta cutoff), so sorting all ~35 moves is mostly wasted work.
// Picking one at a time sorts only as much as is actually used.
//
// The moves in between are shifted, not swapped, so equal-scored moves keep
// their generation order, exactly as a stable sort would leave them.
static inline void pickBest(MoveList& list, int* scores, int i) {

    int best = i;
    for (int j = i + 1; j < list.count; j++) {
        if (scores[j] > scores[best]) {
            best = j;
        }
    }

    if (best != i) {
        const Move m = list[best];
        const int s = scores[best];
        for (int j = best; j > i; j--) {
            list[j] = list[j - 1];
            scores[j] = scores[j - 1];
        }
        list[i] = m;
        scores[i] = s;
    }
}

// True if the given side has at least one knight, bishop, rook or queen.
// Used to keep null-move pruning out of pawn endings, where zugzwang (a
// position in which any move is worse than passing) is common.
static bool hasNonPawnMaterial(bool white) {
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            const char p = board[r][c];
            if (p == '.') continue;
            if (white ? (p == 'N' || p == 'B' || p == 'R' || p == 'Q')
                      : (p == 'n' || p == 'b' || p == 'r' || p == 'q')) {
                return true;
            }
        }
    }
    return false;
}

// Null-move pruning is not tried below this remaining depth.
static const int NULL_MOVE_MIN_DEPTH = 3;

// Late move reductions (see the comment in negamax).
// The first LMR_FULL_MOVES legal moves of a node are never reduced.
static const int LMR_FULL_MOVES = 3;
// No reductions below this remaining depth.
static const int LMR_MIN_DEPTH = 3;
// How many plies shallower a late quiet move is searched at first. Start
// small: a reduction that is too eager hides good quiet moves.
static const int LMR_REDUCTION = 1;

// ---------------------------------------------------------------------
// Quiescence search
// ---------------------------------------------------------------------

// Called instead of evaluating when the normal search runs out of depth.
// Keeps playing forcing moves (captures, queen promotions) until the
// position is "quiet", so the evaluation is never taken in the middle of
// a capture exchange.
//
// knownNotInCheck: the caller (negamax) has already tested for check, so the
// test is skipped. Recursive calls from here leave it false and test normally.
static int quiescence(bool whiteToMove, int alpha, int beta, int ply,
                      bool knownNotInCheck = false) {

    nodesSearched++;

    if (shouldStop()) {
        return 0;
    }

    if (ply >= MAX_PLY - 1) {
        return evaluateRelative(whiteToMove);   // safety net, never hit in practice
    }

    const bool inCheck = knownNotInCheck ? false : isKingInCheck(whiteToMove);

    int best;

    if (inCheck) {
        // Standing pat is not allowed in check: we must get out of it, so
        // every move is considered. No legal move at all means mate.
        best = -INF;
    }
    else {
        // "Stand pat": the side to move is never forced to capture, so
        // the static evaluation is a floor for the score.
        best = evaluateRelative(whiteToMove);

        if (best >= beta) {
            return best;           // already good enough: cut off
        }
        if (best > alpha) {
            alpha = best;
        }
    }

    MoveList& list = movePool[ply];
    int* scores = scorePool[ply];

    list.clear();
    generatePseudoLegalMoves(whiteToMove, list, !inCheck);   // captures only unless in check
    scoreMoves(list, scores);

    int legalMoves = 0;

    for (int i = 0; i < list.count; i++) {

        pickBest(list, scores, i);
        const Move move = list[i];

        if (isCastlingMove(move) && !castlingIsSafe(move, whiteToMove, inCheck)) {
            continue;
        }

        UndoInfo undo;
        applyMove(move, whiteToMove, undo);

        // Legality is checked only for moves we actually try.
        if (isKingInCheck(whiteToMove)) {
            unmakeMove(move, undo);
            continue;
        }

        legalMoves++;

        const int score = -quiescence(!whiteToMove, -beta, -alpha, ply + 1);

        unmakeMove(move, undo);

        if (searchStopped) {
            return 0;
        }

        if (score > best) {
            best = score;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            break;
        }
    }

    if (inCheck && legalMoves == 0) {
        return -(MATE_SCORE - ply);
    }

    return best;
}

// ---------------------------------------------------------------------
// Negamax alpha-beta
// ---------------------------------------------------------------------

// Returns the score from the side to move's point of view.
// (alpha, beta) is the window in which the score matters to the caller;
// the child gets the negated, swapped window (-beta, -alpha).
//
// allowNull is false only for the child of a null move: two passes in a row
// would prove nothing, so the search never makes them.
static int negamax(int depth, bool whiteToMove, int alpha, int beta, int ply,
                   bool allowNull = true) {

    // Draws come first, before anything else is looked at. Checking ahead of
    // the depth test means leaf positions are covered too.
    //
    // 1. Repetition (the exact rule is described at isRepeatedPosition).
    if (isRepeatedPosition(searchRootIndex)) {
        return DRAW_SCORE;
    }

    // 2. Fifty-move rule: 100 half-moves without a capture or pawn move.
    // Checkmate takes precedence, so when in check the side to move must also
    // have a legal move for this to count. (Rare, so the move generation here
    // costs nothing in practice.)
    if (positionState.halfmoveClock >= 100 &&
        (!isKingInCheck(whiteToMove) || !generateLegalMoves(whiteToMove).empty())) {
        return DRAW_SCORE;
    }

    // Check extension: a side in check has very few replies, so searching it
    // one ply deeper is cheap and stops the search from stopping in the middle
    // of a forcing sequence of checks (the horizon effect). Being in check
    // also guarantees at least one full-width ply, so a leaf in check looks at
    // every evasion instead of going straight to quiescence.
    //
    // This must come BEFORE the depth test below, and before the table probe
    // (so the stored depth is the extended depth, the same for every visit to
    // this position, since "in check" depends only on the position).
    //
    // Depth cannot run away: each ply costs 1 and a check gives back at most
    // 1, and a perpetual-check line ends in a repetition draw.
    const bool inCheck = isKingInCheck(whiteToMove);
    if (inCheck) {
        depth++;
    }

    // Out of depth: resolve the capture exchanges instead of evaluating.
    // (We are not in check here, which saves quiescence from asking again.)
    if (depth <= 0) {
        return quiescence(whiteToMove, alpha, beta, ply, true);
    }

    nodesSearched++;

    if (shouldStop()) {
        return 0;
    }

    if (ply >= MAX_PLY - 1) {
        return evaluateRelative(whiteToMove);   // safety net, never hit in practice
    }

    // Transposition table: have we searched this exact position before?
    // (After the draw checks above, so a repetition is still scored as a draw.)
    const unsigned long long hash = positionState.hash;
    unsigned short ttMove = 0;

    if (const TTEntry* entry = ttLookup(hash)) {
        ttMove = entry->move;

        if (entry->depth >= depth) {
            const int ttScore = scoreFromTT(entry->score, ply);

            if (entry->bound == BOUND_EXACT) {
                return ttScore;
            }
            if (entry->bound == BOUND_LOWER && ttScore >= beta) {
                return ttScore;
            }
            if (entry->bound == BOUND_UPPER && ttScore <= alpha) {
                return ttScore;
            }
        }
    }

    // Null-move pruning. If we are already so far ahead that even PASSING
    // (giving the opponent a free move) still leaves the score at or above
    // beta, then a real move would do at least as well, so this node can be
    // cut off after a much shallower search. The pass is searched R plies
    // shallower than a normal move would be.
    //
    // It is unsound in positions where passing is better than any real move
    // (zugzwang), so it is only tried when it is safe in practice:
    //   * not in check (a pass would leave the king capturable)
    //   * not in a PV node (beta - alpha > 1), where exact scores are needed
    //   * enough depth left for the reduced search to mean something
    //   * beta is not a mate score (a "mate" proven after a pass is bogus)
    //   * the static evaluation already reaches beta (cheap filter; otherwise
    //     the pass almost never works and the search is wasted)
    //   * the side to move owns a piece besides pawns and king, which is
    //     where zugzwang is rare; pawn endings are searched normally
    //   * the previous move was not itself a pass
    if (allowNull &&
        !inCheck &&
        beta - alpha == 1 &&
        depth >= NULL_MOVE_MIN_DEPTH &&
        std::abs(beta) < MATE_BOUND &&
        evaluateRelative(whiteToMove) >= beta &&
        hasNonPawnMaterial(whiteToMove)) {

        const int reduction = (depth >= 6) ? 3 : 2;

        UndoInfo nullUndo;
        applyNullMove(whiteToMove, nullUndo);

        const int nullScore = -negamax(depth - 1 - reduction, !whiteToMove,
                                       -beta, -beta + 1, ply + 1, false);

        unmakeNullMove(nullUndo);

        if (searchStopped) {
            return 0;
        }

        if (nullScore >= beta) {
            // Never return a mate score found through a pass: it is not a
            // real forced mate.
            return nullScore >= MATE_BOUND ? beta : nullScore;
        }
    }

    // Pseudo-legal moves only. Whether each one is truly legal is decided
    // lazily, when (and if) the search reaches it. After a cutoff the rest
    // are never tested, which is where most of the time used to go.
    MoveList& list = movePool[ply];
    int* scores = scorePool[ply];

    list.clear();
    generatePseudoLegalMoves(whiteToMove, list);
    scoreMovesOrdered(list, scores, whiteToMove, ply, ttMove);

    const int originalAlpha = alpha;
    int best = -INF;
    unsigned short bestMovePacked = 0;
    int legalMoves = 0;

    for (int i = 0; i < list.count; i++) {

        pickBest(list, scores, i);
        const Move move = list[i];

        if (isCastlingMove(move) && !castlingIsSafe(move, whiteToMove, inCheck)) {
            continue;
        }

        // Must be decided BEFORE the move is made (it looks at the board).
        const bool quiet = (moveOrderScore(move) == 0);

        UndoInfo undo;
        applyMove(move, whiteToMove, undo);

        if (isKingInCheck(whiteToMove)) {
            unmakeMove(move, undo);   // illegal: leaves our king attacked
            continue;
        }

        legalMoves++;

        // Principal variation search. The first move is expected to be the
        // best one (table move, then captures, killers, history), so it gets
        // the full window. Every later move only has to PROVE it is no better:
        // a zero-width window (-alpha-1, -alpha) asks just "does this beat
        // alpha?", which cuts off far sooner. Only if the answer is yes, and
        // the score also stays below beta, is the move searched again with the
        // full window to get its exact value.
        //
        // Late move reductions (LMR) add one more cheap step in front of that.
        // A quiet move that comes late in the ordering is unlikely to be the
        // best, so it is first searched LMR_REDUCTION plies shallower. Only if
        // that shallow look "surprises" us by beating alpha is it searched
        // again at full depth (still with the zero window), and only if THAT
        // still beats alpha does the full-window search below run. A move that
        // fails to beat alpha even when searched shallowly is simply dropped.
        //
        // The search is not reduced when it would be risky:
        //   * the first LMR_FULL_MOVES moves are always searched in full
        //   * only quiet moves (captures and promotions are forcing)
        //   * killer moves are kept at full depth (they worked nearby)
        //   * never when we are in check, nor for a move that gives check
        //   * not in PV nodes, where exact scores matter
        //   * only with enough depth left for a reduced search to mean something
        int score;
        if (legalMoves == 1) {
            score = -negamax(depth - 1, !whiteToMove, -beta, -alpha, ply + 1);
        }
        else {
            int reduction = 0;

            if (quiet &&
                legalMoves > LMR_FULL_MOVES &&
                depth >= LMR_MIN_DEPTH &&
                !inCheck &&
                beta - originalAlpha == 1) {   // zero-window node; alpha itself rises during the loop

                const unsigned short packed = packMove(move);
                const bool isKiller = (packed == killers[ply][0] ||
                                       packed == killers[ply][1]);

                // The move is already on the board, so this asks whether the
                // OPPONENT is now in check, i.e. whether the move gives check.
                if (!isKiller && !isKingInCheck(!whiteToMove)) {
                    reduction = LMR_REDUCTION;
                }
            }

            score = -negamax(depth - 1 - reduction, !whiteToMove, -alpha - 1, -alpha, ply + 1);

            // The surprise: the shallow search says this late move beats
            // alpha. Do not trust it; look again at the proper depth.
            if (reduction > 0 && score > alpha && !searchStopped) {
                score = -negamax(depth - 1, !whiteToMove, -alpha - 1, -alpha, ply + 1);
            }

            if (score > alpha && score < beta && !searchStopped) {
                score = -negamax(depth - 1, !whiteToMove, -beta, -alpha, ply + 1);
            }
        }

        unmakeMove(move, undo);

        if (searchStopped) {
            return 0;
        }

        if (score > best) {
            best = score;
            bestMovePacked = packMove(move);
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            if (quiet) {
                rewardQuietMove(move, whiteToMove, depth, ply);
            }
            break;      // the opponent will never allow this line
        }
    }

    // No legal move: checkmate or stalemate.
    if (legalMoves == 0) {
        return inCheck ? -(MATE_SCORE - ply) : 0;
    }

    // Remember the result. (A stopped search returned above, so nothing
    // half-finished is ever stored.)
    unsigned char bound;
    if (best <= originalAlpha)  bound = BOUND_UPPER;   // failed low
    else if (best >= beta)      bound = BOUND_LOWER;   // failed high
    else                        bound = BOUND_EXACT;

    ttStore(hash, depth, scoreToTT(best, ply), bound,
            bound == BOUND_UPPER ? 0 : bestMovePacked);

    return best;
}

// ---------------------------------------------------------------------
// Root search and iterative deepening
// ---------------------------------------------------------------------

enum class RootResult {
    Complete,   // every root move was searched
    Partial,    // stopped early, but at least one move was fully searched
    Nothing     // stopped before the first move finished
};

// Searches every root move to the given depth, in the order given.
static RootResult searchRoot(
    const std::vector<Move>& moves,
    int depth,
    bool whiteToMove,
    Move& bestMove,
    int& bestScore
) {

    bool foundMove = false;

    int alpha = -INF;
    const int beta = INF;

    bestScore = -INF;

    for (const Move& move : moves) {

        UndoInfo undo;
        applyMove(move, whiteToMove, undo);

        // Same idea as inside negamax: the first root move (the best one from
        // the previous iteration) gets the full window; the others only have
        // to show they beat it, and are searched again only if they do.
        int score;
        if (!foundMove) {
            score = -negamax(depth - 1, !whiteToMove, -beta, -alpha, 1);
        }
        else {
            score = -negamax(depth - 1, !whiteToMove, -alpha - 1, -alpha, 1);
            if (score > alpha && !searchStopped) {
                score = -negamax(depth - 1, !whiteToMove, -beta, -alpha, 1);
            }
        }

        unmakeMove(move, undo);

        if (searchStopped) {
            // Moves finished before the stop are fully searched, so the
            // best of them is still a trustworthy result.
            return foundMove ? RootResult::Partial : RootResult::Nothing;
        }

        if (!foundMove || score > bestScore) {
            bestScore = score;
            bestMove = move;
            foundMove = true;
        }

        if (score > alpha) {
            alpha = score;
        }
    }

    return foundMove ? RootResult::Complete : RootResult::Nothing;
}


// Find the best move using a fixed-depth search.
bool findBestMove(
    int depth,
    bool whiteToMove,
    Move& bestMove,
    int& bestScore
) {

    std::vector<Move> moves =
        generateLegalMoves(whiteToMove);

    if (moves.empty()) {
        return false;
    }

    orderMoves(moves);

    // A fixed-depth search starts from a clean slate so that its node count
    // depends only on the position (bench relies on this).
    clearTranspositionTable();
    clearOrderingTables();

    nodesSearched = 0;
    searchStopped = false;
    useDeadline = false;
    searchRootIndex = positionHistoryIndex();

    return searchRoot(moves, depth, whiteToMove, bestMove, bestScore)
           == RootResult::Complete;
}


static bool sameMove(const Move& a, const Move& b) {
    return a.fromRow == b.fromRow && a.fromCol == b.fromCol &&
           a.toRow == b.toRow && a.toCol == b.toCol &&
           a.promotion == b.promotion;
}


// One UCI "info" line. The score is already from the side to move's
// point of view, which is what UCI wants.
static void printInfoLine(
    int depth,
    int score,
    const Move& best,
    long long elapsedMs
) {

    std::cout << "info depth " << depth << " score ";

    if (std::abs(score) >= MATE_BOUND) {
        // |score| = MATE_SCORE - plies, so the mate is that many plies away.
        const int plies = MATE_SCORE - std::abs(score);
        const int movesToMate = (plies + 1) / 2;
        std::cout << "mate " << (score > 0 ? movesToMate : -movesToMate);
    }
    else {
        std::cout << "cp " << score;
    }

    const unsigned long long nps =
        elapsedMs > 0 ? nodesSearched * 1000ULL / static_cast<unsigned long long>(elapsedMs) : 0;

    std::cout << " nodes " << nodesSearched
              << " nps " << nps
              << " time " << elapsedMs
              << " pv " << moveToUCI(best)
              << std::endl;
}


bool iterativeSearch(
    bool whiteToMove,
    const SearchLimits& limits,
    Move& bestMove,
    int& bestScore,
    int& depthReached,
    bool printInfo
) {

    std::vector<Move> moves = generateLegalMoves(whiteToMove);

    depthReached = 0;

    if (moves.empty()) {
        return false;
    }

    // Start with the most promising moves (captures) first.
    orderMoves(moves);

    // The transposition table is deliberately kept between searches: positions
    // from the previous move are still valid and are often reached again.
    // Killers and history are per-search.
    clearOrderingTables();

    nodesSearched = 0;
    searchStopped = false;
    searchRootIndex = positionHistoryIndex();

    const auto start = std::chrono::steady_clock::now();
    searchDeadline = start + std::chrono::milliseconds(limits.moveTimeMs);

    // Fallback; depth 1 is never interrupted so a real result always replaces it.
    bestMove = moves[0];
    bestScore = 0;

    for (int depth = 1; depth <= limits.maxDepth; depth++) {

        // Depth 1 is never interrupted, so a move always exists.
        useDeadline = (limits.moveTimeMs > 0) && depth > 1;

        Move depthBest;
        int depthScore = 0;

        const RootResult result = searchRoot(
            moves, depth, whiteToMove, depthBest, depthScore);

        if (result == RootResult::Nothing) {
            break;   // ran out of time before finishing even the first move
        }

        // Out of time mid-iteration. The root moves are searched best-first,
        // so if a later move beat the previous best, it is the better
        // choice: use it rather than throwing the work away.
        if (result == RootResult::Partial) {
            bestMove = depthBest;
            bestScore = depthScore;
            break;
        }

        bestMove = depthBest;
        bestScore = depthScore;
        depthReached = depth;

        // Search the best move first next iteration: alpha-beta prunes
        // far more when the strongest move is examined early.
        auto it = std::find_if(moves.begin(), moves.end(),
            [&](const Move& m) { return sameMove(m, depthBest); });
        std::rotate(moves.begin(), it, it + 1);

        const long long elapsedMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

        if (printInfo) {
            printInfoLine(depth, depthScore, depthBest, elapsedMs);
        }

        // A forced mate (either way) will not change with more depth.
        if (std::abs(depthScore) >= MATE_BOUND) {
            break;
        }

        // Only one legal move: nothing to decide, so under a clock do not
        // waste time. (An explicit "go depth N" still searches to depth N
        // so the reported score is meaningful.)
        if (moves.size() == 1 && limits.moveTimeMs > 0) {
            break;
        }

        // Each depth costs several times the previous one, so if half the
        // budget is gone the next iteration will almost surely not finish.
        if (limits.moveTimeMs > 0 && elapsedMs * 2 >= limits.moveTimeMs) {
            break;
        }
    }

    useDeadline = false;
    return true;
}
