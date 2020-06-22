/*
Copyright (c) 2019 Lior Lahav

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#pragma warning(push)
#pragma warning(disable:4244) // when calling std::transform with ::tolower;

#include "Platform.h"
#include <string>
#include <cstring>
#include <vector>
#include <sstream>
#include <algorithm>
#include <locale>
#include <cwchar>
#include "StringDefs.h"

namespace LLUtils
{
    class StringUtility
    {
    public:

        template <typename char_type, typename string_type = std::basic_string<char_type>>
        static inline string_type& rtrim(string_type& s, const char_type* t)
        {
            s.erase(s.find_last_not_of(t) + 1);
            return s;
        }

        // trim from beginning of string (left)
        template <typename char_type, typename string_type = std::basic_string<char_type>>
        static inline string_type& ltrim(string_type& s, const char_type* t)
        {
            s.erase(0, s.find_first_not_of(t));
            return s;
        }

        // trim from both ends of string (left & right)
        template <typename char_type, typename string_type = std::basic_string<char_type>>
        static inline string_type& trim(string_type& s, const char_type* t)
        {
            return ltrim(rtrim(s, t), t);
        }
	
        template<typename chartype, typename string_type = std::basic_string<chartype>>
        static ListString<string_type> split(const string_type &s, chartype delim)
        {
            using namespace std;
            ListString<string_type> elems;
            using value_type = typename string_type::value_type;
            basic_stringstream<value_type> ss;
            ss.str(s);
            string_type item;
            while (getline<value_type>(ss, item, delim))
                if (item.empty() == false)
                    elems.push_back(item);

            return elems;
        }


#pragma region String conversions routines  

	public:
		template <typename DST, typename SRC, std::enable_if_t<!std::is_same_v<SRC, DST>, int> = 0>
		static DST ConvertString(const SRC & sourceString)
		{
			return ConvertStringImp(sourceString);
		}

		// Identity conversion.
		template <typename DST, typename SRC, std::enable_if_t<std::is_same_v<SRC, DST>, int> = 0>
		static DST ConvertString(const SRC & sourceString)
		{
			return sourceString;
		}


		

		static std::string ToAString(const std::string& str)
		{
			return str;
		}

		static inline std::wstring ToWString(const std::string& str)
		{
			return ConvertStringImp(str);
		}

		static std::string ToAString(const std::wstring str)
		{
			return ConvertStringImp(str);
		}

		static std::string ToAString(const wchar_t* str)
		{
			return ConvertStringImp(str);
		}
		static const char* ToAString(const char* str)
		{
			return str;
		}

		template <typename Source>
		static inline native_string_type ToNativeString(const Source& str)
		{
			return ConvertString<native_string_type>(str);
		}

		template <typename Source>
		static inline default_string_type ToDefaultString(const Source& str)
		{
			return ConvertString< default_string_type>(str);
		}


	


		
	
#pragma endregion String conversions routines
  
        template <class string_type>
        static string_type GetFileExtension(const string_type& str)
        {
            using namespace std;
            string_type extension;
            
            
            typename string_type::size_type pos = str.find_last_of(46);
            if (pos != string_type::npos)
                extension = str.substr(pos + 1, str.length() - pos - 1);

            return extension;
        }

        template <class string_type>
        static string_type ToLower(const string_type& str)
        {
            using namespace std;
            string_type localStr = str;

            std::transform(localStr.begin(), localStr.end(), localStr.begin(), ::tolower);

            return localStr;
        }

        template <class string_type>
        static string_type ToUpper(const string_type& str)
        {
            using namespace std;
            string_type localStr = str;
            std::transform(localStr.begin(), localStr.end(), localStr.begin(), ::toupper);
            return localStr;
        }

		private:
			static std::wstring ConvertStringImp(const char* sourceString)
			{
				const std::size_t size = (std::strlen(sourceString) + 1) * 2;
				auto buf = std::make_unique<wchar_t[]>(size);
				swprintf_s(buf.get(), size, L"%S", sourceString);
				return std::wstring(buf.get());
			}


			static std::wstring ConvertStringImp(const std::string & sourceString)
			{
				return ConvertStringImp(sourceString.c_str());
			}

			static std::string ConvertStringImp(const wchar_t* sourceString)
			{
				std::size_t size = wcslen(sourceString) + 1;
				auto pBuff = std::make_unique<char[]>(size);
				std::size_t converted;
				wcstombs_s(&converted, pBuff.get(), size, sourceString, size * 2);
				return std::string(pBuff.get());
			}

			static std::string ConvertStringImp(const std::wstring & sourceString)
			{
				return ConvertStringImp(sourceString.c_str());
			}

    };
}

#pragma warning(pop)