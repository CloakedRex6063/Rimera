#pragma once

namespace RM
{
    // TODO: Maybe replace this with a custom result class?
    template <typename T>
    using Result = std::expected<T, std::string>;

    template <typename T>
    T Unwrap(Result<T>&& result)
    {
        if (!result)
        {
            std::fprintf(stderr, "%s\n", result.error().c_str());
            std::exit(1);
        }
        if constexpr (!std::is_void_v<T>)
        {
            return std::move(*result);
        }
    }
}
