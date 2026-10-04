#include <LLUtils/Logging/Logger.h>
LLUtils::LogCategory OtherLoggingCategory()
{
    return LLUtils::Logger::RegisterCategory("Test.Logging");
}
void LogFromOtherTranslationUnit()
{
    LL_LOG(OtherLoggingCategory(), LLUtils::LogLevel::Info, "other translation unit");
}
