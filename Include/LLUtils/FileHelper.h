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
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "Buffer.h"

namespace LLUtils
{
    class File
    {
    public:
        static std::string ReadAllText(std::wstring filePath)
        {
            using namespace std;
            ifstream t(filePath);
            if (t.is_open())
            {
                stringstream buffer;
                buffer << t.rdbuf();
                return buffer.str();
            }
            else
                return std::string();
        }

		template <class string_type, typename char_type = typename string_type::value_type >
		static void WriteAllText(const std::wstring& filePath, const string_type& text, bool append = false)
		{
			using namespace std;
			basic_ofstream<char_type, char_traits<char_type>> file(filePath, append ? std::ios_base::app : std::ios_base::out);
			file << text;
		}

        static LLUtils::Buffer ReadAllBytes(std::wstring filePath)
        {
            using namespace std;
			uintmax_t fileSize = filesystem::file_size(filePath);
            LLUtils::Buffer buf(static_cast<size_t>(fileSize));
            ifstream t(filePath, std::ios::binary);
            t.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(fileSize));
            return buf;
        }


        static void WriteAllBytes(const std::wstring& filePath, const std::size_t size, const std::byte* const buffer)
        {
            using namespace std;
            filesystem::path parent = filesystem::path(filePath).parent_path();
            if (filesystem::exists(parent) == false)
                filesystem::create_directory(parent);

            ofstream file(filePath, std::ios::binary);
            file.write(reinterpret_cast<const char*>(buffer),static_cast<std::streamsize>(size));
        }
    };
}
