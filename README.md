```
#include "ExternalStructSort.h"

#pragma pack(push, 1)
struct Record
{
    uint32_t index;
    uint32_t file;

    byte data[40];

};
#pragma pack(pop)

MZ::ExternalStructSort<Record, &Record::index>::Sort(f,
   [&](const Record& record)
   {
       sortedRecords++;

   }, 128ull * 1024 * 1024
);

```

```
#include "CDC.h"

MZ::CDC::Zpaq<8192> cdc;

cdc.Cut(
    [&](uint32_t seek, uint32_t& size) -> uint8_t*
    {
        data_pos += seek;
        
        if (data_pos >= data.size()) return nullptr;

        size = (uint32_t)(data.size() - data_pos);

        return data.data() + data_pos;
    },
    [&](const uint8_t* data, uint32_t size, float hits)
    {
          std::cout << size << ',' << hits << std:endl;
    }
);
```
