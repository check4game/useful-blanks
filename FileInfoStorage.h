#pragma once

#include "StringStorage.h"

template <typename CharT>
class FileInfoStorage
{
	StringStorage<CharT> ss;

	struct FileInfoEntry
	{
		uint32_t dIndex;

		size_t size;

		const CharT* name;
	};

	GrowingMemoryPool<FileInfoEntry> pool;

public:

	explicit FileInfoStorage(size_t page_size = 1024 * 1024) : ss(page_size), pool(page_size) {}

	void Add(const CharT* path, const CharT* name, size_t size)
	{
		auto ptr = pool.allocate();
	
		ptr->dIndex = ss.GetOrAdd(path);

		ptr->name = ss.MakeString(name);

		ptr->size = size;
	}

	void clear()
	{
		ss.Clear(); pool.release();
	}
};

