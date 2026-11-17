#ifndef PERFT_H
#define PERFT_H

// Counts the leaf nodes of the legal-move tree.
// With selfCheck on, every unmakeMove is verified to restore the board
// and position state exactly, and the first violation is reported.
unsigned long long perft(int depth, bool whiteToMove, bool selfCheck = false);

// Prints the node count below each root move (for comparing with a
// reference engine) and the total.
void perftDivide(int depth, bool whiteToMove);

// Runs the standard test positions up to maxDepth. Returns true if every
// count matched the published value.
bool runPerftSuite(int maxDepth);

#endif
