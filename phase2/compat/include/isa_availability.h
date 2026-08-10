#pragma once

// MSVC's CRT publishes __isa_available and these levels for runtime dispatch.
// mingw-w64 has the intrinsic operations but not this declaration header.

#include <cstdint>
#include <cstring>

#define __ISA_AVAILABLE_X86 0
#define __ISA_AVAILABLE_SSE2 1
#define __ISA_AVAILABLE_SSE42 2
#define __ISA_AVAILABLE_AVX 3
#define __ISA_AVAILABLE_ENFSTRG 4
#define __ISA_AVAILABLE_AVX2 5
#define __ISA_AVAILABLE_AVX512 6

#if defined(__GNUC__) && !defined(__clang__) && !defined(__AVX2__)
namespace openterminal::mingw
{
    struct avx2_words
    {
        std::uint16_t values[16];
    };

    inline avx2_words avx2_load(const void* source) noexcept
    {
        avx2_words value{};
        std::memcpy(&value, source, sizeof(value));
        return value;
    }

    inline void avx2_store(void* destination, const avx2_words& value) noexcept
    {
        std::memcpy(destination, &value, sizeof(value));
    }

    inline avx2_words avx2_add(const avx2_words& left, const avx2_words& right) noexcept
    {
        avx2_words value{};
        for (std::size_t index = 0; index < 16; ++index)
        {
            value.values[index] = static_cast<std::uint16_t>(left.values[index] + right.values[index]);
        }
        return value;
    }

    inline avx2_words avx2_set1(const short input) noexcept
    {
        avx2_words value{};
        for (auto& element : value.values)
        {
            element = static_cast<std::uint16_t>(input);
        }
        return value;
    }
}

// GCC diagnoses calls to always_inline AVX2 intrinsics when the translation
// unit targets generic x86-64. The runtime flag is zero in this build, but the
// branch must still be well-formed; these equivalents preserve that property
// without raising the executable's minimum CPU level.
#undef _mm256_load_si256
#undef _mm256_storeu_si256
#undef _mm256_add_epi16
#undef _mm256_set1_epi16
#define _mm256_load_si256(source) ::openterminal::mingw::avx2_load(source)
#define _mm256_storeu_si256(destination, value) ::openterminal::mingw::avx2_store(destination, value)
#define _mm256_add_epi16(left, right) ::openterminal::mingw::avx2_add(left, right)
#define _mm256_set1_epi16(value) ::openterminal::mingw::avx2_set1(value)
#endif
