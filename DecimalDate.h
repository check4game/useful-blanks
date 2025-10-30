#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace MZ
{
	using DecimalDateValue = int64_t; // YYYYMMDDHHMMSS

	class DecimalDate
	{
	private:

		DecimalDateValue value; 

		static const DecimalDateValue MIN_DATE_VALUE = 1970'01'01'00'00'00;

		constexpr static const char* empty = "                   ";
		constexpr static const char* zero  = "0000-00-00 00:00:00";

		static DecimalDateValue ToDate(const std::tm& t)
		{
			DecimalDateValue date = (t.tm_year + 1900LL) * 10000000000LL + (t.tm_mon + 1LL) * 100000000LL + t.tm_mday * 1000000LL + t.tm_hour * 10000LL + t.tm_min * 100LL + t.tm_sec;

			return (date <= MIN_DATE_VALUE) ? MIN_DATE_VALUE : date;
		}

	public:

		DecimalDate()
		{
			value = Now();
		}

		DecimalDate(DecimalDateValue initial)
		{
			value = Now();

			if (value <= initial)
			{
				value = ToDate(ToTime(initial) + 1);
			}
		}

		DecimalDateValue GetValue() const
		{
			return value;
		}

		static DecimalDateValue Now()
		{
			std::time_t now = std::time(nullptr);

			return ToDate(now);
		}

		static DecimalDateValue ToDate(const std::time_t time)
		{
			std::tm tm; gmtime_s(&tm, &time); return ToDate(tm);
		}

		static DecimalDateValue ToDate(const FILETIME& fileTime)
		{
			SYSTEMTIME st;

			DecimalDateValue date = 0;

			if (FileTimeToSystemTime(&fileTime, &st))
			{
				date = st.wYear * 10000000000LL + st.wMonth * 100000000LL + st.wDay * 1000000LL + st.wHour * 10000LL + st.wMinute * 100LL + st.wSecond;
			}

			return (date <= MIN_DATE_VALUE) ? MIN_DATE_VALUE : date;
		}

		static std::time_t ToTime(const DecimalDateValue date)
		{
			if (date <= MIN_DATE_VALUE) return MIN_DATE_VALUE;

			static const int days[12] = { 0,31,59,90,120,151,181,212,243,273,304,334 };

			const int year = date / 10000000000LL % 10000;
			const int month = (date / 100000000 % 100 - 1) % 12;
			const int day = date / 1000000 % 100;
			const int hour = date / 10000 % 100;
			const int min = date / 100 % 100;
			const int sec = date % 100;

			return (day - 1LL + days[month] + (year % 4 == 0 && month > 1) + ((year - 1970LL) * 1461 + 1) / 4) * 86400 + hour * 3600LL + min * 60LL + sec;
		}

		const std::string ToString()
		{
			return ToString(value, false);
		}

		template<int digits, typename T>
		static void ToString(T value, char* ptr)
		{
			ptr += digits - 1;

			for (int i = 0; i < digits; i++)
			{
				*ptr-- = '0' + value % 10; value /= 10;
			}
		}

		template<int digits, typename R=std::string, typename T>
		static R ToString(T value)
		{
			char buffer[digits + 1]{};

			ToString<digits>(value, buffer);

			if constexpr (std::is_same_v<R, std::string>)
			{
				return R(buffer);
			}
			else
			{
				R ws(digits, L'\0');

				for (auto i = 0; i < digits; i++) ws[i] = static_cast<wchar_t>(buffer[i]);

				return ws;
			}
		}

		// Convert to "YYYY-MM-DD HH:MM:SS"
		static std::string ToString(DecimalDateValue date, bool bToLocal) 
		{
			if (date < MIN_DATE_VALUE) return empty;

			std::string str(zero);
			
			static const int t[] = { 18,17,15,14,12,11,9,8,6,5,3,2,1,0 };

			if (bToLocal)
			{
				const auto time = ToTime(date);

				std::tm tm; localtime_s(&tm, &time);

				date = ToDate(tm);
			}

			for (int i = 0; i < 14; ++i)
			{
				str[t[i]] += char(date % 10), date /= 10;
			}

			return str;
		}
	};
}
