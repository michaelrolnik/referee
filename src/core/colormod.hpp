#pragma once

#include <ostream>
#include <iostream>
#include <cstdlib>
#include <unistd.h>

//  refer https://stackoverflow.com/questions/2616906/how-do-i-output-coloured-text-to-a-linux-terminal
namespace Color {
    enum Code {
        FG_DEFAULT      = 39,
        FG_BLACK        = 30,
        FG_RED          = 31,
        FG_GREEN        = 32,
        FG_YELLOW       = 33,
        FG_BLUE         = 34,
        FG_MAGENTA      = 35,
        FG_CYAN         = 36,
        FG_LIGHT_GRAY   = 37,
        FG_DARK_GRAY    = 90,
        FG_LIGHT_RED    = 91,
        FG_LIGHT_GREEN  = 92,
        FG_LIGHT_YELLOW = 93,
        FG_LIGHT_BLUE   = 94,
        FG_LIGHT_MAGENTA= 95,
        FG_LIGHT_CYAN   = 96,
        FG_WHITE        = 97,
        BG_RED          = 41,
        BG_GREEN        = 42,
        BG_BLUE         = 44,
        BG_DEFAULT      = 49
    };

    //  Whether an escape code written to `os` would reach a terminal. A colour
    //  code is a terminal control sequence; in a file or a pipe it is garbage
    //  that breaks grep, diff and the tests, so it must be suppressed there.
    //
    //  An ostream does not expose its file descriptor, but the process's own
    //  cout/cerr can be recognised by their stream buffer -- and once matched,
    //  isatty on the corresponding fd answers the real question, so
    //  `referee ... > out.txt` (still cout, but redirected) correctly gets no
    //  colour too. Any other stream -- a file, or the stringstream the tests
    //  print into -- is never a terminal. NO_COLOR (https://no-color.org) is
    //  the universal opt-out and wins over everything.
    inline bool isTerminal(std::ostream& os) {
        if (std::getenv("NO_COLOR") != nullptr)
        {
            return false;
        }
        if (os.rdbuf() == std::cout.rdbuf())
        {
            return isatty(STDOUT_FILENO) != 0;
        }
        if (os.rdbuf() == std::cerr.rdbuf() || os.rdbuf() == std::clog.rdbuf())
        {
            return isatty(STDERR_FILENO) != 0;
        }
        return false;
    }

    class Modifier {
        Code code;
    public:
        Modifier(Code pCode) : code(pCode) {}
        friend std::ostream&
        operator<<(std::ostream& os, const Modifier& mod) {
            if (isTerminal(os))
            {
                os << "\033[" << mod.code << "m";
            }
            return os;
        }
    };
}
