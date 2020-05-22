#pragma once
#include "StringUtility.h"
#include "PlatformUtility.h"
#include "FileHelper.h"


namespace LLUtils
{
    class IniFile
    {

    public:
        IniFile(const std::wstring& fileName)
        {
            using namespace LLUtils;
            using namespace std::string_literals;
        	if (std::filesystem::exists(fileName) == false || std::filesystem::is_regular_file(fileName) == false)
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::NotFound, "The file: '"s + StringUtility::ToAString(fileName) + "' has not been found or it's not a regular file"s);

            std::string fileContents = File::ReadAllText(fileName);
            ListAString lines = StringUtility::split(fileContents, '\n');

            for (const std::string& pair : lines)
            {
                if (pair.length() > 0 && pair[0] == ';')
                    continue;
                ListAString pairSplitted = StringUtility::split(pair, '=');
                if (pairSplitted.size() != 2)
                    continue;
                std::string key = pairSplitted[0];
                StringUtility::trim(key, "\t\n\r ");
                key = StringUtility::ToLower(key);
                std::string value = pairSplitted[1];
                StringUtility::trim(value, "\t\n\r ");
                mSettings[key] = value;
            }
        }

        const std::string& GetEntry(const std::string& key, bool throwifNotFound = true)
        {
            using namespace LLUtils;
            std::string lowerKey = StringUtility::ToLower(key);
            auto it = mSettings.find(lowerKey);
            if (it != mSettings.end())
                return it->second;
            else
            {
                if (throwifNotFound == true)
                    LL_EXCEPTION(LLUtils::Exception::ErrorCode::InvalidState, "entry must exist");

                static std::string empty;
                return empty;
            }
        }

    private:
        std::map<std::string, std::string> mSettings;
    };
}