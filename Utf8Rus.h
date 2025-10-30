#pragma once

#include <string>
#include <cstdint>
#include <array>

#include "Assert.h"

namespace MZ
{
	using ToCP1251TableType = std::array<char, 0x10000>;
	using ToUnicodeTableType = std::array<wchar_t, 0x100>;

	constexpr auto make_unicode_to_cp1251 = []()
		{
			static_assert(sizeof(wchar_t) == 2 && (wchar_t)(-1) == 0xFFFF);

			ToCP1251TableType table = { 0 };

			// Кириллические заглавные буквы (А-Я) 0x0410-0x042F -> 0xC0-0xDF
			for (wchar_t unicode = 0x0410, cp1251 = 0xC0; unicode <= 0x042F; ++unicode, ++cp1251)
			{
				table[unicode] = static_cast<char>(cp1251);
			}

			// Кириллические строчные буквы (а-я) 0x0430-0x044F -> 0xE0-0xFF
			for (wchar_t unicode = 0x0430, cp1251 = 0xE0; unicode <= 0x044F; ++unicode, ++cp1251)
			{
				table[unicode] = static_cast<char>(cp1251);
			}

			// Добавляем Ё и ё, ЕЁ это двухбайтовая последовательсность в UTF8!
			table[0x0401] = static_cast<char>(0xA8);  // Ё
			table[0x0451] = static_cast<char>(0xB8);  // ё

			return table;
		};

	constexpr auto make_cp1251_to_unicode = []()
		{
			static_assert(sizeof(wchar_t) == 2 && (wchar_t)(-1) == 0xFFFF);

			ToUnicodeTableType table = { 0 };

			// Кириллические заглавные буквы (А-Я) 0x0410-0x042F -> 0xC0-0xDF
			for (wchar_t unicode = 0x0410, cp1251 = 0xC0; unicode <= 0x042F; ++unicode, ++cp1251)
			{
				table[cp1251] = unicode;
			}

			// Кириллические строчные буквы (а-я) 0x0430-0x044F -> 0xE0-0xFF
			for (wchar_t unicode = 0x0430, cp1251 = 0xE0; unicode <= 0x044F; ++unicode, ++cp1251)
			{
				table[cp1251] = unicode;
			}

			// Добавляем Ё и ё, ЕЁ это двухбайтовая последовательсность в UTF8!
			table[0xA8] = static_cast<wchar_t>(0x0401);  // Ё
			table[0xB8] = static_cast<wchar_t>(0x0451);  // ё

			return table;
		};

	class Utf8Rus
	{
	private:

		static size_t SizeA(const wchar_t* wStr, const ToCP1251TableType& table)
		{
			mz_assert(wStr != nullptr);

			size_t size = 0;

			for (/*wStr*/; *wStr; wStr++)
			{
				if (*wStr < 0x80)
				{
					size++;
				}
				else if (*wStr < 0x800) // русские символы все < 0x800
				{
					size++; size += (0 == table[*wStr]);
				}
				else
				{
					size += 3;
				}
			}

			return size;
		}

		static std::string StringA(const wchar_t* wStr, const ToCP1251TableType& table)
		{
			std::string result(SizeA(wStr, table), '\0');

			auto aStr = result.begin();

			for (/*wStr*/; *wStr; wStr++)
			{
				if (*wStr < 0x80)
				{
					*aStr++ = static_cast<char>(*wStr);
				}
				else if (*wStr < 0x800)
				{
					if (0 == (*aStr = table[*wStr]))
					{
						*aStr++ = static_cast<char>(0x90 | ((*wStr >> 0) & 0x0F));
						*aStr++ = static_cast<char>(0x80 | ((*wStr >> 4) & 0x7F));
					}
					else aStr++;
				}
				else
				{
					*aStr++ = static_cast<char>(0x80 | ((*wStr) & 0x0F));
					*aStr++ = static_cast<char>(0x80 | ((*wStr >> 4) & 0x3F));
					*aStr++ = static_cast<char>(0x80 | ((*wStr >> 10) & 0x3F));
				}
			}

			return result;
		}

		static std::string StringA(const std::wstring& wStr, const ToCP1251TableType& table)
		{
			return StringA(wStr.c_str(), table);
		}

		static size_t SizeW(const char* aStr, const ToUnicodeTableType& table)
		{
			size_t size = 0; mz_assert(aStr != nullptr);

			auto bStr = reinterpret_cast<const uint8_t*>(aStr);

			for (/*bStr*/; *bStr; bStr++)
			{
				size++;

				if (*bStr >= 0x80)
				{
					if (0 == table[*bStr])
					{
						if (0x90 == (*bStr & 0x90)) // последовательность два байта
						{
							mz_assert(0 != (bStr[1] & 0x80)); aStr++;
						}
						else if (*bStr & 0x80) // последовательность три байта
						{
							mz_assert(0 != (bStr[1] & 0x80) && 0 != (bStr[2] & 0x80)); bStr += 2;
						}
						else
						{
							mz_assert(false, "Utf8Rus? %u", *bStr);
						}
					}
				}
			}

			return size;
		}

		static std::wstring StringW(const char* str, const ToUnicodeTableType& table)
		{
			std::wstring result(SizeW(str, table), L'\0'); // через SizeW мы проверили валидность строки

			auto r = result.begin();

			auto aStr = reinterpret_cast<const uint8_t*>(str);

			for (/*aStr*/; *aStr; aStr++)
			{
				if (*aStr >= 0x80)
				{
					if (0x00 == (*r = table[*aStr]))
					{
						if (0x90 == (*aStr & 0x90)) // последовательность два байта
						{
							*r++ = (static_cast<wchar_t>(aStr[1] & 0x7F) << 4) | (*aStr & 0x0F); aStr++;
						}
						else if (*aStr & 0x80) // последовательность три байта
						{
							*r++ = (static_cast<wchar_t>(aStr[2] & 0x3F) << 10)
								| (static_cast<wchar_t>(aStr[1] & 0x3F) << 4)
								| (*aStr & 0x0F); aStr += 2;
						}
					}
					else r++;
				}
				else
				{
					*r++ = *aStr;
				}
			}

			return result;
		}

		static std::wstring StringW(const std::string& aStr, const ToUnicodeTableType& table)
		{
			return StringW(aStr.c_str(), table);
		}

		static constexpr auto unicode_to_cp1251 = make_unicode_to_cp1251();

		static constexpr auto cp1251_to_unicode = make_cp1251_to_unicode();

public:
		static std::string Encode(const wchar_t* wStr)
		{
			return StringA(wStr, unicode_to_cp1251);
		}

		static std::string Encode(const std::wstring& wStr)
		{
			return Encode(wStr.c_str());
		}

		static std::wstring Decode(const char* aStr)
		{
			return StringW(aStr, cp1251_to_unicode);
		}

		static std::wstring Decode(const std::string& aStr)
		{
			return Decode(aStr.c_str());
		}

		static size_t DecodedSize(const char* aStr)
		{
			return SizeW(aStr, cp1251_to_unicode);
		}

		static size_t EncodedSize(const wchar_t* wStr)
		{
			return SizeA(wStr, unicode_to_cp1251);
		}

		template<typename T>
		static size_t Size(const T* str)
		{
			static_assert(
				std::is_same_v<T, char> || std::is_same_v<T, wchar_t>,
				"Template parameter must be either char or wchar_t"
				);

			mz_assert(str != nullptr);

			size_t size = 0;

			while (*str) size++;
		}
	};
}
