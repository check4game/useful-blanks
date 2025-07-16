#pragma once

#include <windows.h>

#include <string>
#include <stdexcept>
#include <functional>

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
    std::string WStringToAString(const std::wstring& wstr)
    {
        if (wstr.empty()) return {};

        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), nullptr, 0, nullptr, nullptr);

        std::string str(size_needed, 0);

        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, nullptr, nullptr);

        return str;
    }

    std::wstring GetLastErrorW(DWORD error)
    {
        LPVOID lpMsgBuf;

        FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR)&lpMsgBuf,
            0,
            NULL
        );

        std::wstring errorMessage(static_cast<wchar_t*>(lpMsgBuf));

        LocalFree(lpMsgBuf);

        return std::to_wstring(error) + L", " + errorMessage;
    }

    static int64_t FileTimeToDecimal(const FILETIME& fileTime)
    {
        /*
        FILETIME localTime;
        FileTimeToLocalFileTime(&fd.ftCreationTime, &localTime);
        */

        SYSTEMTIME st;

        if (FileTimeToSystemTime(&fileTime, &st))
        {
            return st.wYear * 10000000000ll + st.wMonth * 100000000ll + st.wDay * 1000000
                + st.wHour * 10000ll + st.wMinute * 100ll + st.wSecond;
        }

        return 1900 * 10000000000ll + 100000000ll + 1000000 + 10000 + 100;
    }

    class FileEnumerator
    {
    private:

        WIN32_FIND_DATA fd {};

        std::wstring buffer, prefix;

        FileEnumerator(const std::wstring& prefix) 
        {
            buffer.reserve(0xFFFF); this->prefix = prefix;

            if (prefix.size()) buffer = prefix;
        }

    public:


        static void Enumerate(const std::wstring prefix, const std::wstring path, std::function<void(const std::wstring& path, const wchar_t* name, int64_t mt, int64_t size)> fileAction, std::function<void(const std::wstring& path, const std::wstring& error)> errorAction)
        {
            assert(path.size() != 0);

            FileEnumerator fe(prefix);

            if (path.back() != L'\\' && path.back() != L'/')
            {
                fe.EnumerateInternal(path + L'\\', fileAction, errorAction);
            }
            else
            {
                fe.EnumerateInternal(path, fileAction, errorAction);
            }
        }

    private:

        void EnumerateInternal(const std::wstring& path, std::function<void(const std::wstring& path, const wchar_t* name, int64_t mt, int64_t size)> fileAction, std::function<void(const std::wstring& path, const std::wstring& error)> errorAction)
        {
            buffer.resize(prefix.size(), 0);
            buffer.append(path); buffer.resize(buffer.size() + 1, L'*');

            HANDLE findHandle = ::FindFirstFileEx(buffer.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);

            if (findHandle == INVALID_HANDLE_VALUE)
            {
                errorAction(path, GetLastErrorW(::GetLastError())); return;
            }

            do
            {
                if (!(fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    {
                        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

                        if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0)
                        {
                            EnumerateInternal(path + fd.cFileName + L'\\', fileAction, errorAction);
                        }

                        continue;
                    }

                    const auto size = ((uint64_t)fd.nFileSizeHigh) << 32 | fd.nFileSizeLow;
                    
                    fileAction(path, fd.cFileName, FileTimeToDecimal(fd.ftLastWriteTime), size);
                }

            } while (::FindNextFile(findHandle, &fd));

            auto error = ::GetLastError();

            ::FindClose(findHandle);

            if (error != ERROR_NO_MORE_FILES)
            {
                errorAction(path, GetLastErrorW(error)); return;
            }
        }
    };


    class CFile
    {
        HANDLE fileHandle = INVALID_HANDLE_VALUE;

        DWORD lastError = 0;

    public:

        std::string GetLastErrorA()
        {
            return WStringToAString(GetLastErrorW());
        }

        std::wstring GetLastErrorW()
        {
            assert(lastError != 0);

            return MZ::GetLastErrorW(lastError);
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

                throw std::runtime_error{ GetLastErrorA() };
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

                throw std::runtime_error{ GetLastErrorA() };
            }
        }

        int64_t Seek(int64_t dist, DWORD dwMoveMethod)
        {
            LARGE_INTEGER li = { 0 }; li.QuadPart = dist;

            if (!::SetFilePointerEx(fileHandle, li, &li, dwMoveMethod))
            {
                lastError = ::GetLastError();

                throw std::runtime_error{ GetLastErrorA() };
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

        bool Open(const wchar_t* path, bool bWrite = true, bool NoBuffering = true, bool DeleteOnClose = false)
        {
            DWORD flags = (NoBuffering) ? FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN : 0;

            flags |= (DeleteOnClose) ? FILE_FLAG_DELETE_ON_CLOSE : 0;

            if (!bWrite)
                return OpenExist(path, GENERIC_READ, FILE_SHARE_READ, flags);
            else
                return OpenExist(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, flags);
        }

        bool Create(const wchar_t* path, bool NoBuffering = true, bool DeleteOnClose = false)
        {
            DWORD flags = (NoBuffering) ? FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN : 0;

            flags |= (DeleteOnClose) ? FILE_FLAG_DELETE_ON_CLOSE : 0;

            return Create(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, flags);
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
