#pragma once

#include <cstddef>
#include <vector>
#include <memory>
#include <stack>

#include <Assert.h>

template<typename Type>
class GrowingMemoryPool
{
    struct MemoryPage
    {
        void* _ptr;

        size_t _size;
        size_t _used;

        MemoryPage(size_t size) : _size(size), _used(0)
        {
            _ptr = ::operator new(size);
        }

        ~MemoryPage()
        {
            ::operator delete(_ptr);
        }
        
        MemoryPage(const MemoryPage&) = delete;
        MemoryPage& operator=(const MemoryPage&) = delete;
    };

    struct Checkpoint
    {
        size_t _page;
        size_t _used;

        Checkpoint(size_t page, size_t used) : _page(page), _used(used) {}
    };

    using PagesType = std::vector<std::unique_ptr<MemoryPage>>;

    using CheckpointsType = std::stack<Checkpoint>;

    PagesType _pages;

    CheckpointsType _checkpoints;

    size_t _page_size;

    static constexpr size_t MIN_PAGE_SIZE = 4096;

public:
    explicit GrowingMemoryPool(size_t page_size = 1024 * 1024)
    {
        if (page_size < MIN_PAGE_SIZE) page_size = MIN_PAGE_SIZE;

        _page_size = ((page_size + MIN_PAGE_SIZE) / MIN_PAGE_SIZE - 1) * MIN_PAGE_SIZE;
    }

    ~GrowingMemoryPool() = default;

    template<typename... Args>
    Type* construct(Args&&... args)
    {
        const auto memory = allocateMemory(sizeof(Type));

        return new (memory) Type(std::forward<Args>(args)...);
    }

    Type* allocate(size_t n = 1)
    {
        if constexpr (sizeof(Type) > 1)
            return reinterpret_cast<Type*>(allocateMemory(n * sizeof(Type)));
        else
            return reinterpret_cast<Type*>(allocateMemory(n));
    }

    void checkpoint()
    {
        if (_pages.empty())
        {
            assert(_checkpoints.empty());
                
            _checkpoints.push(Checkpoint(0, 0));
        }
        else if (_checkpoints.empty())
        {
            _checkpoints.push(Checkpoint(_pages.size() - 1, _pages.back()->_used));
        }
        else
        {
            const auto& top = _checkpoints.top();

            assert(!(top._page == (_pages.size() - 1) && top._used == _pages.back()->_used));

            _checkpoints.push(Checkpoint(_pages.size() - 1, _pages.back()->_used));
        }
    }
    
    void discard_checkpoint()
    {
        if (!_checkpoints.empty())
        {
            _checkpoints.pop();
        }
    }

    void rollback_checkpoint()
    {
        if (!_checkpoints.empty())
        {
            Checkpoint checkpoint = _checkpoints.top(); _checkpoints.pop();

            while (_pages.size() > checkpoint._page + 1)
            {
                _pages.pop_back();
            }

            if (!_pages.empty() && _pages.size() == checkpoint._page + 1)
            {
                _pages.back()->_used = checkpoint._used;
            }
        }
    }
    
    void release()
    {
        CheckpointsType().swap(_checkpoints);

        PagesType().swap(_pages);
    }

private:

    MemoryPage& allocatePage(size_t size)
    {
        return *_pages.emplace_back(std::make_unique<MemoryPage>(size)).get();
    }

    void* allocateMemory(size_t size = 1)
    {
        if (size == 0) return nullptr;

        assert(size <= _page_size);

        if (_pages.size() > 0)
        {
            auto& page = *_pages.back().get();

            if (page._used + size <= page._size)
            {
                auto* ptr = static_cast<char*>(page._ptr) + page._used;

                page._used += size; return ptr;
            }
        }

        auto& np = allocatePage(_page_size);

        np._used = size; return np._ptr;
    }

    __forceinline size_t GetPageSize() const
    {
        return _page_size;
    }

    __forceinline size_t GetLastPageIndex() const
    {
        return _pages.size() - 1;
    }

    __forceinline size_t GetLastPageOffset() const
    {
        return _pages[GetLastPageIndex()]->_used;
    }

    const Type* getType(size_t page, size_t offset) const
    {
        return reinterpret_cast<const Type*>(_pages[page]->_ptr) + offset / sizeof(Type);
    }

    const class iterator
    {
    private:
        const GrowingMemoryPool<Type>* pool;

        size_t page = 0, offset = 0;

    public:
        using iterator_category = std::forward_iterator_tag;

        using difference_type = std::ptrdiff_t;

        iterator(const GrowingMemoryPool<Type>* pool, size_t page, size_t offset)
        {
            this->pool = pool; this->page = page; this->offset = offset;
        }

        const Type& operator*() const
        {
            return *pool->getType(page, offset);
        }

        iterator& operator++()
        {
            offset += sizeof(Type);

            if ((offset + sizeof(Type)) < pool->GetPageSize())
            {
                return *this;
            }

            offset = 0; page++; return *this;
        }

        iterator operator++(int)
        {
            iterator tmp = *this;

            offset += sizeof(Type);

            if ((offset + sizeof(Type)) < pool->GetPageSize())
            {
                return tmp;
            }
            
            offset = 0; page++; return tmp;
        }

        bool operator==(const iterator& other) const
        {
            return pool == other.pool && offset == other.offset && page == other.page;
        }

        bool operator!=(const iterator& other) const
        {
            return !(*this == other);
        }
    };

public:

    const iterator begin() const
    {
        return iterator(this, 0, 0);
    }

    const iterator end() const
    {
        return iterator(this, GetLastPageIndex(), GetLastPageOffset());
    }
};
