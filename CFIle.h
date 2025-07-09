#pragma once

#include <Windows.h>
#include <string>

#if defined(assert)
#undef assert
#endif

#define assert(cond) \
    do {                \
        if (!(cond)) {  \
            fprintf(stderr, "%s:%d: Assertion failed in function '%s': %s\n", __FILE__, __LINE__, __FUNCSIG__, #cond); \
            abort();    \
        }               \
    } while (false)

namespace MZ
{
    class CFile
    {
        HANDLE fileHandle = INVALID_HANDLE_VALUE;

        DWORD lastError = 0;

    public:

        std::string GetLastErrorAsString()
        {
            auto wstr = GetLastError();

            if (wstr.empty()) return {};

            int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), nullptr, 0, nullptr, nullptr);

            std::string str(size_needed, 0);

            WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, nullptr, nullptr);

            return str;


            //std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

            //return converter.to_bytes(GetLastError());
        }

        std::wstring GetLastError()
        {
            assert(lastError != 0);

            LPVOID lpMsgBuf;
            FormatMessage(
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                lastError,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPTSTR)&lpMsgBuf,
                0,
                NULL
            );

            std::wstring errorMessage(static_cast<LPCWSTR>(lpMsgBuf));

            LocalFree(lpMsgBuf);

            return std::to_wstring(lastError) + L", " + errorMessage;
        }

    private:

        bool OpenExist(const wchar_t* path, const DWORD dwDesiredAccess, const DWORD dwShareMode, const DWORD flags)
        {
            assert(IsInvalid());

            fileHandle = CreateFile(path, dwDesiredAccess, dwShareMode,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | flags, nullptr);

            if (IsInvalid()) lastError = ::GetLastError();

            return !IsInvalid();
        }

        bool Create(const wchar_t* path, const DWORD dwDesiredAccess, const DWORD dwShareMode, const DWORD flags)
        {
            assert(IsInvalid());

            fileHandle = CreateFile(path, dwDesiredAccess, dwShareMode,
                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | flags, nullptr);

            if (IsInvalid()) lastError = ::GetLastError();

            return !IsInvalid();
        }

        DWORD ReadInternal(byte* buffer, DWORD nNumberOfBytesToRead)
        {
            assert(!IsInvalid());

            DWORD NumberOfBytesRead = 0;

            if (!::ReadFile(fileHandle, buffer, nNumberOfBytesToRead, &NumberOfBytesRead, nullptr))
            {
                lastError = ::GetLastError();

                throw std::runtime_error{ GetLastErrorAsString() };
            }

            return NumberOfBytesRead;
        }

        void WriteInretnal(const byte* buffer, DWORD nNumberOfBytesToWrite)
        {
            assert(!IsInvalid());

            DWORD NumberOfBytesWritten = 0;

            if (!::WriteFile(fileHandle, buffer, nNumberOfBytesToWrite, &NumberOfBytesWritten, nullptr))
            {
                lastError = ::GetLastError();

                throw std::runtime_error{ GetLastErrorAsString() };
            }
        }

        int64_t Seek(int64_t dist, DWORD dwMoveMethod)
        {
            LARGE_INTEGER li = { 0 }; li.QuadPart = dist;

            if (!::SetFilePointerEx(fileHandle, li, &li, dwMoveMethod))
            {
                lastError = ::GetLastError();

                throw std::runtime_error{ GetLastErrorAsString() };
            }

            return li.QuadPart;
        }

    public:

        size_t Size()
        {
            assert(!IsInvalid());

            LARGE_INTEGER size = { 0 };

            assert(GetFileSizeEx(fileHandle, &size));

            return size.QuadPart;

        }

        bool IsInvalid()
        {
            return INVALID_HANDLE_VALUE == fileHandle;
        }

        ~CFile()
        {
            Close();
        }

        void Close()
        {
            if (!IsInvalid())
            {
                ::CloseHandle(fileHandle); fileHandle = INVALID_HANDLE_VALUE; lastError = 0;
            }
        }

        bool Open(const wchar_t* path, bool bWrite = true, bool NoBuffering = true)
        {
            DWORD flags = (NoBuffering) ? FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN : 0;

            if (!bWrite)
                return OpenExist(path, GENERIC_READ, FILE_SHARE_READ, flags);
            else
                return OpenExist(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, flags);
        }

        bool Create(const wchar_t* path, bool NoBuffering = true)
        {
            return Create(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, (NoBuffering) ? FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN : 0);
        }

        void Write(const byte* buffer, size_t nNumberOfBytesToWrite, uint32_t blockSize = 128u * 1024)
        {
            for (const byte* begin = buffer, *end = (buffer + nNumberOfBytesToWrite); begin < end; begin += blockSize)
            {
                if ((end - begin) < (int64_t)blockSize)
                {
                    WriteInretnal(begin, static_cast<DWORD>(end - begin)); break;
                }

                WriteInretnal(begin, static_cast<DWORD>(blockSize));
            }
        }

        DWORD Read(byte* buffer, size_t nNumberOfBytesToRead, uint32_t blockSize = 128u * 1024)
        {
            DWORD NumberOfBytesRead = 0;

            for (byte* begin = buffer, *end = (buffer + nNumberOfBytesToRead); begin < end; begin += blockSize)
            {
                if ((end - begin) < (int64_t)blockSize)
                {
                    NumberOfBytesRead += ReadInternal(begin, static_cast<DWORD>(end - begin)); break;
                }

                NumberOfBytesRead += ReadInternal(begin, static_cast<DWORD>(blockSize));
            }

            return NumberOfBytesRead;
        }

        int64_t SeekBegin(int64_t dist)
        {
            return Seek(dist, FILE_BEGIN);
        }

        int64_t SeekCurrent(int64_t dist)
        {
            return Seek(dist, FILE_CURRENT);
        }

        int64_t SeekBack(int64_t dist)
        {
            assert(dist >= 0);

            return Seek(-dist, FILE_CURRENT);
        }

        int64_t SeekEnd(int64_t dist)
        {
            return Seek(dist, FILE_END);
        }
    };

}