#pragma once

#include <cstdlib>
#include <cstdint>

#include "DecimalDate.h"
#include "CRC32.h"

namespace MZ
{
    enum class BlockType { DATA, INDEX, HASH };

#pragma pack(push, 1)

    template <BlockType type>
    struct TextFingerPrint;

    struct BinaryFingerPrint
    {
        uint64_t low64;
        uint64_t high64;
    };

    template<>
    struct TextFingerPrint<BlockType::DATA>
    {
        char value[30] = "MZYYYYMMDDHHMMSSdNNNNNNNNNNZM";
    };

    template<>
    struct TextFingerPrint<BlockType::INDEX>
    {
        char value[30] = "MZYYYYMMDDHHMMSSiNNNNNNNNNNZM";
    };

    template<>
    struct TextFingerPrint<BlockType::HASH>
    {
        char value[30] = "MZYYYYMMDDHHMMSShNNNNNNNNNNZM";
    };

    template <BlockType type>
    struct BlockHeader
    {
        uint32_t blockSize = 0; // block size

        union
        {
            TextFingerPrint<type> txt{};

            struct Fields
            {
                char MZ[2];

                char date[14];

                char type; // 'd' | 'i' | 'h'

                char index[10];

                char ZM[2];

                char zero;

            } fields;
        };

        bool isData() const
        {
            return fields.type == 'd';
        }

        bool isIndex() const
        {
            return fields.type == 'i';
        }

        bool isHash() const
        {
            return fields.type == 'h';
        }

        void SetDate(DecimalDateValue value)
        {
            DecimalDate::ToString<14>(value, fields.date);
        }

        DecimalDateValue GetDate() const
        {
            return std::atoll(fields.date);
        }

        void SetIndex(uint32_t value)
        {
            DecimalDate::ToString<10>(value, fields.index);
        }

        uint32_t GetIndex() const
        {
            return static_cast<uint32_t>(std::atol(fields.index));
        }

        void SetSize(uint32_t size)
        {
            blockSize = size;
        }

        uint32_t GetSize() const
        {
            return blockSize;
        }

        uint64_t blockFingerprint = 0; // контрольная сумма блока

        uint64_t headerFingerprint = 0; // контрольная сумма DataHeader | IndexHeader
    };

    template <uint8_t key_size>
    struct DataHeader
    {
        BlockHeader<BlockType::DATA> header;

        uint64_t keysFingerprint = 0; // контрольная сумма ключей включая текущие

        uint16_t flags = 0; // 0=RAW_DATA, 1=ZSTD

        uint16_t counter = 0;

        uint32_t size = 0; // raw data size

        void Init(DecimalDateValue date, uint32_t index, uint32_t raw_size, uint16_t data_flags)
        {
            header.txt = TextFingerPrint<BlockType::DATA>{};

            header.SetSize(sizeof(DataHeader));

            header.SetDate(date);

            header.SetIndex(index);

            counter = 0; size = raw_size; flags = data_flags;

            header.headerFingerprint = header.blockFingerprint = 0;
        }

        void AddKeys(const uint8_t* data, uint32_t data_size)
        {
            assert((data_size % key_size) == 0 && data_size != 0);
            //assert(header.GetSize() == sizeof(DataHeader) && counter == 0);

            std::copy_n(data, data_size, reinterpret_cast<uint8_t*>(&header) + header.GetSize());

            counter = data_size / key_size; header.SetSize(header.GetSize() + data_size);
        }

        void AddData(const uint8_t* data, uint32_t data_size)
        {
            assert(counter != 0 && data_size != 0);
            //assert((counter * key_size + sizeof(DataHeader)) == header.GetSize());

            std::copy_n(data, data_size, reinterpret_cast<uint8_t*>(&header) + header.GetSize());

            header.SetSize(header.GetSize() + data_size);
        }
    };

#pragma pack(pop)

}
