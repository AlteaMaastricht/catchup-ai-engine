#include "ui.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

void print_menu() {
    std::cout <<
        "Catchup\n"
        "  1  play White (you move first)\n"
        "  2  play Black (the engine moves first)\n"
        "  3  watch the engine play itself\n"
        "  4  visual board\n"
        "  5  quit\n";
}

int launch_gui(double total_time) {
    char timebuf[64];
    std::snprintf(timebuf, sizeof(timebuf), "%.6g", total_time);
#ifdef _WIN32
    return _spawnlp(_P_WAIT, "python3", "python3", "main.py", "--gui", "--time",
                    timebuf, nullptr);
#else
    execlp("python3", "python3", "main.py", "--gui", "--time", timebuf, nullptr);
    std::perror("python3");
    return 1;
#endif
}

int choose_side(bool& gui) {
    gui = false;
    while (true) {
        print_menu();
        std::cout << "  choice > " << std::flush;
        std::string answer;
        if (!std::getline(std::cin, answer)) return -2;
        auto b = answer.find_first_not_of(" \t");
        if (b != std::string::npos) {
            auto e = answer.find_last_not_of(" \t");
            answer = answer.substr(b, e - b + 1);
        }
        if (answer == "1") return catchup::WHITE;
        if (answer == "2") return catchup::BLACK;
        if (answer == "3") return -1;
        if (answer == "4") {
            gui = true;
            return -1;
        }
        if (answer == "5" || answer == "q" || answer == "quit") return -2;
        std::cout << "  Please type 1, 2, 3, 4 or 5.\n\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    bool as_white = false, as_black = false, self_play = false, gui = false, quiet = false;
    double total_time = 600.0;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--white") as_white = true;
        else if (a == "--black") as_black = true;
        else if (a == "--self-play") self_play = true;
        else if (a == "--gui") gui = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--time") {
            if (i + 1 >= argc) {
                std::cerr << "--time needs a number\n";
                return 2;
            }
            total_time = std::atof(argv[++i]);
        } else if (a == "-h" || a == "--help") {
            std::cout <<
                "Play Catchup against an alpha-beta engine.\n\n"
                "  ./catchup                 choose a side from the menu\n"
                "  ./catchup --white         play White against the engine\n"
                "  ./catchup --black         play Black against the engine\n"
                "  ./catchup --self-play     watch the engine play itself\n"
                "  ./catchup --time 600      seconds each side gets for the whole game\n"
                "  ./catchup --gui           visual hex board (Python/Tkinter)\n"
                "  ./catchup --quiet         do not print search statistics\n";
            return 0;
        } else {
            std::cerr << "unknown argument: " << a << "\n";
            return 2;
        }
    }

    if (gui) return launch_gui(total_time);

    int human;
    if (as_white) human = catchup::WHITE;
    else if (as_black) human = catchup::BLACK;
    else if (self_play) human = -1;
    else {
        bool menu_gui = false;
        human = choose_side(menu_gui);
        if (human == -2) return 0;
        if (menu_gui) return launch_gui(total_time);
    }

    catchup::Engine engine;
    catchup::play(human, &engine, total_time, !quiet);
    return 0;
}
