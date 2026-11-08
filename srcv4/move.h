#ifndef MOVE_H
#define MOVE_H

// Describes one chess move.
struct Move {
    int fromRow = 0;
    int fromCol = 0;
    int toRow = 0;
    int toCol = 0;

    // '\0' means no promotion.
    // Otherwise: 'q', 'r', 'b', or 'n'.
    char promotion = '\0';
};

// Information about the current position's special rules.
struct PositionState {
    // Castling rights:
    // Bit 0: White kingside
    // Bit 1: White queenside
    // Bit 2: Black kingside
    // Bit 3: Black queenside
    unsigned char castlingRights = 15;

    // En passant target square.
    // -1 means there is no target.
    int enPassantRow = -1;
    int enPassantCol = -1;

    // Draw-rule counters.
    int halfmoveClock = 0;
    int fullmoveNumber = 1;
};

// Information needed to undo a move.
struct UndoInfo {
    char movedPiece = '.';
    char capturedPiece = '.';

    // Save the previous special-rule information.
    PositionState previousPositionState;

    // Square the captured piece stood on. This differs from the
    // destination square only for en passant.
    int capturedRow = -1;
    int capturedCol = -1;

    // Castling information.
    bool wasCastling = false;
    int rookFromCol = -1;
    int rookToCol = -1;
};

#endif
