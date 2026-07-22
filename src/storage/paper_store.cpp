#include "storage/paper_store.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace icarus::storage {

namespace {

void replace_file(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination
) {
#ifdef _WIN32
    if (!MoveFileExW(
            temporary.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        )) {
        throw std::runtime_error(
            "Unable to replace paper state file (Windows error " +
            std::to_string(GetLastError()) + ")"
        );
    }
#else
    std::filesystem::rename(temporary, destination);
#endif
}

}  // namespace

std::optional<paper::PaperTradingEngine> load_paper_state(
    const std::filesystem::path& path
) {
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Unable to read paper state: " + path.string());
    }

    const nlohmann::json json = nlohmann::json::parse(input, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        throw std::runtime_error("Invalid paper state JSON: " + path.string());
    }

    return paper::PaperTradingEngine::fromJson(json);
}

void save_paper_state(
    const std::filesystem::path& path,
    const paper::PaperTradingEngine& engine
) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::filesystem::path temporary = path;
    temporary += ".tmp";

    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            throw std::runtime_error(
                "Unable to write temporary paper state: " + temporary.string()
            );
        }
        output << engine.toJson().dump(2) << '\n';
        output.flush();
        if (!output) {
            throw std::runtime_error(
                "Unable to flush temporary paper state: " + temporary.string()
            );
        }
    }

    replace_file(temporary, path);
}

}  // namespace icarus::storage
