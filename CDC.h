#pragma once

#include <algorithm>
#include <functional>
#include <vector>

namespace MZ
{
	namespace CDC
	{
		// Content-Defined Chunking

		template<uint32_t minFragmentSize = 4096, uint8_t maxFragmentBits = 19>
		class Zpaq
		{
			uint32_t fastMultTable[256];
			
			uint32_t fastSumTable[256];

			std::vector<uint8_t> fragment;

			static constexpr uint32_t hashLimit = 1u << 16;

			const uint32_t maxFragmentSize = (1u << maxFragmentBits) - 1;

		public:
			Zpaq()
			{
				std::fill_n(fastMultTable, sizeof(fastMultTable) / sizeof(fastMultTable[0]), 271828182u); fastMultTable[0] = 314159265u;

				std::fill_n((uint64_t*)fastSumTable, sizeof(fastSumTable) / sizeof(uint64_t), 0ull); fastSumTable[0] = 1;

				fragment.resize(maxFragmentSize + 1);
			}

			void Cut(std::function<uint8_t* (uint32_t seek, uint32_t& size)> dataAction, std::function<void(const uint8_t* data, uint32_t size, float hits)> readyAction)
			{
				uint8_t o1BytesTable[256];

				uint32_t hash = 0, prev = 0, hits = 0;  // rolling hash for finding fragment boundaries & previous byte

				uint32_t blockSize = 0, fragmentLength = 0, dataSize = 0;

				uint8_t* begin = nullptr;

				while ((begin = dataAction(blockSize, dataSize)) != nullptr && dataSize)
				{
					if (fragmentLength < minFragmentSize)
					{
						if (!fragmentLength)
						{
							std::fill_n((uint64_t*)o1BytesTable, sizeof(o1BytesTable) / sizeof(uint64_t), 0ull);

							o1BytesTable[prev = *begin] = *begin;
						}

						const auto end = begin + (blockSize = std::min(dataSize, minFragmentSize - fragmentLength));

						std::copy(begin, end, fragment.data() + fragmentLength);

						while (begin != end)
						{
							hash++; hash += *begin;

							const uint32_t idx = o1BytesTable[prev] ^ *begin;

							hash *= fastMultTable[idx]; hits += fastSumTable[idx];
								
							prev = o1BytesTable[prev] = *begin++;
						}

						fragmentLength += blockSize; continue;
					}

					const auto end = begin + (blockSize = std::min(dataSize, maxFragmentSize - fragmentLength));

					while (begin != end && hash >= hashLimit)
					{
						hash++; hash += *begin;

						const uint32_t idx = o1BytesTable[prev] ^ *begin;

						hash *= fastMultTable[idx]; hits += fastSumTable[idx];

						prev = o1BytesTable[prev] = *begin++;
					}

					std::copy(end - blockSize, begin, fragment.data() + fragmentLength);

					fragmentLength += (blockSize -= (uint32_t)(end - begin));

					if (fragmentLength == maxFragmentSize || hash < hashLimit)
					{
						readyAction(fragment.data(), fragmentLength, (float)hits / fragmentLength);

						fragmentLength = hits = hash = 0u;
					}
				}
			
				if (fragmentLength)
				{
					readyAction(fragment.data(), fragmentLength, (float)hits / fragmentLength);
				}
			}
		};
	}
}
