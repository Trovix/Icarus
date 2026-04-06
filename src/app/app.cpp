#include "app/app.hpp"

#include <filesystem>

#include "app/engine.hpp"

namespace icarus::app {

namespace {

std::filesystem::path find_project_root() {
    std::filesystem::path current = std::filesystem::current_path();

    while (!current.empty()) {
        if (std::filesystem::exists(current / "CMakeLists.txt")) {
            return current;
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            break;
        }

        current = parent;
    }

    return std::filesystem::current_path();
}

}  // namespace

int run_app() {
    Engine engine(find_project_root());
    return engine.run();
}

}  // namespace icarus::app
