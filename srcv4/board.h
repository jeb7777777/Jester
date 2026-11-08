#ifndef BOARD_H
#define BOARD_H

#include <iosfwd>
#include <vector>
#include <string>

#include "move.h"

// board[0] is rank 8 (Black's back rank), board[7] is rank 1.
// Upper case = White, lower case = Black, '.' = empty.
extern char board[8][8];

// Global information belonging to the current board position.
extern PositionState positionState;

// ---- Setting up and printing positions ----
void initializeBoard();
bool loadFEN(const std::string& fen, bool& whiteToMove);
std::string toFEN(bool whiteToMove);
void printBoard(std::ostream& out);

// ---- Small helpers ----
bool insideBoard(int row, int col);
bool sameSide(char a, char b);

// ---- Fixed-size move list ----
// Holds moves without touching the heap. Used on the search's hot path,
// where allocating a std::vector at every node is measurably slow.
const int MOVELIST_CAPACITY = 320;

struct MoveList {
    Move moves[MOVELIST_CAPACITY];
    int count = 0;

    void clear() { count = 0; }
    int size() const { return count; }

    // Silently ignores moves beyond capacity (no legal chess position
    // comes close to 320 pseudo-legal moves).
    void push_back(const Move& m) {
        if (count < MOVELIST_CAPACITY) {
            moves[count++] = m;
        }
    }

    Move& operator[](int i) { return moves[i]; }
    const Move& operator[](int i) const { return moves[i]; }
};

// ---- Attacks and check ----
bool isSquareAttacked(int row, int col, bool byWhite);
bool isKingInCheck(bool whiteKing);

// ---- Move generation ----
// Pseudo-legal: obeys how pieces move (including castling structure,
// en passant and promotion) but may leave the mover's own king in check.
//
// With capturesOnly, only "forcing" moves are produced: captures (including
// en passant) and queen promotions. Used by quiescence search.
void generatePseudoLegalMoves(bool whiteMoving, std::vector<Move>& out,
                              bool capturesOnly = false);

// Same generator, writing into a fixed-size list (no heap allocation).
void generatePseudoLegalMoves(bool whiteMoving, MoveList& out,
                              bool capturesOnly = false);

// For filtering pseudo-legal moves into legal ones one at a time:
// 1. A king moving two squares sideways is castling (call BEFORE applying).
// 2. Castling is only allowed if the king is not in check and does not pass
//    through an attacked square. (Landing on an attacked square is caught
//    by the usual "is my king attacked after the move" test.)
bool isCastlingMove(const Move& move);
bool castlingIsSafe(const Move& move, bool whiteMoving, bool inCheck);

// Fully legal moves. This is the single definition of "legal".
std::vector<Move> generateLegalMoves(bool whiteMoving, bool capturesOnly = false);

std::string moveToUCI(const Move& move);

// Turns text such as "e2e4" or "e7e8q" into a Move, but only if it is a
// legal move in the current position. Returns false otherwise.
bool parseMove(const std::string& text, bool whiteMoving, Move& out);

// ---- Making and unmaking moves ----
// Executes a move that is ALREADY known to be legal (for example one that
// came from generateLegalMoves). No validation. Fast; used by search/perft.
void applyMove(const Move& move, bool whiteMoving, UndoInfo& undo);

// Exactly reverses applyMove.
void unmakeMove(const Move& move, const UndoInfo& undo);

// Validates first, then applies. For moves from outside (user, GUI).
bool makeMove(const Move& move, bool whiteMoving, UndoInfo& undo);

struct GameState {
    bool whiteToMove = true;
    bool gameOver = false;
};

bool playMove(const Move& move, GameState& state, UndoInfo& undo);

// 0 = game continues, 1 = checkmate, 2 = stalemate.
int getGameResult(bool whiteToMove);

#endif
