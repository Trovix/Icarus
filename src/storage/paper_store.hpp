#pragma once

#include <filesystem>
#include <optional>

#include "paper/paper_trading.hpp"

namespace icarus::storage {

std::optional<paper::PaperTradingEngine> load_paper_state(
    const std::filesystem::path& path
);

void save_paper_state(
    const std::filesystem::path& path,
    const paper::PaperTradingEngine& engine
);

}  // namespace icarus::storage
