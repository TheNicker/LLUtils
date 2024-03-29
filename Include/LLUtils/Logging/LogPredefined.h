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
#include "LogTarget.h"
#include <LLUtils/FileHelper.h>
#include "../FileSystemHelper.h"
#include <iostream>
namespace LLUtils
{
	class LogConsole final : public ILog
	{
		void Log(std::wstring message) override
		{
			std::wcout << message;
		}
	};

	class LogDebug final : public ILog
	{
		void Log(std::wstring message) override
		{
			OutputDebugStringW(message.c_str());
		}
	};

	class LogFile final : public ILog
	{
	public:
		LogFile(std::wstring logPath, bool clear)
		{
			mLogPath = logPath;
			FileSystemHelper::EnsureDirectory(mLogPath);
			if (clear == true)
				std::filesystem::remove(logPath);
		}
		void Log(std::wstring message) override
		{
			if (mLogPath.empty() == false)
			{
				File::WriteAllText(mLogPath, message, true);
			}
		}

		const std::wstring& GetLogPath() const
		{
			return mLogPath;
		}


	private:
		std::wstring mLogPath;
	};
}