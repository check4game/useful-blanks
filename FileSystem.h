#pragma once

#include <windows.h>

#include <string>
#include <stdexcept>
#include <functional>

#include "Assert.h"

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

        ::FormatMessageW(
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

    std::string GetLastErrorA(DWORD error)
    {
        LPVOID lpMsgBuf;

        ::FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&lpMsgBuf,
            0,
            NULL
        );

        std::string errorMessage(static_cast<char*>(lpMsgBuf));

        LocalFree(lpMsgBuf);

        return std::to_string(error) + ", " + errorMessage;
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

    class PathHelper
    {
        std::wstring buffer, prefix;

    public:

        PathHelper()
        {
            buffer.reserve(0xFFFF);
        }

        PathHelper(const std::wstring& prefix)
        {
            buffer.reserve(0xFFFF); SetPrefix(prefix);
        }

        void SetPrefix(const std::wstring& prefix)
        {
            this->prefix = prefix; buffer.resize(0, 0); buffer.append(prefix);
        }

        const wchar_t* c_str(const std::wstring& path)
        {
            buffer.resize(prefix.size(), 0); buffer.append(path); return buffer.c_str();
        }

        const wchar_t* c_str(const std::wstring& path, wchar_t ch)
        {
            buffer.resize(prefix.size(), 0); buffer.append(path); buffer.push_back(ch); return buffer.c_str();
        }

        const wchar_t* c_str(const std::wstring& path, const wchar_t* name)
        {
            buffer.resize(prefix.size(), 0); buffer.append(path); buffer.append(name); return buffer.c_str();
        }

        const wchar_t* get_prefix()
        {
            return prefix.c_str();
        }
    };

    class FileEnumerator
    {
    private:

        WIN32_FIND_DATA fd {};

        PathHelper bp;

        FileEnumerator(const std::wstring& prefix) 
        {
            bp.SetPrefix(prefix);
        }

    public:


        static void Enumerate(const std::wstring& prefix, const std::wstring path, std::function<void(const std::wstring& path, const wchar_t* name, int64_t mt, int64_t raw_data_size)> fileAction, std::function<void(const std::wstring& path, const std::wstring& error)> errorAction)
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

        void EnumerateInternal(const std::wstring& path, std::function<void(const std::wstring& path, const wchar_t* name, int64_t mt, int64_t raw_data_size)> fileAction, std::function<void(const std::wstring& path, const std::wstring& error)> errorAction)
        {
            HANDLE findHandle = ::FindFirstFileEx(bp.c_str(path, L'*'), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);

            if (findHandle == INVALID_HANDLE_VALUE)
            {
                const auto lastError = ::GetLastError();

                errorAction(path, GetLastErrorW(lastError)); return;
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

            const auto lastError = ::GetLastError();

            ::FindClose(findHandle);

            if (lastError != ERROR_NO_MORE_FILES)
            {
                errorAction(path, GetLastErrorW(lastError)); return;
            }
        }
    };


    class File
    {
        HANDLE fileHandle = INVALID_HANDLE_VALUE;

        DWORD lastError = ERROR_SUCCESS;

        OVERLAPPED overlapped = { 0 };

    public:

        std::wstring GetLastErrorW()
        {
            assert(IsError());

            return MZ::GetLastErrorW(lastError);
        }

        std::string GetLastErrorA()
        {
            assert(IsError());

            return MZ::GetLastErrorA(lastError);
        }

        bool IsOpen() const
        {
            return INVALID_HANDLE_VALUE != fileHandle;
        }

        bool IsError() const
        {
            return lastError != ERROR_SUCCESS;
        }

        bool IsSharingViolation() const
        {
            return lastError == ERROR_SHARING_VIOLATION;
        }

    private:

        bool OpenExist(const wchar_t* path, const DWORD dwDesiredAccess, const DWORD dwShareMode, const DWORD flags)
        {
            assert(IsOpen() != true);

            fileHandle = CreateFile(path, dwDesiredAccess, dwShareMode,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | flags, nullptr);

            if (!IsOpen())
            {
                lastError = ::GetLastError(); return false;
            }

            return true;
        }

        bool Create(const wchar_t* path, const DWORD dwDesiredAccess, const DWORD dwShareMode, const DWORD flags)
        {
            assert(IsOpen() != true);

            fileHandle = CreateFile(path, dwDesiredAccess, dwShareMode,
                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | flags, nullptr);

            if (!IsOpen())
            {
                lastError = ::GetLastError(); return false;
            }

            return true;
        }

        DWORD ReadInternal(byte* buffer, DWORD nNumberOfBytesToRead)
        {
            assert(IsOpen() == true);

            DWORD NumberOfBytesRead = 0;

            if (!::ReadFile(fileHandle, buffer, nNumberOfBytesToRead, &NumberOfBytesRead, nullptr))
            {
                lastError = ::GetLastError(); assert(lastError != ERROR_INVALID_PARAMETER);
            }

            return NumberOfBytesRead;
        }

        bool WriteInternal(const byte* buffer, DWORD nNumberOfBytesToWrite)
        {
            assert(IsOpen() == true);

            DWORD NumberOfBytesWritten = 0;

            if (!::WriteFile(fileHandle, buffer, nNumberOfBytesToWrite, &NumberOfBytesWritten, nullptr))
            {
                lastError = ::GetLastError(); assert(lastError != ERROR_INVALID_PARAMETER);
                
                return false;
            }

            return true;
        }

        int64_t Seek(int64_t dist, DWORD dwMoveMethod)
        {
            assert(IsOpen() == true);

            LARGE_INTEGER li = { 0 }; li.QuadPart = dist;

            if (!::SetFilePointerEx(fileHandle, li, &li, dwMoveMethod))
            {
                lastError = ::GetLastError(); 

                assert(lastError == 0, "%s", GetLastErrorA().c_str());
            }

            return li.QuadPart;
        }

    public:

        size_t Size()
        {
            assert(IsOpen() == true);

            LARGE_INTEGER size = { 0 };

            if (!::GetFileSizeEx(fileHandle, &size))
            {
                lastError = ::GetLastError();

                assert(false, "%s\n", GetLastErrorA().c_str());
            }

            return size.QuadPart;
        }

        ~File()
        {
            Close();
        }

        void Close()
        {
            if (IsOpen())
            {
                if (lastError == ERROR_IO_PENDING)
                {
                    CancelIo(fileHandle);
                }

                ::CloseHandle(fileHandle);
            }

            overlapped = { 0 }; fileHandle = INVALID_HANDLE_VALUE; lastError = ERROR_SUCCESS;
        }

        bool OpenReadOverlapped(const wchar_t* path, bool FileShareWrite = false, bool NoBuffering = true)
        {
            DWORD flags = (NoBuffering) ? FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN : FILE_FLAG_SEQUENTIAL_SCAN;

            return OpenExist(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, flags | FILE_FLAG_OVERLAPPED);
        }

        bool OpenRead(const wchar_t* path, bool FileShareWrite = false, bool NoBuffering = true)
        {
            DWORD flags = (NoBuffering) ? FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN : FILE_FLAG_SEQUENTIAL_SCAN;

            return OpenExist(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, flags);
        }

        bool Open(const wchar_t* path, bool bWrite = true, bool NoBuffering = true, bool DeleteOnClose = false)
        {
            DWORD flags = (NoBuffering) ? FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN : FILE_FLAG_SEQUENTIAL_SCAN;

            flags |= (DeleteOnClose) ? FILE_FLAG_DELETE_ON_CLOSE : 0;

            if (!bWrite)
                return OpenExist(path, GENERIC_READ, FILE_SHARE_READ, flags);
            else
                return OpenExist(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, flags);
        }

        bool Create(const wchar_t* path, bool NoBuffering = true, bool DeleteOnClose = false)
        {
            DWORD flags = (NoBuffering) ? FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN : FILE_FLAG_SEQUENTIAL_SCAN;

            flags |= (DeleteOnClose) ? FILE_FLAG_DELETE_ON_CLOSE : 0;

            return Create(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, flags);
        }

        static constexpr uint32_t defaultBlockSize = 128u * 1024;

        template <typename T>
        void Write(const std::vector<T>& data, uint32_t blockSize = defaultBlockSize)
        {
            Write((uint8_t*)data.data(), data.size() * sizeof(T), blockSize);
        }

        void Write(const byte* buffer, size_t nNumberOfBytesToWrite, uint32_t blockSize = defaultBlockSize)
        {
            for (const byte* begin = buffer, *end = (buffer + nNumberOfBytesToWrite); begin < end; begin += blockSize)
            {
                if ((end - begin) <= (int64_t)blockSize)
                {
                    WriteInternal(begin, static_cast<DWORD>(end - begin)); break;
                }

                WriteInternal(begin, static_cast<DWORD>(blockSize));
            }
        }

        template <typename T>
        DWORD Read(const std::vector<T>& data, uint32_t blockSize = defaultBlockSize)
        {
            const auto length = Read((uint8_t*)data.data(), data.size() * sizeof(T), blockSize);

            assert((length % sizeof(T)) == 0);

            return length / sizeof(T);
        }

        template <typename T>
        DWORD Read(const uint32_t index, const std::vector<T>& data, uint32_t blockSize = defaultBlockSize)
        {
            assert(SeekBegin(index * sizeof(T)) == index * sizeof(T));

            return Read(data, blockSize);
        }

        DWORD Read(byte* buffer, size_t nNumberOfBytesToRead, uint32_t blockSize = defaultBlockSize)
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
            assert(dist >= 0);

            return Seek(dist, FILE_BEGIN);
        }

        int64_t SeekCurrent(int64_t dist)
        {
            return Seek(dist, FILE_CURRENT);
        }

        template <typename T>
        int64_t SeekBack(const std::vector<T>& data)
        {
            return SeekBack(data.size() * sizeof(T));
        }

        int64_t SeekBack(int64_t dist)
        {
            assert(dist >= 0);

            return SeekCurrent(-dist);
        }

        int64_t SeekEnd(int64_t dist)
        {
            return Seek(dist, FILE_END);
        }
        
        void OverlappedPosition(int64_t offset)
        {
            assert(offset >= 0 && (offset % 4096) == 0);

            if (lastError == ERROR_HANDLE_EOF) lastError = ERROR_SUCCESS;

            assert(lastError == ERROR_SUCCESS);

            OverlappedPositionInternal(offset);
        }

        int64_t OverlappedPosition() const
        {
            return (((uint64_t)overlapped.OffsetHigh << 32) | overlapped.Offset);
        }

        int64_t Position()
        {
            return SeekCurrent(0);
        }

    private:

        void OverlappedPositionInternal(int64_t offset)
        {
            overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
            overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
        }

        std::vector<uint8_t> internal_buffer;

    public:
      
        DWORD ReadOverlapped(std::vector<uint8_t>& buffer)
        {
            assert(IsOpen() == true);

            if (lastError == ERROR_HANDLE_EOF) return 0;

            assert(lastError == ERROR_SUCCESS || lastError == ERROR_IO_PENDING);

            DWORD NumberOfBytesRead = 0;

            auto offset = OverlappedPosition();

            if (lastError == ERROR_SUCCESS)
            {
                internal_buffer.resize(buffer.size());

                if (!::ReadFile(fileHandle, internal_buffer.data(), static_cast<DWORD>(buffer.size()), &NumberOfBytesRead, &overlapped))
                {
                    lastError = ::GetLastError();

                    if (lastError == ERROR_HANDLE_EOF) return 0;
                    
                    if (lastError != ERROR_IO_PENDING)
                    {
                        assert(false, "%s\n", GetLastErrorA().c_str());
                    }
                }
                else
                {
                    internal_buffer.swap(buffer);

                    OverlappedPositionInternal(offset + NumberOfBytesRead);

                    return NumberOfBytesRead;
                }
            }

            lastError = ERROR_SUCCESS;

            if (!::GetOverlappedResult(fileHandle, &overlapped, &NumberOfBytesRead, TRUE))
            {
                lastError = ::GetLastError();

                if (lastError == ERROR_HANDLE_EOF) return 0;

                assert(false, "%s\n", GetLastErrorA().c_str());
            }

            internal_buffer.swap(buffer);

            if (internal_buffer.size() == NumberOfBytesRead)
            {
                OverlappedPositionInternal(offset + NumberOfBytesRead);

                internal_buffer.resize(NumberOfBytesRead);

                if (!::ReadFile(fileHandle, internal_buffer.data(), static_cast<DWORD>(internal_buffer.size()), nullptr, &overlapped))
                {
                    lastError = ::GetLastError();

                    if (lastError != ERROR_IO_PENDING && lastError != ERROR_HANDLE_EOF)
                    {
                        assert(false, "%s\n", GetLastErrorA().c_str());
                    }
                }
            }

            return NumberOfBytesRead;
        }
    };
}
