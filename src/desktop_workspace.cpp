#include "desktop_workspace.h"
#include <shellapi.h>
#include <stdexcept>
#include <vector>
namespace lenia {
StartupOptions startup_options() {
    int count = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv)
        throw std::runtime_error("Cannot read command line");
    struct FreeArguments {
        wchar_t **value;
        ~FreeArguments() {
            LocalFree(value);
        }
    } cleanup{argv};
    StartupOptions options;
    for (int i = 1; i < count; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--smoke")
            options.smoke = true;
        else if (arg == L"--workspace" && i + 1 < count && options.workspace.empty())
            options.workspace = argv[++i];
        else
            throw std::invalid_argument("Use --smoke and/or --workspace <directory>");
    }
    return options;
}
WorkspaceAccess::WorkspaceAccess(const std::filesystem::path &override_directory) {
    if (override_directory.empty()) {
        auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        if (!size)
            throw std::runtime_error("Local application data directory is unavailable");
        std::vector<wchar_t> text(size);
        auto copied = GetEnvironmentVariableW(L"LOCALAPPDATA", text.data(), size);
        if (!copied || copied >= size)
            throw std::runtime_error("Cannot resolve application data directory");
        directory = std::filesystem::path(text.data()) / L"Leniascope" / L"Laboratory";
    } else
        directory = std::filesystem::absolute(override_directory);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (!error)
        lock = CreateFileW((directory / L"workspace.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (!writable())
        message = "Workspace is read-only or open in another window. Automatic saving is disabled here.";
}
WorkspaceAccess::~WorkspaceAccess() {
    if (writable())
        CloseHandle(lock);
}
} // namespace lenia
