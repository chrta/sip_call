// SPDX-License-Identifier: AGPL-3.0-or-later
/*
   Copyright 2026 Christian Taedcke <hacking@taedcke.com>
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#ifdef COMPILE_FOR_NATIVE
#include <random>
#else
#include "esp_random.h"
#endif

class SipIdentifier
{
public:
    static std::string generate(std::size_t bytes = 8)
    {
        static constexpr std::array<char, 16> hexits {
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
        };
        std::string result;
        result.reserve(bytes * 2);
        uint32_t word = 0;
        for (std::size_t i = 0; i < bytes; ++i)
        {
            if ((i & 0x03U) == 0U)
            {
                word = random_u32();
            }
            const auto byte = static_cast<uint8_t>(word & 0xFFU);
            word >>= 8U;
            result.push_back(hexits[byte >> 4U]);
            result.push_back(hexits[byte & 0x0FU]);
        }
        return result;
    }

    // RFC 3261 §8.1.1.7 mandates the magic-cookie prefix for every branch.
    static std::string generate_branch()
    {
        return std::string("z9hG4bK-") + generate(8);
    }

    static uint32_t random_u32()
    {
#ifdef COMPILE_FOR_NATIVE
        thread_local std::mt19937 engine { std::random_device {}() };
        return engine();
#else
        return esp_random();
#endif
    }
};
