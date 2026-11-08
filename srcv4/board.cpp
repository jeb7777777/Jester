#include "board.h"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>

char board[8][8];
PositionState positionState;

// ---------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------

static inline bool isWhitePiece(char p) { return p >= 'A' && p <= 'Z'; }
static inline bool isBlackPiece(char p) { return p >= 'a' && p <= 'z'; }

// True if p is a piece belonging to the opponent of the given side.
static inline bool isEnemyOf(bool white, char p) {
    return white ? isBlackPiece(p) : isWhitePiece(p);
}

// Lower-case version of a piece letter ('.' stays '.').
static inline char lower(char p) { return static_cast<char>(p | 32); }

bool insideBoard(int row, int col) {
    return row >= 0 && row < 8 && col >= 0 && col < 8;
}

bool sameSide(char a, char b) {
    if (a == '.' || b == '.') {
        return false;
    }
    return std::isupper(static_cast<unsigned char>(a)) ==
           std::isupper(static_cast<unsigned char>(b));
}

// Movement directions, written as (row, column) offsets.
static const int KNIGHT_DR[8] = {-2, -2, -1, -1,  1,  1,  2,  2};
static const int KNIGHT_DC[8] = {-1,  1, -2,  2, -2,  2, -1,  1};

static const int KING_DR[8]   = {-1, -1, -1,  0,  0,  1,  1,  1};
static const int KING_DC[8]   = {-1,  0,  1, -1,  1, -1,  0,  1};

static const int ORTHO_DR[4]  = {-1,  1,  0,  0};
static const int ORTHO_DC[4]  = { 0,  0, -1,  1};

static const int DIAG_DR[4]   = {-1, -1,  1,  1};
static const int DIAG_DC[4]   = {-1,  1, -1,  1};

// ---------------------------------------------------------------------
// Setting up and printing positions
// ---------------------------------------------------------------------

void initializeBoard() {
    positionState = PositionState{};
    const char startingPosition[8][8] = {
        {'r','n','b','q','k','b','n','r'},
        {'p','p','p','p','p','p','p','p'},
        {'.','.','.','.','.','.','.','.'},
        {'.','.','.','.','.','.','.','.'},
        {'.','.','.','.','.','.','.','.'},
        {'.','.','.','.','.','.','.','.'},
        {'P','P','P','P','P','P','P','P'},
        {'R','N','B','Q','K','B','N','R'}
    };

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            board[row][col] = startingPosition[row][col];
        }
    }
}

void printBoard(std::ostream& out) {

    out << "\n";

    for (int row = 0; row < 8; row++) {

        out << 8 - row << " ";

        for (int col = 0; col < 8; col++) {
            out << board[row][col] << " ";
        }

        out << "\n";
    }

    out << "  a b c d e f g h\n\n";
}

bool loadFEN(const std::string& fen, bool& whiteToMove) {

    std::istringstream in(fen);
    std::string placement, side, castling, ep;
    int halfmove = 0, fullmove = 1;

    if (!(in >> placement >> side >> castling >> ep)) {
        return false;
    }
    in >> halfmove >> fullmove;   // optional fields

    // Parse into a temporary board so a bad FEN can't corrupt the real one.
    char newBoard[8][8];
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            newBoard[r][c] = '.';

    const std::string pieces = "pnbrqkPNBRQK";
    int row = 0, col = 0;
    int whiteKings = 0, blackKings = 0;

    for (char ch : placement) {
        if (ch == '/') {
            if (col != 8) return false;
            row++;
            col = 0;
        }
        else if (ch >= '1' && ch <= '8') {
            col += ch - '0';
            if (col > 8) return false;
        }
        else if (pieces.find(ch) != std::string::npos) {
            if (row > 7 || col > 7) return false;
            // Pawns can never stand on the first or last rank.
            if ((ch == 'P' || ch == 'p') && (row == 0 || row == 7)) {
                return false;
            }
            if (ch == 'K') whiteKings++;
            if (ch == 'k') blackKings++;
            newBoard[row][col++] = ch;
        }
        else {
            return false;
        }
    }
    if (row != 7 || col != 8) return false;
    if (whiteKings != 1 || blackKings != 1) return false;

    bool white;
    if (side == "w")      white = true;
    else if (side == "b") white = false;
    else return false;

    unsigned char rights = 0;
    for (char ch : castling) {
        if      (ch == 'K') rights |= 1;
        else if (ch == 'Q') rights |= 2;
        else if (ch == 'k') rights |= 4;
        else if (ch == 'q') rights |= 8;
        else if (ch != '-') return false;
    }

    int epRow = -1, epCol = -1;
    if (ep != "-") {
        if (ep.size() != 2 || ep[0] < 'a' || ep[0] > 'h' ||
            ep[1] < '1' || ep[1] > '8') {
            return false;
        }
        epCol = ep[0] - 'a';
        epRow = '8' - ep[1];
    }

    // Everything parsed: commit.
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            board[r][c] = newBoard[r][c];

    positionState = PositionState{};
    positionState.castlingRights = rights;
    positionState.enPassantRow = epRow;
    positionState.enPassantCol = epCol;
    positionState.halfmoveClock = halfmove;
    positionState.fullmoveNumber = fullmove;

    whiteToMove = white;
    return true;
}

std::string toFEN(bool whiteToMove) {

    std::string fen;

    for (int r = 0; r < 8; r++) {
        int empty = 0;
        for (int c = 0; c < 8; c++) {
            char p = board[r][c];
            if (p == '.') {
                empty++;
                continue;
            }
            if (empty > 0) {
                fen += static_cast<char>('0' + empty);
                empty = 0;
            }
            fen += p;
        }
        if (empty > 0) {
            fen += static_cast<char>('0' + empty);
        }
        if (r < 7) {
            fen += '/';
        }
    }

    fen += whiteToMove ? " w " : " b ";

    std::string rights;
    if (positionState.castlingRights & 1) rights += 'K';
    if (positionState.castlingRights & 2) rights += 'Q';
    if (positionState.castlingRights & 4) rights += 'k';
    if (positionState.castlingRights & 8) rights += 'q';
    fen += rights.empty() ? "-" : rights;

    fen += ' ';
    if (positionState.enPassantRow >= 0) {
        fen += static_cast<char>('a' + positionState.enPassantCol);
        fen += static_cast<char>('8' - positionState.enPassantRow);
    } else {
        fen += '-';
    }

    fen += ' ' + std::to_string(positionState.halfmoveClock);
    fen += ' ' + std::to_string(positionState.fullmoveNumber);
    return fen;
}

// ---------------------------------------------------------------------
// Attacks and check
// ---------------------------------------------------------------------

// Works outward FROM the target square: instead of asking every enemy
// piece "do you reach this square?", we look at the few squares from
// which each kind of piece could reach it.
bool isSquareAttacked(int row, int col, bool byWhite) {

    if (!insideBoard(row, col)) {
        return false;
    }

    // Pawns. A white pawn attacks diagonally toward row-1, so it must be
    // standing on row+1 to hit us; a black pawn must stand on row-1.
    const char pawn = byWhite ? 'P' : 'p';
    const int pawnRow = row + (byWhite ? 1 : -1);
    if (pawnRow >= 0 && pawnRow < 8) {
        if (col > 0 && board[pawnRow][col - 1] == pawn) return true;
        if (col < 7 && board[pawnRow][col + 1] == pawn) return true;
    }

    // Knights.
    const char knight = byWhite ? 'N' : 'n';
    for (int i = 0; i < 8; i++) {
        int r = row + KNIGHT_DR[i];
        int c = col + KNIGHT_DC[i];
        if (insideBoard(r, c) && board[r][c] == knight) return true;
    }

    // Enemy king next to us.
    const char king = byWhite ? 'K' : 'k';
    for (int i = 0; i < 8; i++) {
        int r = row + KING_DR[i];
        int c = col + KING_DC[i];
        if (insideBoard(r, c) && board[r][c] == king) return true;
    }

    // Sliding pieces: walk each ray until something blocks it.
    const char rook   = byWhite ? 'R' : 'r';
    const char bishop = byWhite ? 'B' : 'b';
    const char queen  = byWhite ? 'Q' : 'q';

    for (int d = 0; d < 4; d++) {
        int r = row + ORTHO_DR[d];
        int c = col + ORTHO_DC[d];
        while (insideBoard(r, c)) {
            char p = board[r][c];
            if (p != '.') {
                if (p == rook || p == queen) return true;
                break;
            }
            r += ORTHO_DR[d];
            c += ORTHO_DC[d];
        }
    }

    for (int d = 0; d < 4; d++) {
        int r = row + DIAG_DR[d];
        int c = col + DIAG_DC[d];
        while (insideBoard(r, c)) {
            char p = board[r][c];
            if (p != '.') {
                if (p == bishop || p == queen) return true;
                break;
            }
            r += DIAG_DR[d];
            c += DIAG_DC[d];
        }
    }

    return false;
}

bool isKingInCheck(bool whiteKing) {

    const char king = whiteKing ? 'K' : 'k';

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            if (board[r][c] == king) {
                return isSquareAttacked(r, c, !whiteKing);
            }
        }
    }

    return false;   // no king on the board
}

// ---------------------------------------------------------------------
// Pseudo-legal move generation
// ---------------------------------------------------------------------

// The generator is written once and works with any container that has
// push_back: std::vector<Move> (general use) or MoveList (fast, no heap).
template <class Out>
static inline void pushMove(Out& out,
                            int fromRow, int fromCol,
                            int toRow, int toCol,
                            char promotion = '\0') {
    Move m;
    m.fromRow = fromRow;
    m.fromCol = fromCol;
    m.toRow = toRow;
    m.toCol = toCol;
    m.promotion = promotion;
    out.push_back(m);
}

// A pawn move that reaches the last rank becomes four moves.
template <class Out>
static inline void pushPawnMove(Out& out,
                                int fromRow, int fromCol,
                                int toRow, int toCol,
                                int promotionRow) {
    if (toRow == promotionRow) {
        pushMove(out, fromRow, fromCol, toRow, toCol, 'q');
        pushMove(out, fromRow, fromCol, toRow, toCol, 'r');
        pushMove(out, fromRow, fromCol, toRow, toCol, 'b');
        pushMove(out, fromRow, fromCol, toRow, toCol, 'n');
    } else {
        pushMove(out, fromRow, fromCol, toRow, toCol);
    }
}

// Rays for bishops, rooks and queens.
template <class Out>
static void generateSlides(Out& out, bool white,
                           int row, int col,
                           const int* dr, const int* dc, int directions,
                           bool capturesOnly) {
    for (int d = 0; d < directions; d++) {
        int r = row + dr[d];
        int c = col + dc[d];

        while (insideBoard(r, c)) {
            char target = board[r][c];

            if (target == '.') {
                if (!capturesOnly) {
                    pushMove(out, row, col, r, c);
                }
            }
            else {
                // Enemy piece: can capture it (never the king); stop here.
                if (isEnemyOf(white, target) && lower(target) != 'k') {
                    pushMove(out, row, col, r, c);
                }
                break;
            }

            r += dr[d];
            c += dc[d];
        }
    }
}

template <class Out>
static void generateImpl(bool white, Out& out, bool capturesOnly) {

    const int direction    = white ? -1 : 1;   // which way this side's pawns walk
    const int startRow     = white ? 6 : 1;
    const int promotionRow = white ? 0 : 7;
    const int homeRow      = white ? 7 : 0;

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {

            const char piece = board[row][col];

            if (piece == '.' || isWhitePiece(piece) != white) {
                continue;
            }

            switch (lower(piece)) {

                case 'p': {
                    const int r1 = row + direction;
                    if (r1 < 0 || r1 > 7) break;

                    // Pushes. In captures-only mode the only push that counts
                    // as forcing is a promotion (queen only; underpromotion
                    // is rare and not worth the extra nodes here).
                    if (board[r1][col] == '.') {
                        if (!capturesOnly) {
                            pushPawnMove(out, row, col, r1, col, promotionRow);

                            if (row == startRow && board[r1 + direction][col] == '.') {
                                pushMove(out, row, col, r1 + direction, col);
                            }
                        }
                        else if (r1 == promotionRow) {
                            pushMove(out, row, col, r1, col, 'q');
                        }
                    }

                    // Captures (and en passant).
                    for (int dc = -1; dc <= 1; dc += 2) {
                        const int c = col + dc;
                        if (c < 0 || c > 7) continue;

                        const char target = board[r1][c];

                        if (target == '.') {
                            if (r1 == positionState.enPassantRow &&
                                c == positionState.enPassantCol) {

                                const char beside = board[row][c];
                                if (lower(beside) == 'p' && isEnemyOf(white, beside)) {
                                    pushMove(out, row, col, r1, c);
                                }
                            }
                        }
                        else if (isEnemyOf(white, target) && lower(target) != 'k') {
                            if (capturesOnly && r1 == promotionRow) {
                                pushMove(out, row, col, r1, c, 'q');
                            } else {
                                pushPawnMove(out, row, col, r1, c, promotionRow);
                            }
                        }
                    }
                    break;
                }

                case 'n':
                    for (int i = 0; i < 8; i++) {
                        const int r = row + KNIGHT_DR[i];
                        const int c = col + KNIGHT_DC[i];
                        if (!insideBoard(r, c)) continue;

                        const char target = board[r][c];
                        if ((target == '.' && !capturesOnly) ||
                            (isEnemyOf(white, target) && lower(target) != 'k')) {
                            pushMove(out, row, col, r, c);
                        }
                    }
                    break;

                case 'k': {
                    for (int i = 0; i < 8; i++) {
                        const int r = row + KING_DR[i];
                        const int c = col + KING_DC[i];
                        if (!insideBoard(r, c)) continue;

                        const char target = board[r][c];
                        if ((target == '.' && !capturesOnly) ||
                            (isEnemyOf(white, target) && lower(target) != 'k')) {
                            pushMove(out, row, col, r, c);
                        }
                    }

                    // Castling: structural rules only (right still held,
                    // rook present, squares between empty). King safety is
                    // checked when filtering for legality.
                    if (!capturesOnly && row == homeRow && col == 4) {
                        const unsigned char kingsideBit  = white ? 1 : 4;
                        const unsigned char queensideBit = white ? 2 : 8;
                        const char rook = white ? 'R' : 'r';

                        if ((positionState.castlingRights & kingsideBit) &&
                            board[homeRow][7] == rook &&
                            board[homeRow][5] == '.' &&
                            board[homeRow][6] == '.') {
                            pushMove(out, row, col, row, 6);
                        }

                        if ((positionState.castlingRights & queensideBit) &&
                            board[homeRow][0] == rook &&
                            board[homeRow][1] == '.' &&
                            board[homeRow][2] == '.' &&
                            board[homeRow][3] == '.') {
                            pushMove(out, row, col, row, 2);
                        }
                    }
                    break;
                }

                case 'r':
                    generateSlides(out, white, row, col, ORTHO_DR, ORTHO_DC, 4, capturesOnly);
                    break;

                case 'b':
                    generateSlides(out, white, row, col, DIAG_DR, DIAG_DC, 4, capturesOnly);
                    break;

                case 'q':
                    generateSlides(out, white, row, col, ORTHO_DR, ORTHO_DC, 4, capturesOnly);
                    generateSlides(out, white, row, col, DIAG_DR, DIAG_DC, 4, capturesOnly);
                    break;
            }
        }
    }
}

void generatePseudoLegalMoves(bool white, std::vector<Move>& out,
                              bool capturesOnly) {
    generateImpl(white, out, capturesOnly);
}

void generatePseudoLegalMoves(bool white, MoveList& out,
                              bool capturesOnly) {
    generateImpl(white, out, capturesOnly);
}

// ---------------------------------------------------------------------
// Making and unmaking moves
// ---------------------------------------------------------------------

// Called for the origin AND destination square of every move. If a king
// or rook leaves its home square, or anything lands on a rook's home
// square (a capture), the matching castling rights are gone.
static void updateCastlingRights(int row, int col) {
    unsigned char r = positionState.castlingRights;

    if      (row == 7 && col == 4) r &= 12;  // white king: both
    else if (row == 7 && col == 7) r &= 14;  // white h1 rook
    else if (row == 7 && col == 0) r &= 13;  // white a1 rook
    else if (row == 0 && col == 4) r &= 3;   // black king: both
    else if (row == 0 && col == 7) r &= 11;  // black h8 rook
    else if (row == 0 && col == 0) r &= 7;   // black a8 rook

    positionState.castlingRights = r;
}

void applyMove(const Move& move, bool whiteMoving, UndoInfo& undo) {

    const char piece = board[move.fromRow][move.fromCol];
    const bool isPawn = (piece == 'P' || piece == 'p');
    const bool isKing = (piece == 'K' || piece == 'k');

    // Decide the special-move types BEFORE the board changes.
    // A pawn moving diagonally onto an empty square can only be en passant.
    const bool enPassant = isPawn &&
                           move.fromCol != move.toCol &&
                           board[move.toRow][move.toCol] == '.';
    const bool castling = isKing && std::abs(move.toCol - move.fromCol) == 2;

    // Record everything needed to undo.
    undo.previousPositionState = positionState;
    undo.movedPiece = piece;
    undo.capturedRow = enPassant ? move.fromRow : move.toRow;
    undo.capturedCol = move.toCol;
    undo.capturedPiece = board[undo.capturedRow][undo.capturedCol];
    undo.wasCastling = castling;
    undo.rookFromCol = -1;
    undo.rookToCol = -1;

    if (castling) {
        const bool kingside = move.toCol > move.fromCol;
        undo.rookFromCol = kingside ? 7 : 0;
        undo.rookToCol   = kingside ? 5 : 3;
    }

    // Execute, swapping in the promoted piece if needed.
    char placed = piece;
    if (move.promotion != '\0') {
        placed = whiteMoving
            ? static_cast<char>(std::toupper(static_cast<unsigned char>(move.promotion)))
            : static_cast<char>(std::tolower(static_cast<unsigned char>(move.promotion)));
    }
    board[move.toRow][move.toCol] = placed;
    board[move.fromRow][move.fromCol] = '.';

    if (enPassant) {
        board[undo.capturedRow][undo.capturedCol] = '.';
    }

    if (castling) {
        board[move.fromRow][undo.rookToCol] = board[move.fromRow][undo.rookFromCol];
        board[move.fromRow][undo.rookFromCol] = '.';
    }

    // Update position state.
    positionState.enPassantRow = -1;
    positionState.enPassantCol = -1;

    if (isPawn && std::abs(move.toRow - move.fromRow) == 2) {
        // The square the pawn skipped over.
        positionState.enPassantRow = (move.fromRow + move.toRow) / 2;
        positionState.enPassantCol = move.fromCol;
    }

    updateCastlingRights(move.fromRow, move.fromCol);
    updateCastlingRights(move.toRow, move.toCol);

    if (isPawn || undo.capturedPiece != '.') {
        positionState.halfmoveClock = 0;
    } else {
        positionState.halfmoveClock++;
    }

    if (!whiteMoving) {
        positionState.fullmoveNumber++;
    }
}

void unmakeMove(const Move& move, const UndoInfo& undo) {

    board[move.fromRow][move.fromCol] = undo.movedPiece;           // the pawn again, if promoted
    board[move.toRow][move.toCol] = '.';                           // clear first...
    board[undo.capturedRow][undo.capturedCol] = undo.capturedPiece; // ...then restore the capture

    if (undo.wasCastling) {
        const char rook = isWhitePiece(undo.movedPiece) ? 'R' : 'r';
        board[move.fromRow][undo.rookFromCol] = rook;
        board[move.fromRow][undo.rookToCol] = '.';
    }

    positionState = undo.previousPositionState;
}

// ---------------------------------------------------------------------
// Legal move generation
// ---------------------------------------------------------------------

bool isCastlingMove(const Move& move) {
    const char piece = board[move.fromRow][move.fromCol];
    return (piece == 'K' || piece == 'k') &&
           std::abs(move.toCol - move.fromCol) == 2;
}

bool castlingIsSafe(const Move& move, bool white, bool inCheck) {
    // Not out of check...
    if (inCheck) {
        return false;
    }
    // ...and not through an attacked square.
    const int transitCol = (move.fromCol + move.toCol) / 2;
    return !isSquareAttacked(move.fromRow, transitCol, !white);
    // "Into check" is covered by the king-safety test after the move.
}

std::vector<Move> generateLegalMoves(bool white, bool capturesOnly) {

    std::vector<Move> pseudo;
    pseudo.reserve(64);
    generatePseudoLegalMoves(white, pseudo, capturesOnly);

    std::vector<Move> legal;
    legal.reserve(pseudo.size());

    const bool inCheck = isKingInCheck(white);

    for (const Move& move : pseudo) {

        if (isCastlingMove(move) && !castlingIsSafe(move, white, inCheck)) {
            continue;
        }

        // Play it, see whether our own king is attacked, take it back.
        UndoInfo undo;
        applyMove(move, white, undo);
        const bool leavesKingInCheck = isKingInCheck(white);
        unmakeMove(move, undo);

        if (!leavesKingInCheck) {
            legal.push_back(move);
        }
    }

    return legal;
}

std::string moveToUCI(const Move& move) {

    std::string result;

    result += static_cast<char>('a' + move.fromCol);
    result += static_cast<char>('8' - move.fromRow);
    result += static_cast<char>('a' + move.toCol);
    result += static_cast<char>('8' - move.toRow);

    if (move.promotion != '\0') {
        result += move.promotion;   // e.g. "a7a8q"
    }

    return result;
}

// "Legal" means "the generator produces it", so there is exactly one
// definition of legality in the whole program.
static bool findLegalMove(int fromRow, int fromCol, int toRow, int toCol,
                          char promotion, bool white, Move& out) {

    const std::vector<Move> legal = generateLegalMoves(white);

    for (const Move& m : legal) {
        if (m.fromRow == fromRow && m.fromCol == fromCol &&
            m.toRow == toRow && m.toCol == toCol &&
            m.promotion == promotion) {
            out = m;
            return true;
        }
    }

    return false;
}

bool parseMove(const std::string& text, bool white, Move& out) {

    if (text.size() != 4 && text.size() != 5) {
        return false;
    }

    const char f = text[0], fr = text[1], t = text[2], tr = text[3];

    if (f < 'a' || f > 'h' || t < 'a' || t > 'h' ||
        fr < '1' || fr > '8' || tr < '1' || tr > '8') {
        return false;
    }

    char promotion = '\0';
    if (text.size() == 5) {
        promotion = static_cast<char>(std::tolower(static_cast<unsigned char>(text[4])));
        if (promotion != 'q' && promotion != 'r' &&
            promotion != 'b' && promotion != 'n') {
            return false;
        }
    }

    return findLegalMove('8' - fr, f - 'a', '8' - tr, t - 'a',
                         promotion, white, out);
}

bool makeMove(const Move& move, bool whiteMoving, UndoInfo& undo) {

    Move legal;
    if (!findLegalMove(move.fromRow, move.fromCol, move.toRow, move.toCol,
                       move.promotion, whiteMoving, legal)) {
        return false;
    }

    applyMove(legal, whiteMoving, undo);
    return true;
}

bool playMove(const Move& move, GameState& state, UndoInfo& undo) {

    if (state.gameOver) {
        return false;
    }

    if (!makeMove(move, state.whiteToMove, undo)) {
        return false;
    }

    // Switch turns.
    state.whiteToMove = !state.whiteToMove;

    return true;
}

int getGameResult(bool whiteToMove) {

    std::vector<Move> moves = generateLegalMoves(whiteToMove);

    if (!moves.empty()) {
        return 0; // Game continues.
    }

    if (isKingInCheck(whiteToMove)) {
        return 1; // Checkmate.
    }

    return 2; // Stalemate.
}
