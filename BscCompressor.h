#pragma once

#include <cstdint>
#include <vector>

#include "libs/libbsc/libbsc.h"
#include "libs/libbsc/bwt.h"
#include "libs/libbsc/coder.h"

#include "Assert.h"

#pragma comment(lib, "libs/libbsc_static.lib")

class BscCompressor
{
private:

	int features = 0;

	static bool bInit;

	BscCompressor(int features)
	{
		this->features = features;
	}

	template <int coder>
	int Encode(const uint8_t* input, uint8_t* output, size_t size)
	{
		return bsc_compress(input, output, static_cast<int>(size), 0, 0, LIBBSC_BLOCKSORTER_BWT, coder, features);
	}

public:

	class BscRawCompressor
	{
		int features = 0;

		friend BscCompressor;

	private:

		BscRawCompressor(int features)
		{
			this->features = features;
		}

		template<int coder>
		int32_t Encode(uint8_t* inout, size_t size)
		{
			int32_t   indexes[256];
			uint8_t   num_indexes = 0;

			const auto result_index = bsc_bwt_encode(inout, static_cast<int32_t>(size), &num_indexes, indexes, features);

			mz_assert(result_index >= LIBBSC_NO_ERROR);

			if (size < 64ull * 1024)
			{
				num_indexes = 0;
			}

			uint8_t* buffer = new uint8_t[size + 4096];

			mz_assert(buffer != nullptr);

			auto result_size = bsc_coder_compress(inout, buffer, static_cast<int32_t>(size), coder, features);

			const auto header_size = static_cast<int32_t>(1 + sizeof(int32_t) + 1 + sizeof(int32_t) * num_indexes);

			mz_assert(result_size > LIBBSC_NO_ERROR && (result_size + header_size) <= size,
				"result_size: %d > %d", result_size, static_cast<int32_t>(size));

			std::memcpy(inout + header_size, buffer, result_size); delete[] buffer;

			*inout++ = coder;

			*reinterpret_cast<int32_t*>(inout) = result_index; inout += sizeof(int32_t);

			*inout++ = num_indexes;

			std::memcpy(inout, indexes, sizeof(indexes[0]) * num_indexes);

			return result_size + header_size;
		}
		
	public:

		template <typename T>
		int32_t EncodeAdaptive(std::vector<T>& inout)
		{
			return EncodeAdaptive(reinterpret_cast<uint8_t*>(inout.data()), inout.size() * sizeof(T));
		}

		int32_t EncodeAdaptive(uint8_t* inout, size_t size)
		{
			return Encode<LIBBSC_CODER_QLFC_ADAPTIVE>(inout, size);
		}

		template <typename T>
		int32_t EncodeStatic(std::vector<T>& inout)
		{
			return EncodeStatic(reinterpret_cast<uint8_t*>(inout.data()), inout.size() * sizeof(T));
		}

		int32_t EncodeStatic(uint8_t* inout, size_t size)
		{
			return Encode<LIBBSC_CODER_QLFC_STATIC>(inout, size);
		}

		template <typename TI, typename TO>
		int32_t Decode(const std::vector<TI>& input, std::vector<TO>& output)
		{
			return Decode(reinterpret_cast<const uint8_t*>(input.data()), input.size() * sizeof(TO), reinterpret_cast<uint8_t*>(output.data()), output.size() * sizeof(TO));
		}

		int32_t Decode(const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size)
		{
			auto header_size = static_cast<int32_t>(1 + sizeof(int32_t) + 1);

			mz_assert(input_size > header_size && input_size < output_size);

			const auto coder = *input++;
			const auto index = *reinterpret_cast<const int32_t*>(input);

			input += sizeof(int32_t);

			mz_assert(coder == LIBBSC_CODER_QLFC_STATIC || coder == LIBBSC_CODER_QLFC_ADAPTIVE);

			uint8_t num_indexes = *input++;

			header_size += sizeof(int32_t) * num_indexes;

			mz_assert(input_size > header_size);

			int32_t indexes[256];

			std::memcpy(indexes, input, sizeof(int32_t) * num_indexes);

			input += sizeof(int32_t) * num_indexes;

			const auto result_size = bsc_coder_decompress(input, output, coder, features);

			mz_assert(result_size > LIBBSC_NO_ERROR);

			return result_size;
		}
	};

	static BscRawCompressor CreateRaw(bool bSingleThread = false)
	{
		if (!bInit)
		{
			bsc_init(0); bInit = true;
		}

		return BscRawCompressor((bSingleThread) ? 0 : LIBBSC_FEATURE_MULTITHREADING);
	}

	static BscCompressor Create(bool bSingleThread = false)
	{
		if (!bInit)
		{
			bsc_init(0); bInit = true;
		}

		return BscCompressor((bSingleThread) ? 0 : LIBBSC_FEATURE_MULTITHREADING);
	}

	int32_t EncodeStatic(const uint8_t* input, uint8_t* output, size_t size)
	{
		return Encode<LIBBSC_CODER_QLFC_STATIC>(input, output, size);
	}

	int32_t EncodeAdaptive(const uint8_t* input, uint8_t* output, size_t size)
	{
		return Encode<LIBBSC_CODER_QLFC_ADAPTIVE>(input, output, size);
	}

	template <typename T>
	int32_t EncodeStatic(std::vector<T>& inout)
	{
		return EncodeStatic(reinterpret_cast<const uint8_t*>(inout.data()),
			reinterpret_cast<uint8_t*>(inout.data()), inout.size() * sizeof(T));
	}

	template <typename T>
	int32_t EncodeAdaptive(std::vector<T>& inout)
	{
		return EncodeAdaptive(reinterpret_cast<const uint8_t*>(inout.data()),
			reinterpret_cast<uint8_t*>(inout.data()), inout.size() * sizeof(T));
	}

	template <typename T>
	int32_t getBlockDataSize(const std::vector<T>& in, size_t size)
	{
		int blockSize = 0, dataSize = 0;

		const auto result = GetBlockInfo(in, size, blockSize, dataSize);

		return (result == 0 && blockSize == size) ? dataSize : 0;
	}

	template <typename T>
	bool CheckBlockInfo(const std::vector<T>& in, size_t size)
	{
		int blockSize = 0, dataSize = 0;

		const auto result = GetBlockInfo(in, size, blockSize, dataSize);

		return result == 0 && blockSize == size;
	}

	template <typename T>
	int32_t GetBlockInfo(const std::vector<T>& in, size_t size, int& blockSize, int& dataSize)
	{
		return GetBlockInfo(reinterpret_cast<const uint8_t*>(in.data()), size, blockSize, dataSize);
	}

	int32_t GetBlockInfo(const uint8_t* in, size_t size, int& blockSize, int& dataSize)
	{
		return bsc_block_info(in, LIBBSC_HEADER_SIZE, &blockSize, &dataSize, features);
	}
};

bool BscCompressor::bInit = false;
