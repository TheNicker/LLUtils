#pragma once
#include "../Emergency.h"
#include "../NativeFile.h"
#include <filesystem>

namespace LLUtils::LoggingDetail
{
    // Process-lifetime private emergency descriptor. A faulting thread and a late static destructor can both
    // report, so the descriptor is opened once and never closed; that keeps reporting allocation-free and
    // independent of session lifetime. This half stays in Logging/ because it needs <filesystem>, and
    // Emergency.h must not: Exception.h includes that header and would otherwise inherit it. The reporting
    // side it adopts a descriptor for is neutral and lives in ../Emergency.h.
    class EmergencyFile
    {
      public:

        static void Open(const std::filesystem::path& path)
        {
            // At most one descriptor is ever adopted, and creation always prevents child inheritance.
            if (path.empty() || EmergencyDetail::Emergency::HasDescriptor())
                return;
            auto file = NativeFile::Open(path, NativeFile::Access::Append, NativeFile::Disposition::OpenOrCreate);
            EmergencyDetail::Emergency::AdoptDescriptor(file.Release());
        }
    };
}  // namespace LLUtils::LoggingDetail