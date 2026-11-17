#include <cstdlib>
#include <string>
#include "bench.h"
#include "perft.h"
#include "uci.h"

// Usage:
//   engine                 talk UCI on stdin/stdout (what a chess GUI uses)
//   engine --perft [N]     run the perft self-test suite up to depth N
int main(int argc, char** argv) {

    if (argc > 1 && std::string(argv[1]) == "--perft") {
        const int depth = argc > 2 ? std::atoi(argv[2]) : 4;
        return runPerftSuite(depth) ? 0 : 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--bench") {
        runBench(argc > 2 ? std::atoi(argv[2]) : 5);
        return 0;
    }

    uciLoop();
    return 0;
}
