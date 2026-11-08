#include "board.h"
#include "evaluation.h"

#include <cctype>

// ---------------------------------------------------------------------
// Piece-square tables
// ---------------------------------------------------------------------
//
// Each table gives a bonus (or penalty), in centipawns, for a piece of
// that type standing on that square. They come from the "Simplified
// Evaluation Function" by Tomasz Michniewski:
//   https://www.chessprogramming.org/Simplified_Evaluation_Function
//
// The tables are written from White's point of view with rank 8 at the
// top, which matches board[0] = rank 8. So for a White piece on
// board[row][col] we read table[row][col]; for a Black piece we read the
// vertically flipped square table[7 - row][col].

static const int PAWN_TABLE[8][8] = {
    {  0,  0,  0,  0,  0,  0,  0,  0},
    { 50, 50, 50, 50, 50, 50, 50, 50},
    { 10, 10, 20, 30, 30, 20, 10, 10},
    {  5,  5, 10, 25, 25, 10,  5,  5},
    {  0,  0,  0, 20, 20,  0,  0,  0},
    {  5, -5,-10,  0,  0,-10, -5,  5},
    {  5, 10, 10,-20,-20, 10, 10,  5},
    {  0,  0,  0,  0,  0,  0,  0,  0}
};

static const int KNIGHT_TABLE[8][8] = {
    {-50,-40,-30,-30,-30,-30,-40,-50},
    {-40,-20,  0,  0,  0,  0,-20,-40},
    {-30,  0, 10, 15, 15, 10,  0,-30},
    {-30,  5, 15, 20, 20, 15,  5,-30},
    {-30,  0, 15, 20, 20, 15,  0,-30},
    {-30,  5, 10, 15, 15, 10,  5,-30},
    {-40,-20,  0,  5,  5,  0,-20,-40},
    {-50,-40,-30,-30,-30,-30,-40,-50}
};

static const int BISHOP_TABLE[8][8] = {
    {-20,-10,-10,-10,-10,-10,-10,-20},
    {-10,  0,  0,  0,  0,  0,  0,-10},
    {-10,  0,  5, 10, 10,  5,  0,-10},
    {-10,  5,  5, 10, 10,  5,  5,-10},
    {-10,  0, 10, 10, 10, 10,  0,-10},
    {-10, 10, 10, 10, 10, 10, 10,-10},
    {-10,  5,  0,  0,  0,  0,  5,-10},
    {-20,-10,-10,-10,-10,-10,-10,-20}
};

static const int ROOK_TABLE[8][8] = {
    {  0,  0,  0,  0,  0,  0,  0,  0},
    {  5, 10, 10, 10, 10, 10, 10,  5},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    {  0,  0,  0,  5,  5,  0,  0,  0}
};

static const int QUEEN_TABLE[8][8] = {
    {-20,-10,-10, -5, -5,-10,-10,-20},
    {-10,  0,  0,  0,  0,  0,  0,-10},
    {-10,  0,  5,  5,  5,  5,  0,-10},
    { -5,  0,  5,  5,  5,  5,  0, -5},
    {  0,  0,  5,  5,  5,  5,  0, -5},
    {-10,  5,  5,  5,  5,  5,  0,-10},
    {-10,  0,  5,  0,  0,  0,  0,-10},
    {-20,-10,-10, -5, -5,-10,-10,-20}
};

// In the middlegame the king should hide behind its pawns...
static const int KING_MIDDLEGAME_TABLE[8][8] = {
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-20,-30,-30,-40,-40,-30,-30,-20},
    {-10,-20,-20,-20,-20,-20,-20,-10},
    { 20, 20,  0,  0,  0,  0, 20, 20},
    { 20, 30, 10,  0,  0, 10, 30, 20}
};

// ...but in the endgame it is a fighting piece and belongs in the centre.
static const int KING_ENDGAME_TABLE[8][8] = {
    {-50,-40,-30,-20,-20,-30,-40,-50},
    {-30,-20,-10,  0,  0,-10,-20,-30},
    {-30,-10, 20, 30, 30, 20,-10,-30},
    {-30,-10, 30, 40, 40, 30,-10,-30},
    {-30,-10, 30, 40, 40, 30,-10,-30},
    {-30,-10, 20, 30, 30, 20,-10,-30},
    {-30,-30,  0,  0,  0,  0,-30,-30},
    {-50,-30,-30,-30,-30,-30,-30,-50}
};

// ---------------------------------------------------------------------
// Material
// ---------------------------------------------------------------------

int getPieceValue(char piece) {

    switch (std::tolower(static_cast<unsigned char>(piece))) {

        case 'p': return 100;
        case 'n': return 320;
        case 'b': return 330;
        case 'r': return 500;
        case 'q': return 900;
        case 'k': return 0;   // the king's worth comes from its table

        default: return 0;
    }
}

// Bonus for a non-king piece on a square. 'type' is lower case.
static int placementBonus(char type, int row, int col, bool white) {

    // Black reads the tables upside down.
    const int r = white ? row : 7 - row;

    switch (type) {
        case 'p': return PAWN_TABLE[r][col];
        case 'n': return KNIGHT_TABLE[r][col];
        case 'b': return BISHOP_TABLE[r][col];
        case 'r': return ROOK_TABLE[r][col];
        case 'q': return QUEEN_TABLE[r][col];
        default:  return 0;
    }
}

// Per-side piece counts used to decide whether the endgame has begun.
struct SideCounts {
    int queens = 0;
    int rooks = 0;
    int minors = 0;
};

// The endgame has begun if both sides have no queens, or every side that
// still has a queen has no rooks and at most one minor piece.
static bool isEndgame(const SideCounts& white, const SideCounts& black) {

    if (white.queens == 0 && black.queens == 0) {
        return true;
    }

    const bool whiteQuiet = white.queens == 0 ||
                            (white.rooks == 0 && white.minors <= 1);
    const bool blackQuiet = black.queens == 0 ||
                            (black.rooks == 0 && black.minors <= 1);

    return whiteQuiet && blackQuiet;
}

// Score in centipawns from WHITE's point of view.
int evaluatePosition() {

    int score = 0;

    SideCounts white, black;
    int whiteKingRow = -1, whiteKingCol = -1;
    int blackKingRow = -1, blackKingCol = -1;

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {

            const char piece = board[row][col];

            if (piece == '.') {
                continue;
            }

            const bool isWhite = std::isupper(static_cast<unsigned char>(piece)) != 0;
            const char type = static_cast<char>(piece | 32);   // lower case

            if (type == 'k') {
                // Scored after the loop, once we know the game phase.
                if (isWhite) { whiteKingRow = row; whiteKingCol = col; }
                else         { blackKingRow = row; blackKingCol = col; }
                continue;
            }

            SideCounts& counts = isWhite ? white : black;
            if (type == 'q') counts.queens++;
            else if (type == 'r') counts.rooks++;
            else if (type == 'n' || type == 'b') counts.minors++;

            const int value = getPieceValue(piece) +
                              placementBonus(type, row, col, isWhite);

            score += isWhite ? value : -value;
        }
    }

    const int (*kingTable)[8] =
        isEndgame(white, black) ? KING_ENDGAME_TABLE : KING_MIDDLEGAME_TABLE;

    if (whiteKingRow >= 0) {
        score += kingTable[whiteKingRow][whiteKingCol];
    }
    if (blackKingRow >= 0) {
        score -= kingTable[7 - blackKingRow][blackKingCol];
    }

    return score;
}
