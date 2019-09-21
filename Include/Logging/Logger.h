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
#include <mutex>
#include <iostream>
#include "FileSystemHelper.h"
#include "FileHelper.h"
#include "PlatformUtility.h"
#include "Singleton.h"
#include "LogTarget.h"
#include "LogPredefined.h"

namespace LLUtils
{

	class Logger : public Singleton<Logger>
	{

	public:

		enum class TargetType
		{
			None,
			DefaultFile,
			Console,
			OutputDebug,
			Count,
		};

		void AddLogTarget(TargetType targetType, bool clear)
		{
			size_t index = static_cast<size_t>(targetType);
			LogSharedPtr predefineLog;

			switch (targetType)
			{
			case TargetType::DefaultFile:
			{
				using namespace LLUtils;
				std::filesystem::path p = PlatformUtility::GetExePath();
				auto logFilePath = p.replace_extension("log");
				predefineLog = std::make_shared<LogFile>(logFilePath.wstring(), clear);
			}
			break;
			case TargetType::Console:
				predefineLog = std::make_shared<LogConsole>();
				break;
			case TargetType::OutputDebug:
				predefineLog = std::make_shared<LogDebug>();
				break;

			}

			if (predefineLog != nullptr)
			{
				listPredefinedLogs.at(index) = predefineLog;
				logTargets.push_back(predefineLog.get());
			}


		}
		void AddLogTarget(ILog* log)
		{
			logTargets.push_back(log);
		}

		void Log(std::string message)
		{
			Log(StringUtility::ToWString(message));
		}

		void Log(std::wstring message)
		{
			std::wstring messageWithNewLine = message + L"\n";
			for (auto& target : logTargets)
				target->Log(messageWithNewLine);
		}
	private:
		using ListLogTargets = std::vector<std::shared_ptr<ILog>>;
		ListLogTargets listPredefinedLogs = ListLogTargets(static_cast<int>(TargetType::Count));
		std::vector<ILog*> logTargets;
	};



}
