#pragma once

#include "SimdHash.h"
#include "RangeMapper.h"
#include "Assert.h"
#include "FileSystem.h"
#include "ExternalStructSort.h"

#define XXH_VECTOR XXH_AVX2
#include "xxHash3\xxh3.h"


#define DEBUG_COLLISION 0
//#define DEBUG_FRAGMENT_SIZE 32

//#define DEBUG_FRAGMENT_INFO 1

//#define SIZE_IN_SMALL_KEY

namespace MZ
{
#pragma pack(push, 1)

    struct LargeKey
    {
        __inline bool operator==(const LargeKey& lk) const
        {
            return smallKey == lk.smallKey && shortCmp(lk);
        }

        __inline bool shortCmp(const LargeKey& lk) const
        {
            return l1 == lk.l1 && l2 == lk.l2 && l3 == lk.l3;
        }

        union
        {
            struct
            {
                uint64_t smallKey; // [....size:19, flag:1=1] | [sIndex:32, cIndex:31, flag:1=0]

                uint64_t l1;
                uint64_t l2;
                uint64_t l3;
            };

            uint8_t value[32];
        };

        __inline bool hasSize() const
        {
            return static_cast<uint8_t>(smallKey) & 1; // бит 0
        }

        __inline uint32_t collision_index() const
        {
            return static_cast<uint32_t>(smallKey) >> 1; // 31 бит [31...1]
        }

        __inline uint32_t sk_index() const
        {
            return static_cast<uint32_t>(smallKey >> 32); // 32 бита [63...32]
        }

        __inline void index(uint32_t collisionIndex, uint32_t skIndex)
        {
            smallKey = static_cast<uint64_t>(skIndex) << 32 | ((collisionIndex & 0x7FFFFFFFULL) << 1);
        }

#if defined(SIZE_IN_SMALL_KEY)
        __inline uint32_t size() const
        {
            return ((static_cast<uint32_t>(smallKey) >> 1) ^ static_cast<uint32_t>(l3)) & 0x7FFFFU; // 19 бит
        }

        __inline void size(uint32_t size)
        {
            smallKey = (smallKey & ~0xFFFFFULL) | (((size ^ static_cast<uint32_t>(l3)) & 0x7FFFFULL) << 1) | 1;
        }
#else
        __inline uint32_t size() const
        {
            return static_cast<uint32_t>(l1) & 0x7FFFFU; // 19 бит
        }

        __inline void size(uint32_t size)
        {
            l1 = (l1 & ~0x7FFFFULL) | (size & 0x7FFFFU); smallKey |= 1;
        }
#endif
    };

    struct FragmentInfoKey
    {
        __inline bool operator==(const FragmentInfoKey& key) const
        {
            return _Low == key._Low && _High == key._High;
        }

        uint64_t _Low;
        uint64_t _High;
    };

    struct FragmentInfo
    {
        uint32_t skIndex;
        uint32_t fileIndex;

        union
        {
            int64_t fileOffset;
            LargeKey lk;
        };

        const FragmentInfoKey& asKey() const
        {
            return *reinterpret_cast<const FragmentInfoKey*>(this);
        }
    };

#pragma pack(pop)


    static_assert(sizeof(LargeKey) == 32, "LargeKey must be 32 bytes");

    static_assert(sizeof(FragmentInfo) == 40, "FragmentInfo must be 40 bytes");

	class LargeKeyStorage
	{
        using HashIndexType = SimdHash::Index<uint64_t, SimdHash::Hash<uint64_t, SimdHash::HashType::Absl32>, SimdHash::Mode::Fast, true>;

        using HashIndexLargeKeyType = SimdHash::Index<LargeKey, SimdHash::Hash<LargeKey, SimdHash::HashType::Absl32>, SimdHash::Mode::Fast, true>;

        using MapType = SimdHash::Map<FragmentInfoKey, uint32_t>;

        HashIndexType hi;

        HashIndexLargeKeyType hiCollision;

        struct
        {
            HashIndexLargeKeyType hi;

            uint32_t index = 0;

        } lhSelector[2];

        RangeMapper rm;

        blake3_hasher fragmentHasher;

        __declspec(align(64)) XXH3_state_t lksHasher;

        std::vector<FragmentInfo> fiBuffer;

        std::vector<LargeKey> lkBuffer;

        File lkDatFile, fiLogFile;

        MapType fiReMap;

    public:

        LargeKeyStorage(const wchar_t* logPath = nullptr)
        {
            // bLow == false
            lhSelector[0].index = HashIndexType::MAX_SIZE + (static_cast<uint64_t>(HashIndexType::MAX_SIZE) * 2 - HashIndexType::MAX_SIZE) / 2;
            // bLow == true
            lhSelector[1].index = HashIndexType::MAX_SIZE;

            blake3_hasher_init(&fragmentHasher);

            fiBuffer.reserve(6 * find_aligment_for_4096(sizeof(FragmentInfo)));

            lkBuffer.reserve(10 * find_aligment_for_4096(sizeof(LargeKey)));

            std::wstring fiLogPath = L"fi.log", lkDataPath = L"lk.dat";

            if (logPath != nullptr)
            {
                fiLogPath = logPath + L'/' + fiLogPath;
            }

            fiLogFile.Create(fiLogPath.c_str(), true, false);
            assert(fiLogFile.IsOpen(), "%s\n", fiLogFile.GetLastErrorA().c_str());

            if (logPath != nullptr)
            {
                lkDataPath = logPath + L'/' + lkDataPath;
            }

            lkDatFile.Create(lkDataPath.c_str(), true, false);
            assert(lkDatFile.IsOpen(), "%s\n", lkDatFile.GetLastErrorA().c_str());

            LargeKey lk = { 0 };

            hi.Add(lkBuffer.emplace_back(lk).smallKey);

            XXH3_64bits_reset(&lksHasher);
        }

        uint32_t Count(bool bLow) const
        {
            return lhSelector[bLow].hi.Count();
        }

        uint32_t Count() const
        {
            return hi.Count();
        }

        uint32_t CollisionCount() const
        {
            return hiCollision.Count();
        }

        __inline uint32_t remap(uint32_t input)
        {
            return (input <= HashIndexType::MAX_SIZE) ? input : rm.remap(input);
        }

        void FragmentToLargeKey(const uint8_t* fragment, uint32_t fragmentSize, LargeKey& lk)
        {
            blake3_hasher_reset(&fragmentHasher);
#if DEBUG_FRAGMENT_SIZE
            blake3_hasher_update(&fragmentHasher, fragment, DEBUG_FRAGMENT_SIZE);
#else
            blake3_hasher_update(&fragmentHasher, fragment, fragmentSize);
#endif
            blake3_hasher_finalize(&fragmentHasher, lk.value, sizeof(lk.value));

            lk.size(fragmentSize);

#if DEBUG_COLLISION
            std::fill_n(lk.value + sizeof(lk.smallKey) - DEBUG_COLLISION, DEBUG_COLLISION, 0);
#endif
        }

        bool Add(const uint8_t* fragment, uint32_t fragmentSize, uint32_t fileIndex, int64_t fileOffset, bool bLow)
        {
            FragmentInfo& fi = fiBuffer.emplace_back();

            FragmentToLargeKey(fragment, fragmentSize, fi.lk);
            
            const auto bResult = AddToSelector(fi, bLow);

            fi.fileIndex = fileIndex;

#if !DEBUG_FRAGMENT_INFO
            fi.fileOffset = fileOffset;
#endif
            if (fiBuffer.size() == fiBuffer.capacity())
            {
                WriteToDisk(fiBuffer, fiLogFile);
            }

            return bResult;
        }

    private:

        template <typename T>
        __inline void WriteToDisk(std::vector<T>& buffer, File& file)
        {
            if (buffer.size() > 0)
            {
                if (buffer.size() != buffer.capacity())
                {
                    constexpr auto min_size = find_aligment_for_4096(sizeof(T));

                    const auto size = min_size - (buffer.size() % min_size);

                    T value = { 0 };

                    if constexpr (std::is_same_v<T, FragmentInfo>)
                    {
                        value.fileIndex = ~0u;
                    }

                    for (auto i = 0; i < size; i++)
                    {
                        buffer.push_back(value);
                    }
                }

                file.Write(buffer); buffer.resize(0);
            }
        }

    public:

        void Load(const std::vector<LargeKey>& lks)
        {
            // rewrite please!
            for (const auto& clk : lks)
            {
                assert(clk.smallKey != 0 && hi.Add(clk.smallKey));

                lkBuffer.push_back(clk);

                if (lkBuffer.size() == lkBuffer.capacity())
                {
                    WriteToDisk(lkBuffer, lkDatFile);
                }

                if (!clk.hasSize())
                {
                    LargeKey lk = clk;

                    assert(hiCollision.Count() == lk.collision_index());

                    lk.smallKey = hi.GetKey(lk.sk_index());

                    assert(hiCollision.Add(lk));
                }
            }
        }

private:

        bool AddToSelector(FragmentInfo& fi, bool bLow)
        {
            if (hi.TryGetIndex(fi.lk.smallKey, fi.skIndex))
            {
                uint32_t ckIndex;

                if (hiCollision.TryGetIndex(fi.lk, ckIndex))
                {
                    fi.lk.index(ckIndex, fi.skIndex);

                    assert(hi.TryGetIndex(fi.lk.smallKey, fi.skIndex));
                }

                return false;
            }

            auto& selector = lhSelector[bLow];

            if (selector.hi.TryAdd(fi.lk, fi.skIndex))
            {
                fi.skIndex = ++selector.index;
                
                return true;
            }

            // дубликат, уже есть в слекторе
            fi.skIndex += (selector.index - selector.hi.Count() + 1);
            
            return false;
        }

        __inline uint64_t GetFingerPrint()
        {
            auto hasher = lksHasher;

            return XXH3_64bits_digest(&hasher);
        }

        uint64_t GetFingerPrint(const std::vector<LargeKey>& lks)
        {
            XXH3_64bits_update(&lksHasher, lks.data(), sizeof(LargeKey) * lks.size());

            return GetFingerPrint();
        }

        __inline void CalcFingerPrint(const LargeKey& clk)
        {
            XXH3_64bits_update(&lksHasher, &clk, sizeof(LargeKey));
        }

        uint64_t GetFingerPrint(const LargeKey& clk)
        {
            CalcFingerPrint(clk); return GetFingerPrint();
        }
public:

        // возвращаем ключи блока и текущий fingerprint
        uint64_t GetLargeKeys(std::vector<LargeKey>& buffer, bool bLow)
        {
            uint32_t startIndex = hi.Count(), skIndex, ckIndex;

            auto& selector = lhSelector[bLow];

            buffer.resize(0); assert(selector.hi.Count() != 0);

            for (const auto& clk : selector.hi)
            {
                auto& lk = lkBuffer.emplace_back(clk);

                if (!hi.TryAdd(lk.smallKey, skIndex))
                {
                    assert(hiCollision.TryAdd(lk, ckIndex));

                    lk.index(ckIndex, skIndex);

                    assert(hi.Add(lk.smallKey));
                }

                if (lkBuffer.size() == lkBuffer.capacity())
                {
                    WriteToDisk(lkBuffer, lkDatFile);
                }

                buffer.push_back(clk); // только оригинальные ключи
            }

            const auto size = static_cast<uint32_t>(buffer.size());

            rm.addRange(bLow, selector.index + 1 - size, startIndex, size);
            
            selector.hi.Clear();

            return GetFingerPrint(buffer);
        }

        using ReadActionType = std::function<bool(uint32_t fragmentSize, uint32_t fileIndex, uint64_t fileOffset)>;

        using ReadyEventType = std::function<void(uint32_t fragmentSize, const LargeKey& lk)>;

        uint64_t ResolveCollisions(const std::vector<uint8_t>& fragmentBuffer, const ReadActionType& readAction, const ReadyEventType& readyEvent)
        {
            assert(lhSelector[0].hi.Count() == 0 && lhSelector[1].hi.Count() == 0 && fiReMap.Count() == 0);

            WriteToDisk(fiBuffer, fiLogFile);

            const auto lk_in_last_page = ((lkBuffer.size() * sizeof(LargeKey)) % 4096) / sizeof(LargeKey);

            if (lk_in_last_page) // оставляем записи которые попали в последнию страницу
            {
                std::vector<LargeKey> temp;

                temp.assign(lkBuffer.end() - lk_in_last_page, lkBuffer.end());

                WriteToDisk(lkBuffer, lkDatFile);

                lkBuffer.assign(temp.begin(), temp.end());
            }
            else
            {
                WriteToDisk(lkBuffer, lkDatFile);
            }

            MZ::ExternalStructSort<FragmentInfo> sorter(fiLogFile.Size(),
                [](const FragmentInfo& a, const FragmentInfo& b)
                {
                    return a.skIndex < b.skIndex;
                });

            sorter.ChunkSort(fiLogFile, [this](FragmentInfo& record)
                { 
                    record.skIndex = rm.remap(record.skIndex);
                });

            rm.validate(++lhSelector[0].index, ++lhSelector[1].index);

            lkDatFile.SeekBegin(0); assert(0 == (lkDatFile.Size() % 4096));

            uint32_t skIndexL = 0, skIndexR = 0, lkReadBufferSize = static_cast<uint32_t>(128ull * 1024 / sizeof(LargeKey));

            std::vector<LargeKey> lkReadBuffer(lkReadBufferSize), lkTempBuffer(4096 / sizeof(LargeKey));

            SimdHash::Set<LargeKey> dup;

            uint32_t skIndex = 0, ckIndex = 0, hiIndexMaxValue = hi.Count();
            
            sorter.Sort(fiLogFile,
                [&](const FragmentInfo& fi)
                {
                    if (fi.skIndex == 0) return;

                    assert(fi.skIndex < hiIndexMaxValue,
                        "lkReadBuffer: %u, fi.skIndex: %u / 0x%x, skIndexR: %u",
                        (uint32_t)lkReadBuffer.size(), fi.skIndex, fi.skIndex, skIndexR);

                    if (fi.skIndex >= skIndexR)
                    {
                        assert(lkReadBufferSize == lkReadBuffer.size());

                        const auto idx = fi.skIndex / lkReadBufferSize * lkReadBufferSize;

                        lkReadBuffer.resize(lkDatFile.Read(idx, lkReadBuffer));

                        assert(lkReadBuffer.size() != 0);

                        skIndexL = idx; skIndexR = idx + static_cast<uint32_t>(lkReadBuffer.size());
                    }

                    assert(fi.skIndex >= skIndexL && fi.skIndex < skIndexR);

                    const auto& lk = lkReadBuffer[fi.skIndex % lkReadBufferSize];

#if DEBUG_FRAGMENT_INFO

                    // проверяем только в отладке, потому что fi.lk.smallKey == fi.fileOffset
                    if (lk.hasSize()) // в lk.dat смешанные ключи, уникальные по sk
                    {
                        assert(lk.smallKey == fi.lk.smallKey, 
                            "fi.skIndex: %u, skIndexR: %u, lkReadBuffer: %u",
                            fi.skIndex, skIndexR, (uint32_t)lkReadBuffer.size());
                    }
#endif
                    if (lk.shortCmp(fi.lk)) return; // проверяем колизию

                    assert(lk.hasSize(), "EPRST/EKLMN"); // если hasSize()==false то глобальная EPRST/EKLMN!

                    // нужен полностю восстановленый clk фрагмента, чтобы получить размер фрагмента
                    LargeKey clk = fi.lk; clk.smallKey = lk.smallKey;

                    if (hiCollision.TryGetIndex(clk, ckIndex))
                    {
                        clk.index(ckIndex, fi.skIndex);

                        // проверям что исходник существует, получаем его индекс
                        assert(hi.TryGetIndex(clk.smallKey, skIndex));

                        assert(fiReMap.Add(fi.asKey(), skIndex));

                        return;
                    }

                    if (!readAction(clk.size(), fi.fileIndex, fi.fileOffset))
                    {
                        assert(fiReMap.Add(fi.asKey(), 0)); // выкидываем файл с этим фрагментом

                        return;
                    }

                    const auto fragmentSize = clk.size();

                    FragmentToLargeKey(fragmentBuffer.data(), fragmentSize, clk);

                    // фрагмент файла не изменился, можно добавить в hiCollision
                    if (clk.smallKey == lk.smallKey && clk.shortCmp(fi.lk))
                    {
                        CalcFingerPrint(clk);

                        readyEvent(fragmentSize, clk); // оригинальный ключ и fingerprint

                        assert(hiCollision.TryAdd(clk, ckIndex));

                        clk.index(ckIndex, fi.skIndex);

                        assert(hi.TryAdd(clk.smallKey, skIndex)); // получаем новый индекс

                        lkBuffer.push_back(clk);

                        assert(fiReMap.Add(fi.asKey(), skIndex)); // remap на новый индекс

                        return;
                    }

                    assert(fiReMap.Add(fi.asKey(), 0)); // выкидываем файл с этим фрагментом
#if 0
                    // фрагмент изменился и он есть в hiCollision
                    if (hiCollision.TryGetIndex(clk, ckIndex))
                    {
                        clk.index(ckIndex, fi.skIndex);

                        assert(hi.TryGetIndex(clk.smallKey, skIndex)); // проверка, должен быть!

                        return;
                    }

                    // фрагмент изменился и его нет в hi
                    bool bAddClk = !hi.TryGetIndex(clk.smallKey, skIndex);

                    if (!bAddClk) // фрагмент изменился, есть в hi
                    {
                        const auto idx = static_cast<uint32_t>(skIndex / lkTempBuffer.size() * lkTempBuffer.size());

                        assert(lkTempBuffer.size() == lkDatFile.Read(idx, lkTempBuffer));

                        const auto& _lk = lkTempBuffer[skIndex % lkTempBuffer.size()];

                        assert(_lk.smallKey == clk.smallKey);

                        bAddClk = !_lk.shortCmp(clk);
                    }

                    if (bAddClk && dup.Add(clk))
                    {
                        lkBuffer.push_back(clk); CalcFingerPrint(clk);

                        readyEvent(fragmentSize, clk); // оригинальный ключ и fingerprint
                    }
#endif
                });

            
            //std::wcout << L"fiRemap.Count(): " << fiRemap.Count() << std::endl;

            if (lkBuffer.size())
            {
                if (lk_in_last_page < lkBuffer.size())
                {
                    lkDatFile.SeekEnd((lk_in_last_page) ? -4096 : 0);

                    WriteToDisk(lkBuffer, lkDatFile);
                }

                lkBuffer.resize(0);
            }

            return GetFingerPrint();
        }

        void GetFileIndexInfo(const std::function<void(uint32_t fileIndex, const std::vector<uint32_t>& fragmentIndex)>& eventReady)
        {
            MZ::ExternalStructSort<FragmentInfo> sorter(fiLogFile.Size(),
            [](const FragmentInfo& a, const FragmentInfo& b)
            {
                if (a.fileIndex < b.fileIndex) return true;

                if (a.fileIndex == b.fileIndex) return a.fileOffset < b.fileOffset;

                return false;
            });

            std::vector<uint32_t> fragmentIndex; fragmentIndex.reserve(16ull * 1024);

            struct
            {
                uint32_t    fileIndex = 0;
                bool        bUse = true;

            } current;

            std::function<void(FragmentInfo& record)> preSortAction = nullptr;

            if (fiReMap.Count())
            {
                preSortAction = [this](FragmentInfo& fi)
                {
                    fiReMap.TryGetValue(fi.asKey(), fi.skIndex);
                };
            }
            
            sorter.ChunkSort(fiLogFile, nullptr, [&](MZ::FragmentInfo& fi)
            {
                if (~0u == fi.fileIndex) return;

                if (current.fileIndex == fi.fileIndex)
                {
                    if (current.bUse)
                    {
                        if (fi.skIndex != 0)
                        {
                            fiReMap.TryGetValue(fi.asKey(), fi.skIndex);

                            fragmentIndex.push_back(fi.skIndex);
                        }
                        else
                        {
                            current.bUse = false;
                        }
                    }
                }
                else
                {
                    if (current.bUse && fragmentIndex.size())
                    {
                        eventReady(current.fileIndex, fragmentIndex);
                    }

                    fragmentIndex.resize(0);

                    current = { fi.fileIndex, fi.skIndex != 0 };

                    if (current.bUse) fragmentIndex.push_back(fi.skIndex);
                }
            });

            if (current.bUse && fragmentIndex.size())
            {
                eventReady(current.fileIndex, fragmentIndex);
            }
        }
	};
}
