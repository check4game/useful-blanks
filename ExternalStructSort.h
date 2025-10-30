#pragma once

#include <functional>
#include <queue>
#include <execution>

#include "FileSystem.h"

#ifdef max
#undef max
#endif

namespace MZ
{
    constexpr size_t find_aligment_for_4096(uint32_t x)
    {
        uint32_t val = x, power_of_two_in_x = 0;

        while ((val & 1) == 0)
        {
            val >>= 1; power_of_two_in_x++;
        }

        return ((1ull << (std::max(12u, power_of_two_in_x))) * (x >> power_of_two_in_x)) / x;
    }

    template <typename TRecord>
    class ExternalStructSort
    {
        static_assert(alignof(TRecord) == 1, "alignof(TRecord) != 1");

        static_assert(sizeof(TRecord) % 2 == 0, "sizeof(TRecord) % 2 != 0");

        size_t _memoryLimit, _chunkSize, _numChunks, _preloadSize;

        const size_t _minChunkSize = find_aligment_for_4096(sizeof(TRecord));

        std::function<bool(const TRecord& a, const TRecord& b)> _a_is_less_than_b = nullptr;

        constexpr size_t find_optimal_chunk_size(size_t raw_data_size, size_t max_chunk_size, size_t alignment) const
        {
            size_t chunk_size = max_chunk_size / alignment * alignment;

            while (chunk_size >= alignment)
            {
                size_t num_chunks = (raw_data_size + chunk_size - 1) / chunk_size;

                size_t last_chunk_size = raw_data_size - (num_chunks - 1) * chunk_size;

                if (last_chunk_size >= static_cast<size_t>(chunk_size * 0.90))
                {
                    return chunk_size;
                }

                chunk_size -= alignment;
            }

            return alignment;
        }

        struct ChunkInfo
        {
            uint32_t begin = 0;
            uint32_t end = 0;
            uint32_t offset = 0;
            uint32_t raw_data_size = 0;

            std::vector<TRecord> records;
        };

        std::vector<ChunkInfo> _chunkInfo;

    public:

        ExternalStructSort(size_t fileSize, const std::function<bool(const TRecord& a, const TRecord& b)>& a_is_less_than_b, size_t memoryLimit = 256 * 1024 * 1024)
        {
            assert(memoryLimit >= 128 * 1024 * 1024);
            
            _memoryLimit = memoryLimit;

            _a_is_less_than_b = a_is_less_than_b;

            size_t numRecords = fileSize / sizeof(TRecord);

            assert(numRecords >= _minChunkSize && (numRecords % _minChunkSize) == 0);

            _chunkSize = numRecords; _numChunks = 1; _preloadSize = _minChunkSize;

            if (fileSize > _memoryLimit)
            {
                const auto limit = _memoryLimit / 1024 / sizeof(TRecord);

                if (_preloadSize < limit)
                {
                    _preloadSize = (limit / _minChunkSize) * _minChunkSize;
                }

                _chunkSize = find_optimal_chunk_size(numRecords, _memoryLimit / sizeof(TRecord), _minChunkSize);

                if ((numRecords % _chunkSize) != 0)
                {
                    _numChunks = numRecords / _chunkSize + 1;
                }
                else
                {
                    _numChunks = numRecords / _chunkSize;
                }
            }

            assert((_chunkSize % _minChunkSize) == 0);

            assert(((numRecords % _chunkSize) % _minChunkSize) == 0);

            _chunkInfo.resize(_numChunks);

            for (size_t i = 0; i < _numChunks; i++)
            {
                auto& ci = _chunkInfo[i];

                ci.raw_data_size = static_cast<uint32_t>(_chunkSize);

                if ((i + 1) == _numChunks && (numRecords % _chunkSize) != 0)
                {
                    ci.raw_data_size = static_cast<uint32_t>(numRecords % _chunkSize);
                }
            }
        }

        void ChunkSort(File& file, const std::function<void(TRecord& record)>& preSortAction = nullptr, const std::function<void(TRecord& record)>& afterSortAction = nullptr)
        {
            size_t numRecords = 0;

            for (const auto& ci : _chunkInfo) numRecords += ci.raw_data_size;
            
            assert(file.Size() / sizeof(TRecord) == numRecords);

            std::vector<TRecord> records(_chunkSize);

            std::vector<uint32_t> indices;

            std::vector<TRecord> writeRecords;

            file.SeekBegin(0);

            for (size_t iChunk = 0; iChunk < _numChunks; iChunk++)
            {
                records.resize(file.Read(records));

                assert(records.size() != 0);

                assert((records.size() % _minChunkSize) == 0);

                if (preSortAction)
                {
                    for (auto& record : records)
                    {
                        preSortAction(record);
                    }
                }

                indices.resize(records.size()); std::iota(indices.begin(), indices.end(), 0);

                std::sort(std::execution::par, indices.begin(), indices.end(),
                    [&](const uint32_t a, const uint32_t b)
                    {
                        return _a_is_less_than_b(records[a], records[b]);
                    }
                );

                if (afterSortAction) // только сортировка без записи в файл
                {
                    for (const auto value : indices)
                    {
                        afterSortAction(records[value]);
                    }
                }
                else if (preSortAction || !std::is_sorted(indices.begin(), indices.end()))
                {
                    file.SeekBack(records);

                    for (const auto value : indices)
                    {
                        writeRecords.push_back(records[value]);

                        if (writeRecords.size() != _preloadSize) continue;

                        file.Write(writeRecords); writeRecords.clear();
                    }

                    if (writeRecords.size())
                    {
                        assert((writeRecords.size() % _minChunkSize) == 0);

                        file.Write(writeRecords); writeRecords.clear();
                    }
                }
            }
        }

        void Sort(File& file, const std::function<void(const TRecord& record)>& recordAction)
        {
            size_t numRecords = 0;

            for (auto& ci : _chunkInfo)
            {
                ci.begin = ci.end = ci.offset = 0; numRecords += ci.raw_data_size;
            }

            assert(file.Size() / sizeof(TRecord) == numRecords);

            auto queue_compare_records = [&](const size_t a, const size_t b)
            {
                    // invert for queue.top
                    const auto& aa = _chunkInfo[b];
                    const auto& bb = _chunkInfo[a];

                    return _a_is_less_than_b(aa.records[aa.begin], bb.records[bb.begin]);
            };

            std::priority_queue<size_t, std::vector<size_t>, decltype(queue_compare_records)> queue(queue_compare_records);

            auto preload_records = [&](ChunkInfo& ci, size_t i)
            {
                if (ci.begin == ci.end && ci.offset < ci.raw_data_size)
                {
                    if ((ci.offset + _preloadSize) < ci.raw_data_size)
                    {
                        ci.records.resize(_preloadSize);
                    }
                    else
                    {
                        ci.records.resize(ci.raw_data_size - ci.offset);
                    }

                    ci.end = static_cast<size_t>(file.Read(static_cast<uint32_t>(i * _chunkSize + ci.offset), ci.records));

                    assert(ci.end != 0);

                    ci.begin = 0; ci.offset += ci.end;
                }
            };

            for (size_t i = 0; i < _numChunks; i++)
            {
                preload_records(_chunkInfo[i], i); queue.push(i);
            }

            while (queue.size())
            {
                const auto i = queue.top(); queue.pop();

                auto& ci = _chunkInfo[i];

                recordAction(ci.records[ci.begin++]); preload_records(ci, i);

                if (ci.begin != ci.end) queue.push(i);
            }
        }
    };
}
