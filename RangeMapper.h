#pragma once
#include <cstdint>
#include <queue>

#include "Assert.h"

namespace MZ
{
    // Класс для обработки двух очередей range'ей
    class RangeMapper
    {
    private:

#pragma pack(push, 1)

        // Структура для маппинга значений
        struct Range
        {
            uint32_t sourceBegin;  // исходный диапазон
            uint32_t targetBegin;  // целевой диапазон

            uint16_t rangeSize;

            Range(uint32_t source, uint32_t target, uint16_t size)
                : sourceBegin(source), targetBegin(target), rangeSize(size)
            {
                assert((targetBegin + rangeSize) <= sourceBegin);
            }

            __inline uint32_t map(uint32_t input) const
            {
                return targetBegin + (input - sourceBegin);
            }
        };

#pragma pack(pop)

        std::queue<Range> queueL;
        std::queue<Range> queueH;

    public:

        void addRangeL(uint32_t source, uint32_t target, uint16_t size)
        {
            queueL.emplace(source, target, size);
        }

        void addRangeH(uint32_t source, uint32_t target, uint16_t size)
        {
            queueH.emplace(source, target, size);
        }
        void addRange(bool bLow, uint32_t source, uint32_t target, uint16_t size)
        {
            if (bLow)
                queueL.emplace(source, target, size);
            else
                queueH.emplace(source, target, size);
        }

        // Прямое преобразование source -> target (максимально оптимизированное)
        // значения которые не мапятся могут приходить в любом порядке
        // значения из двух текущих диапазонов могут приходить в любом порядке
        // значение input == sourceBegin + rangeSize переключает на следующий дапазон
        // диапазоны в очередях не перекрываются и последовательно растут на rangeSize
        __inline uint32_t remap(uint32_t input)
        {
            while (queueH.size())
            {
                const auto& range = queueH.front();

                if (input < range.sourceBegin) break;
                
                if (input < (range.sourceBegin + range.rangeSize))
                {
                    return range.map(input);
                }

                queueH.pop();
            }

            while (queueL.size())
            {
                const auto& range = queueL.front();

                if (input < range.sourceBegin) break;

                if (input < (range.sourceBegin + range.rangeSize))
                {
                    return range.map(input);
                }

                queueL.pop();
            }

            return input;
        }

        void validate(uint32_t reMapIndexL, uint32_t reMapIndexH)
        {
            assert(remap(reMapIndexL) == reMapIndexL && remap(reMapIndexH) == reMapIndexH);

            assert(queueL.size() == 0 && queueH.size() == 0);
        }
    };
}

