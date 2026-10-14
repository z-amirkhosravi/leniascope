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
        else if (arg == L"--gallery-smoke") {
            options.gallery_smoke = options.smoke = true;
        } else if (arg == L"--editor-smoke") {
            options.editor_smoke = options.smoke = true;
        } else if (arg == L"--profile") {
            options.profile = true;
            options.smoke = true;
        } else if (arg == L"--cpu-transfer")
            options.cpu_transfer = true;
        else if (arg == L"--cuda")
            options.cuda = true;
        else if (arg == L"--workspace" && i + 1 < count && options.workspace.empty())
            options.workspace = argv[++i];
        else
            throw std::invalid_argument(
                "Use --smoke, --profile, --cuda, --cpu-transfer, and/or --workspace <directory>");
    }
    if ((options.gallery_smoke || options.editor_smoke) &&
        (options.workspace.empty() || options.profile || (options.gallery_smoke && options.editor_smoke)))
        throw std::invalid_argument(
            "Gallery/editor smoke requires an isolated --workspace; smoke modes cannot be combined");
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
    retry();
}
bool WorkspaceAccess::retry() {
    if (writable())
        return true;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (!error)
        lock = CreateFileW((directory / L"workspace.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    const auto failure = error ? DWORD(error.value()) : GetLastError();
    if (writable())
        message.clear();
    else if (failure == ERROR_SHARING_VIOLATION || failure == ERROR_LOCK_VIOLATION)
        message = "Another Leniascope window is using this workspace. Close that window, then retry access.";
    else
        message = "Cannot save to this workspace (error " + std::to_string(failure) +
                  "). Check folder access, then retry.";
    return writable();
}
WorkspaceAccess::~WorkspaceAccess() {
    if (writable())
        CloseHandle(lock);
}
} // namespace lenia
