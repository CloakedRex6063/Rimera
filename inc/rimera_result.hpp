#pragma once
#include <expected>
#include <string>
#include <new>
#include <utility>

namespace RM
{
    template <typename T>
    using Result = std::expected<T, std::string>;
}
