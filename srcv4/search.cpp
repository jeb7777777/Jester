#include "search.h"
#include "board.h"
#include "evaluation.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
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

// Counts the number of search positions visited.
unsigned long long nodesSearched = 0;

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

// ---------------------------------------------------------------------
// Quiescence search
// ---------------------------------------------------------------------

// Called instead of evaluating when the normal search runs out of depth.
// Keeps playing forcing moves (captures, queen promotions) until the
// position is "quiet", so the evaluation is never taken in the middle of
// a capture exchange.
static int quiescence(bool whiteToMove, int alpha, int beta, int ply) {

    nodesSearched++;

    if (shouldStop()) {
        return 0;
    }

    if (ply >= MAX_PLY - 1) {
        return evaluateRelative(whiteToMove);   // safety net, never hit in practice
    }

    const bool inCheck = isKingInCheck(whiteToMove);

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
static int negamax(int depth, bool whiteToMove, int alpha, int beta, int ply) {

    // Out of depth: resolve the capture exchanges instead of evaluating.
    if (depth <= 0) {
        return quiescence(whiteToMove, alpha, beta, ply);
    }

    nodesSearched++;

    if (shouldStop()) {
        return 0;
    }

    if (ply >= MAX_PLY - 1) {
        return evaluateRelative(whiteToMove);   // safety net, never hit in practice
    }

    // Pseudo-legal moves only. Whether each one is truly legal is decided
    // lazily, when (and if) the search reaches it. After a cutoff the rest
    // are never tested, which is where most of the time used to go.
    MoveList& list = movePool[ply];
    int* scores = scorePool[ply];

    list.clear();
    generatePseudoLegalMoves(whiteToMove, list);
    scoreMoves(list, scores);

    const bool inCheck = isKingInCheck(whiteToMove);

    int best = -INF;
    int legalMoves = 0;

    for (int i = 0; i < list.count; i++) {

        pickBest(list, scores, i);
        const Move move = list[i];

        if (isCastlingMove(move) && !castlingIsSafe(move, whiteToMove, inCheck)) {
            continue;
        }

        UndoInfo undo;
        applyMove(move, whiteToMove, undo);

        if (isKingInCheck(whiteToMove)) {
            unmakeMove(move, undo);   // illegal: leaves our king attacked
            continue;
        }

        legalMoves++;

        const int score = -negamax(depth - 1, !whiteToMove, -beta, -alpha, ply + 1);

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
            break;      // the opponent will never allow this line
        }
    }

    // No legal move: checkmate or stalemate.
    if (legalMoves == 0) {
        return inCheck ? -(MATE_SCORE - ply) : 0;
    }

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

        const int score = -negamax(depth - 1, !whiteToMove, -beta, -alpha, 1);

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

    nodesSearched = 0;
    searchStopped = false;
    useDeadline = false;

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

    nodesSearched = 0;
    searchStopped = false;

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
