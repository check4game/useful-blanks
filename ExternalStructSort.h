#pragma once

#include <functional>

#include <ppl.h>
#include "CFile.h"

#ifdef max
#undef max
#endif

namespace MZ
{
    size_t find_aligment_for_4096(uint32_t x)
    {
        uint32_t val = x, power_of_two_in_x = 0;

        while ((val & 1) == 0)
        {
            val >>= 1; power_of_two_in_x++;
        }

        // 2^12=4096
        return ((1ull << (std::max(12u, power_of_two_in_x))) * (x >> power_of_two_in_x)) / x;
    }

    size_t find_optimal_chunk_size_aligned(size_t size, size_t max_chunk_size, size_t alignment)
    {
        size_t chunk_size = max_chunk_size / alignment * alignment;

        while (chunk_size >= alignment) 
        {
            size_t num_chunks = (size + chunk_size - 1) / chunk_size;

            size_t last_chunk_size = size - (num_chunks - 1) * chunk_size;

            if (last_chunk_size >= static_cast<size_t>(chunk_size * 0.90)) 
            {
                return chunk_size;
            }

            chunk_size -= alignment;
        }

        return alignment;
    }

    template <typename TRecord, auto Key>
    class ExternalStructSort
    {
        using key_type = std::remove_reference_t<decltype(std::declval<TRecord>().*Key)>;

        static_assert(std::is_arithmetic_v<key_type>, "non arithmetic");

        static_assert(alignof(TRecord) == 1, "alignof(TRerord) != 1");

        static_assert(sizeof(TRecord) % 2 == 0, "sizeof(TRecord) % 2 != 0");

    public:

        static size_t Sort(std::wstring path, std::function<void(const TRecord& record)> recordAction, size_t memoryLimit = 128 * 1024 * 1024)
        {
            MZ::CFile file;

            if (!file.Open(path.c_str()))
            {
                std::wcerr << path << ", " << file.GetLastError() << std::endl; std::abort();
            }

            auto ret = Sort(file, recordAction, memoryLimit);

            file.Close();
        }

        static size_t Sort(MZ::CFile& file, std::function<void(const TRecord& record)> recordAction, size_t memoryLimit = 128 * 1024 * 1024)
        {
            size_t fileSize = file.Size(), numRecords = fileSize / sizeof(TRecord);

            size_t chunkSize = numRecords, numChunks = 1;

            auto aligment = find_aligment_for_4096(sizeof(TRecord));

            assert((numRecords % aligment) == 0);

            assert((fileSize % sizeof(TRecord)) == 0 && chunkSize != 0);

            if (fileSize > memoryLimit)
            {
                while ((aligment * sizeof(TRecord) * 2) < (128ull * 1024))
                {
                    aligment *= 2;
                }

                chunkSize = find_optimal_chunk_size_aligned(numRecords, memoryLimit / sizeof(TRecord), aligment);

                numChunks = numRecords / chunkSize;

                if ((numRecords % chunkSize) != 0) numChunks++;
            }

            assert((chunkSize % aligment) == 0);

            if (numChunks == 1)
            {
                std::vector<TRecord> sortRecords(chunkSize);

                const auto dataSize = static_cast<uint32_t>(sortRecords.size() * sizeof(TRecord));

                assert(file.SeekBegin(0) == 0);

                assert(file.Read(reinterpret_cast<byte*>(sortRecords.data()), dataSize) == dataSize);

                concurrency::parallel_sort(sortRecords.begin(), sortRecords.end(), [](const TRecord& a, const TRecord& b)
                    {
                        return a.*Key < b.*Key;
                    });


                for (const auto& record : sortRecords)
                {
                    recordAction(record);
                }

                return numChunks;
            }
            else
            {
                std::vector<TRecord> sortRecords(chunkSize);

                ChunkSort(sortRecords,

                    [&](byte* data, uint32_t size) -> uint32_t
                    {
                        return file.Read(data, size);
                    },
                    [&](const byte* data, uint32_t size)
                    {
                        file.SeekBack(size); file.Write(data, size);
                    }
                );
            }

            struct ChunkInfoStruct
            {
                size_t begin;
                size_t end;
                size_t offset;
                size_t size;
            };

            std::vector<ChunkInfoStruct> chunkInfo(numChunks);

            for (size_t i = 0; i < numChunks; i++)
            {
                auto& ci = chunkInfo[i];

                ci.begin = 0; ci.end = ci.offset = aligment; ci.size = chunkSize;

                if (numRecords < chunkSize)
                {
                    ci.size = numRecords; 
                    
                    if (numRecords < aligment) ci.end = numRecords;
                    
                    numRecords = 0; break;
                }

                numRecords -= chunkSize;
            }

            assert(numRecords == 0);

            std::vector<std::vector<TRecord>> buffers(numChunks);

            // preload data
            for (size_t i = 0; i < numChunks; i++)
            {
                auto& buffer = buffers[i];

                buffer.resize(aligment);

                file.SeekBegin(i * chunkSize * sizeof(TRecord));

                file.Read(reinterpret_cast<byte*>(buffer.data()), buffer.size() * sizeof(TRecord));
            }

            key_type keyMinValue;

            while (true)
            {
                size_t iMin = 0;

                for (size_t i = 0; i < numChunks; i++)
                {
                    auto& ci = chunkInfo[i];

                    auto& buffer = buffers[i];

                    if (ci.begin == ci.end && ci.offset < ci.size)
                    {
                        file.SeekBegin((i * chunkSize + ci.offset) * sizeof(TRecord));

                        file.Read(reinterpret_cast<byte*>(buffer.data()), buffer.size() * sizeof(TRecord));
                        
                        if ((ci.size - ci.offset) < aligment) ci.end = (ci.size - ci.offset);
                        
                        ci.begin = 0; ci.offset += ci.end;
                    }

                    if (ci.begin != ci.end)
                    {
                        const auto keyValue = buffer[ci.begin].*Key;

                        if (iMin ==  0 || keyValue < keyMinValue)
                        {
                            keyMinValue = keyValue; iMin = i + 1;
                        }
                    }
                }

                if (iMin == 0) break;

                iMin--; recordAction(buffers[iMin][chunkInfo[iMin].begin++]);
            }

            return numChunks;
        }


    private:

        static void ChunkSort(std::vector<TRecord>& sortRecords, std::function<uint32_t(byte* data, uint32_t size)> getAction, std::function<void(const byte* data, uint32_t)> readyAction)
        {
            auto begin = reinterpret_cast<byte*>(sortRecords.data());

            const auto dataSize = static_cast<uint32_t>(sortRecords.size() * sizeof(TRecord));

            auto readlDataSize = getAction(begin, dataSize);

            while (readlDataSize)
            {
                concurrency::parallel_sort(sortRecords.begin(), sortRecords.begin() + readlDataSize / sizeof(TRecord), [](const TRecord& a, const TRecord& b)
                    {
                        return a.*Key < b.*Key;
                    });

                readyAction(begin, readlDataSize);

                readlDataSize = getAction(begin, dataSize);
            }
        }
    };
}
