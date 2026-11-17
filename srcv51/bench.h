#ifndef BENCH_H
#define BENCH_H

// Searches a fixed set of positions to a fixed depth (no clock involved) and
// prints per-position and total node counts, time and nodes per second.
// Returns the total node count.
//
// Two uses:
//   * Speed: total nodes / time = nps. Compare builds on the same machine.
//   * Signature: the total node count is deterministic. A change that is
//     only supposed to make the engine FASTER (refactor, cheaper check test)
//     must leave it identical; if it moves, the search behaviour changed.
unsigned long long runBench(int depth);

#endif
