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
#include <set>
#include <filesystem>
#include "StringUtility.h"
namespace LLUtils
{
    class FileSystemHelper
    {
      public:

        static ListWString FindFiles(ListWString& filesList, std::filesystem::path workingDir,
                                     native_string_type fileTypes, bool recursive, bool caseSensitive)
        {
            using namespace std::filesystem;
            ListNString extensions = StringUtility::split(fileTypes, LLUTILS_TEXT(';'));
            std::set<native_string_type> extensionSet;

            for (const auto& ext : extensions)
                extensionSet.insert(caseSensitive == true ? ext : StringUtility::ToUpper(ext));

            auto AddFileIfExtensionsMatches = [&](const path& filePath)
            {
                native_string_type extNoDot = filePath.extension().string<native_char_type>();

                if (extNoDot.empty() == false)
                    extNoDot = extNoDot.substr(1);

                if (extensionSet.find(caseSensitive == true ? extNoDot : StringUtility::ToUpper(extNoDot)) !=
                    extensionSet.end())
                    filesList.push_back(filePath.wstring());
            };

            if (recursive == true)
                for (const auto& p : recursive_directory_iterator(workingDir))
                    AddFileIfExtensionsMatches(p);
            else
                for (const auto& p : directory_iterator(workingDir))
                    AddFileIfExtensionsMatches(p);

            return filesList;
        }

        template <class string_type = native_string_type, typename char_type = typename string_type::value_type>
        static string_type ResolveFullPath(const string_type& fileName)
        {
            using namespace std::filesystem;
            path p(fileName);
            if (p.empty() == false && p.is_absolute() == false)
                p = current_path() / fileName;

            return p.string<char_type>();
        }

        template <class string_type>
        static bool EnsureDirectory(const string_type& path)
        {
            using namespace std;
            filesystem::path directoryName = path;
            directoryName.remove_filename();
            return filesystem::exists(directoryName) || filesystem::create_directories(directoryName);
        }
    };
}  // namespace LLUtils
