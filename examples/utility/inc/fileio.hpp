#pragma once

#include "rimera_result.hpp"
#include "filesystem"
#include "string"

class FileIO
{
public:
    [[nodiscard]] static std::string ResourcePath();
    [[nodiscard]] static RM::Result<std::string> Read(const std::filesystem::path& path);
};