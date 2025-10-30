#pragma once

#include <string>
#include <cstring>
#include <functional> 

#include "Assert.h"
#include "SimdHash.h"

#include "GrowingMemoryPool.h"

template<typename CharT, size_t strSizeInBytes = 2>
class StringStorage 
{
    static_assert(std::is_same_v<CharT, char> || std::is_same_v<CharT, wchar_t>, "must be either char or wchar_t");

    static_assert(strSizeInBytes >= 1 && strSizeInBytes <= 4, "only 1, 2, 3, 4 bytes");

private:

    using HashFuncType = std::size_t(*)(const CharT*);

    struct StringHash
    {
        __forceinline uint64_t operator()(const CharT* str) const noexcept
        {
            return std::hash<std::basic_string_view<CharT>>{}(std::basic_string_view<CharT>(str, StringLength(str)));
        }
    };

    struct StringEqual
    {
        __forceinline bool operator()(const CharT* lstr, const CharT* rstr) const
        {
            if (lstr == rstr) return true;

            const auto llen = StringLength(lstr);
            const auto rlen = StringLength(rstr);

            if (llen != rlen) return false;

            return std::char_traits<CharT>::compare(lstr, rstr, llen) == 0;
        }
    };

    GrowingMemoryPool<char> pool;
    
    MZ::SimdHash::Index<const CharT*, StringHash, StringEqual> strings;

    static uint32_t StringLength(const CharT* str)
    {
        const auto memory = reinterpret_cast<const char*>(str) - strSizeInBytes;

        if constexpr (strSizeInBytes == 1)
        {
            *reinterpret_cast<const uint16_t*>(memory);
        }
        else if constexpr (strSizeInBytes == 2)
        {
            return *reinterpret_cast<const uint16_t*>(memory);
        }
        else if constexpr (strSizeInBytes == 3)
        {
            *reinterpret_cast<const uint32_t*>(memory);
        }
        else
        {
            *reinterpret_cast<const uint32_t*>(memory);
        }
    }

public:

    const CharT* MakeString(const CharT* source)
    {
        assert(source != nullptr);

        const auto len = static_cast<uint32_t>(std::char_traits<CharT>::length(source) + 1);

        auto memory = static_cast<char*>(pool.allocate(len * sizeof(CharT) + strSizeInBytes));

        assert(memory != nullptr);

        if constexpr (strSizeInBytes == 1)
        {
            assert(len < 0xFF); *reinterpret_cast<uint16_t*>(memory) = static_cast<uint16_t>(len);
        }
        else if constexpr (strSizeInBytes == 2)
        {
            assert(len < 0xFFFF); *reinterpret_cast<uint16_t*>(memory) = static_cast<uint16_t>(len);
        }
        else if constexpr (strSizeInBytes == 3)
        {
            assert(len < 0xFFFFFF); *reinterpret_cast<uint32_t*>(memory) = len;
        }
        else
        {
            *reinterpret_cast<uint32_t*>(memory) = len;
        }

        return std::char_traits<CharT>::copy(reinterpret_cast<CharT*>(memory + strSizeInBytes), source, len);
    }

    void Clear()
    {
        strings.Clear(strings.MIN_SIZE); pool.release();
    }

    explicit StringStorage(size_t page_size = 1024 * 1024)
        : pool(page_size), strings(strings.MIN_SIZE, StringHash(), StringEqual())
    {
    }

    uint32_t GetOrAdd(const CharT* source)
    {
        pool.checkpoint();

        const auto str = MakeString(source);

        uint32_t index;

        if (!strings.TryAdd(str, index))
        {
            pool.rollback_checkpoint(); return index;
        }
        
        pool.discard_checkpoint(); return index;
    }

    uint32_t GetOrAdd(const std::basic_string<CharT>& source)
    {
        return GetOrAdd(source.c_str());
    }
    
    const CharT* Get(uint32_t id) const
    {
        return (id < strings.Count()) ? strings.GetKey(id) : nullptr;
    }
    
    const std::basic_string_view<CharT> GetString(uint32_t id) const
    {
        const auto str = Get(id);

        return str ? std::basic_string_view<CharT>(str) : std::basic_string_view<CharT>();
    }

    uint32_t Get(const CharT* source)
    {
        pool.checkpoint();

        const auto str = MakeString(source);

        uint32_t index = UINT32_MAX;

        strings.TryGetIndex(str, index);

        pool.rollback_checkpoint();

        return index;
    }

    uint32_t Get(const std::basic_string<CharT>& str) const
    {
        return Get(str.c_str());
    }

    bool Contains(const CharT* source) const
    {
        return Get(source) != UINT32_MAX;
    }

    bool Contains(const std::basic_string<CharT>& str) const
    {
        return Contains(str.c_str());
    }

    bool Contains(uint32_t id) const
    {
        return id < strings.Count();
    }
    
    uint32_t Count() const
    {
        return static_cast<uint32_t>(strings.Count());
    }
    
    const class iterator
    {
    private:
        const StringStorage* storage;

        uint32_t id;

    public:
        using iterator_category = std::forward_iterator_tag;

        using value_type = std::pair<uint32_t, std::basic_string_view<CharT>>;

        using difference_type = std::ptrdiff_t;

        using pointer = value_type*;

        using reference = value_type&;

        iterator(const StringStorage* storage, uint32_t id)
        {
            this->storage = storage; this->id = id;
        }

        value_type operator*() const
        {
            return { id, storage->GetString(id) };
        }

        iterator& operator++()
        {
            ++id; return *this;
        }

        iterator operator++(int)
        {
            iterator tmp = *this; ++id; return tmp;
        }

        bool operator==(const iterator& other) const
        {
            return storage == other.storage && id == other.id;
        }

        bool operator!=(const iterator& other) const
        {
            return !(*this == other);
        }
    };

    const iterator begin() const
    { 
        return iterator(this, 0);
    }

    const iterator end() const
    {
        return iterator(this, Count());
    }

    StringStorage(const StringStorage&) = delete;
    StringStorage& operator=(const StringStorage&) = delete;
};
