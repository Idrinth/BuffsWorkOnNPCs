#pragma once

#include <spdlog/sinks/basic_file_sink.h>

namespace logger = SKSE::log;

void SetupLogger(spdlog::level::level_enum level) {
    auto folder = SKSE::log::log_directory();
    if (!folder) {
        return;
    }
    auto logger = std::make_shared<spdlog::logger>(
            "log",
            std::move(
                    std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                            (*folder / std::format(
                                    "{}.log",
                                    SKSE::PluginDeclaration::GetSingleton()->GetName()
                            )).string(),
                            true
                    )
            )
    );
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_level(level);
    spdlog::flush_on(spdlog::level::trace);
}