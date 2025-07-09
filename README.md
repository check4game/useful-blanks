```
#include "CFile.h"
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
