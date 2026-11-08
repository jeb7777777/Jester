#include <cstdlib>
#include <string>

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

    uciLoop();
    return 0;
}
