#include <exception>
#include <iostream>

#include "app/app.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
    try {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
#endif
        return icarus::app::run_app();
    } catch (const std::exception& ex) {
        std::cout << "Error: " << ex.what() << '\n';
    }

    return 1;
}
