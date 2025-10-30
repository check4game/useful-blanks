#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>

class DeltaCompressor
{
public:

    static uint32_t ZigZagEencode(int32_t n)
    {
        return (static_cast<uint32_t>(n) << 1) ^ (static_cast<uint32_t>(n >> 31));
    }

    static int32_t ZigZagDecode(uint32_t n)
    {
        return (static_cast<int32_t>(n >> 1)) ^ (-(static_cast<int32_t>(n & 1)));
    }

    static std::vector<uint32_t> Encode(std::vector<uint32_t>& input)
    {
        std::vector<uint32_t> overflow;

        uint32_t prev = 0, pos = 0, input_size = static_cast<uint32_t>(input.size());

        for (pos = 0; pos < input_size; pos++)
        {
            prev = input[pos];

            if (prev)
            {
                pos++; break;
            }
        }
        
        for (; pos < input_size; pos++) 
        {
            const uint32_t value = input[pos];

            if (value)
            {
                const int64_t delta = static_cast<int64_t>(value) - static_cast<int64_t>(prev);

                if (delta < INT32_MIN || delta > INT32_MAX || delta == 0)
                {
                    overflow.push_back(pos);
                }
                else
                {
                    input[pos] = ZigZagEencode(static_cast<int32_t>(delta));
                }

                prev = value;
            }
        }

        return overflow;
    }

    static void Decode(std::vector<uint32_t>& input, std::vector<uint32_t>& overflow)
    {
        overflow.push_back(UINT_MAX);

        uint32_t prev = 0, pos = 0, input_size = static_cast<uint32_t>(input.size());

        for (pos = 0; pos < input_size; pos++)
        {
            prev = input[pos];

            if (prev)
            {
                pos++; break;
            }
        }

        size_t overflow_pos = 0;

        for (; pos < input_size; pos++) 
        {
            if (pos != overflow[overflow_pos])
            {
                prev = input[pos] = static_cast<int64_t>(prev) + ZigZagDecode(input[pos]);
            }
            else
            {
                overflow_pos++;
            }
        }

        overflow.resize(overflow.size() - 1);
    }
};
