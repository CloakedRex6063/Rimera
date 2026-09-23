#pragma once

#include "type_traits"

namespace RM
{
    template <typename E>
    requires std::is_enum_v<E> class EnumFlags
    {
    public:
        using Underlying = std::underlying_type_t<E>;

        constexpr EnumFlags() noexcept = default;
        constexpr EnumFlags(E flag) noexcept : m_bits(static_cast<Underlying>(flag)) {}
        constexpr EnumFlags(std::initializer_list<E> flags) noexcept
        {
            for (E flag : flags) m_bits |= static_cast<Underlying>(flag);
        }

        [[nodiscard]] constexpr Underlying value() const noexcept { return m_bits; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_bits != 0; }

        [[nodiscard]] constexpr bool contains(EnumFlags flags) const noexcept
        {
            return (m_bits & flags.m_bits) == flags.m_bits;
        }

        [[nodiscard]] constexpr bool intersects(EnumFlags flags) const noexcept
        {
            return (m_bits & flags.m_bits) != 0;
        }

        constexpr EnumFlags& operator|=(EnumFlags other) noexcept
        {
            m_bits |= other.m_bits;
            return *this;
        }

        constexpr EnumFlags& operator&=(EnumFlags other) noexcept
        {
            m_bits &= other.m_bits;
            return *this;
        }

        constexpr EnumFlags& operator^=(EnumFlags other) noexcept
        {
            m_bits ^= other.m_bits;
            return *this;
        }

        friend constexpr EnumFlags operator|(EnumFlags a, EnumFlags b) noexcept { return a |= b; }
        friend constexpr EnumFlags operator&(EnumFlags a, EnumFlags b) noexcept { return a &= b; }
        friend constexpr EnumFlags operator^(EnumFlags a, EnumFlags b) noexcept { return a ^= b; }

        friend constexpr EnumFlags operator~(EnumFlags flags) noexcept
        {
            flags.m_bits = static_cast<Underlying>(~flags.m_bits);
            return flags;
        }

        friend constexpr bool operator==(EnumFlags, EnumFlags) noexcept = default;

    private:
        Underlying m_bits{};
    };
}  // namespace RM
