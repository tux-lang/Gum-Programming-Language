#include <iostream>

#include "ConsoleControl.h"
#include "Runner.h"
#include "Error.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: sekc <file.sek>\n";
        return 1;
    }

    try {
        sek::configureConsoleEncoding();
        std::cout.setf(std::ios::unitbuf);
        std::cerr.setf(std::ios::unitbuf);
        sek::installConsoleControlHandler();
        sek::Runner runner;
        return runner.runFile(argv[1]);
    } catch (const sek::SekError& error) {
        std::cerr << "gum error: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Unexpected error: " << error.what() << '\n';
        return 1;
    }
}
