#pragma once

#include <vector>
#include <cstdint>

#define ZSTD_STATIC_LINKING_ONLY
#include "libs\include\zstd.h"

#include "Assert.h"

namespace MZ
{
    constexpr size_t operator""_KB(size_t v) { return v * 1024; }
    constexpr size_t operator""_MB(size_t v) { return v * 1024_KB; }

    template <uint8_t SIZE>
    class ZstdCompressor
    {
        static_assert(SIZE != 0, "SIZE must be 1+");

    private:
        ZSTD_CCtx* cctx = nullptr;

        std::vector<uint8_t> buffer;

        ZSTD_outBuffer output;

        size_t input_size = 0, max_input_size = MIN_INPUT_SIZE;

    public:

        static constexpr size_t MIN_INPUT_SIZE = SIZE * 1_MB;

        static constexpr size_t MAX_INPUT_SIZE = MIN_INPUT_SIZE * 2;

        explicit ZstdCompressor(int compression_level = 3, int windowLog = 22, int nbWorkers = 8)
        {
            cctx = ZSTD_createCCtx();
            assert(cctx != nullptr);

            auto bounds = ZSTD_cParam_getBounds(ZSTD_c_compressionLevel);
            assert(compression_level >= bounds.lowerBound && compression_level <= bounds.upperBound);
            ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, compression_level);

            bounds = ZSTD_cParam_getBounds(ZSTD_c_nbWorkers);
            assert(nbWorkers >= bounds.lowerBound && nbWorkers <= bounds.upperBound);
            ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, nbWorkers);

            //bounds = ZSTD_cParam_getBounds(ZSTD_c_jobSize);
            //assert(ZSTD_CCtx_setParameter(cctx, ZSTD_c_jobSize, static_cast<int>(512_KB)) == 512_KB);
            assert(ZSTD_CCtx_setParameter(cctx, ZSTD_c_jobSize, static_cast<int>(1024_KB)) == 1024_KB);

            bounds = ZSTD_cParam_getBounds(ZSTD_c_windowLog);
            assert(windowLog >= bounds.lowerBound && windowLog <= bounds.upperBound);
            assert(ZSTD_CCtx_setParameter(cctx, ZSTD_c_windowLog, windowLog) == windowLog);

            ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);

            buffer.resize(MAX_INPUT_SIZE);

            output = { buffer.data(), MAX_INPUT_SIZE, 0 };
        }

        ~ZstdCompressor()
        {
            if (cctx)
            {
                ZSTD_freeCCtx(cctx);
            }
        }

        // Запрещаем копирование
        ZstdCompressor(const ZstdCompressor&) = delete;
        ZstdCompressor& operator=(const ZstdCompressor&) = delete;

        // Разрешаем перемещение (опционально, можно добавить позже)
        ZstdCompressor(ZstdCompressor&&) = delete;
        ZstdCompressor& operator=(ZstdCompressor&&) = delete;

        // Основная функция: добавить блок данных для сжатия
        // Возвращает true — если блок принят и обработан полностью
        // Возвращает false — если не может принять из-за ограничений
        bool compress(const uint8_t* data, size_t data_size)
        {
            assert(data != nullptr || data_size != 0);

            if ((input_size + data_size) > max_input_size)
            {
                if (max_input_size == MAX_INPUT_SIZE) return false;

                const auto progress = ZSTD_getFrameProgression(cctx);

                const int64_t estimatedSize = static_cast<int64_t>(progress.produced + (progress.ingested - progress.consumed));

                if ((static_cast<int64_t>(progress.consumed) - estimatedSize) <= 0x3FFFF) return false;

                max_input_size = MAX_INPUT_SIZE;
            }

            ZSTD_inBuffer input = { data, data_size, 0 };

            while (input.pos != input.size)
            {
                assert(output.pos != output.size);

                const auto remaining = ZSTD_compressStream2(cctx, &output, &input, ZSTD_e_continue);

                assert(!ZSTD_isError(remaining), "%s\n", ZSTD_getErrorName(remaining));
            }

            input_size += input.size;

            return true;
        }

        struct ZstdCompressorResult
        {
            const uint8_t* output_data;
            const size_t output_size;

            const size_t input_size;
        };

        const ZstdCompressorResult finish()
        {
            if (input_size == 0) return { nullptr, 0, 0 };

            ZSTD_inBuffer input = { nullptr, 0, 0 };

            while (true)
            {
                assert(output.pos != output.size);

                const auto remaining = ZSTD_compressStream2(cctx, &output, &input, ZSTD_e_end);

                if (0 == remaining) break;

                assert(!ZSTD_isError(remaining), "%s\n", ZSTD_getErrorName(remaining));
            }

            const auto progress = ZSTD_getFrameProgression(cctx);

            assert(output.pos == progress.flushed);

            ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only); 
            
            output.pos = 0; input_size = 0; max_input_size = MIN_INPUT_SIZE;

            return { buffer.data(), progress.flushed, progress.ingested };
        }
    };
}
