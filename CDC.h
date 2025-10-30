#pragma once

#include <algorithm>
#include <functional>
#include <vector>

#include "Assert.h"

//#define FULL_HASH

namespace MZ
{
	namespace CDC
	{
		// Content-Defined Chunking

		template<int minFragmentSize = 4096, int maxFragmentBits = 19, bool bIncludeZeroSize = true, int avgFragmentSize = 6>
		class Zpaq
		{
			static_assert(maxFragmentBits >= 19 && maxFragmentBits <= 20, "maxFragmentBits [19...20]");

			static_assert((minFragmentSize % 1024) == 0 && minFragmentSize <= (1u << maxFragmentBits) / 2, "minFragmentSize");

			static_assert(avgFragmentSize == 6 || avgFragmentSize == 7, "avgFragmentSize 6=64KB, 7=128KB");

			uint32_t fastMultTable[256], fastSumTable[256];

			uint8_t o1Table[256];

			std::vector<uint8_t> fragment;

		public:

			const uint32_t hashLimit = (1u << (22 - avgFragmentSize)) + 4096; // 2^avgFragmentSize

			const uint32_t bufferSize = (1u << maxFragmentBits);

			const uint32_t maxFragmentSize = bufferSize - ((bIncludeZeroSize) ? 1 : 0);

			Zpaq()
			{
				std::fill_n(fastMultTable, sizeof(fastMultTable) / sizeof(fastMultTable[0]), 271828182u); fastMultTable[0] = 314159265u;

				std::fill_n(reinterpret_cast<uint64_t*>(fastSumTable), sizeof(fastSumTable) / sizeof(uint64_t), 0ull); fastSumTable[0] = 1;

				fragment.resize(bufferSize);
			}

			using DataActionType = std::function<uint8_t* (uint32_t seek, uint32_t& size)>;

			using ReadyActionType = std::function<void(std::vector<uint8_t>& fragmentBuffer, uint32_t size, uint32_t score)>;

			void Cut(const DataActionType& dataAction, const ReadyActionType& readyAction)
			{
				uint32_t hash = 0u, prev = 0u, hits = 0u;  // rolling hash for finding fragment boundaries & previous byte

				uint32_t blockSize = 0u, fragmentLength = 0u, dataSize = 0u;

				uint8_t* begin = nullptr;

				while ((begin = dataAction(blockSize, dataSize)) != nullptr && dataSize)
				{
					if (fragmentLength < minFragmentSize)
					{
						if (!fragmentLength)
						{
							std::fill_n(o1Table, sizeof(o1Table), 0);
							o1Table[prev = *begin] = *begin; hits = hash = 0;
							//hits = hash = prev = 0;
						}

						const auto end = begin + (blockSize = std::min(dataSize, minFragmentSize - fragmentLength));

						std::copy_n(begin, blockSize, fragment.data() + fragmentLength);
#ifdef FULL_HASH
						while (begin != end)
						{
							hash++; hash += *begin;

							const auto match = o1Table[prev] ^ *begin;
							hash *= fastMultTable[match]; hits += fastSumTable[match];

							prev = o1Table[prev] = *begin++;
						}
#else
						while (begin != end)
						{
							const auto match = o1Table[prev] ^ *begin;
							hits += fastSumTable[match];

							prev = o1Table[prev] = *begin++;
						}
#endif

						fragmentLength += blockSize; continue;
					}

					const auto end = begin + (blockSize = std::min(dataSize, maxFragmentSize - fragmentLength));

					do
					{
						hash++; hash += *begin;

						const auto match = o1Table[prev] ^ *begin;
						hash *= fastMultTable[match]; hits += fastSumTable[match];

						prev = o1Table[prev] = *begin++;

					} while (hash >= hashLimit && begin != end);

					std::copy(end - blockSize, begin, fragment.data() + fragmentLength);

					fragmentLength += (blockSize -= static_cast<uint32_t>(end - begin));

					if (hash < hashLimit || fragmentLength == maxFragmentSize)
					{
#if 1
						readyAction(fragment, fragmentLength, (hits * 100) / fragmentLength);
#else
						const auto score = (hits * 100) / fragmentLength;

						if (score >= 30)
						{
							readyAction(fragment, fragmentLength, score);
						}
						else
						{
							constexpr static uint8_t dt[256] =
							{
							  160,80,53,40,32,26,22,20,17,16,14,13,12,11,10,10,
								9, 8, 8, 8, 7, 7, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5,
								4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3,
								3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
								2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
								1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
								1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
								1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
								1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
								1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
							};

							uint8_t o1CntTable[256] = { 0 };  // counts of bytes in o1

							uint32_t h1 = 0;

							for (uint32_t i = 0; i < 256; i++)
							{
								auto& value = o1CntTable[o1Table[i]];

								if (value < 255)
								{
									h1 += ((fragmentLength * dt[value++]) >> 15);
								}
							}

							h1 = (h1 <= fragmentLength) ? (fragmentLength - h1) : (h1 - fragmentLength);

							h1 = h1 * h1 / fragmentLength; // near 0 if random.

							if (h1 > hits)
								readyAction(fragment, fragmentLength, (h1 * 100) / fragmentLength);
							else
								readyAction(fragment, fragmentLength, score);
						}
#endif
						fragmentLength = 0u;
					}
				}

				if (fragmentLength)
				{
					readyAction(fragment, fragmentLength, (hits * 100) / fragmentLength);

					fragmentLength = 0u;
				}
			}
		};
	}
}
