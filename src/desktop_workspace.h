#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>
namespace lenia {
struct StartupOptions {
    bool smoke = false, profile = false, cpu_transfer = false, cuda = false, gallery_smoke = false,
         editor_smoke = false;
    std::filesystem::path workspace;
};
StartupOptions startup_options();
class WorkspaceAccess {
  public:
    explicit WorkspaceAccess(const std::filesystem::path &override_directory);
    ~WorkspaceAccess();
    WorkspaceAccess(const WorkspaceAccess &) = delete;
    WorkspaceAccess &operator=(const WorkspaceAccess &) = delete;
    std::filesystem::path directory;
    std::string message;
    bool retry();
    bool writable() const {
        return lock != INVALID_HANDLE_VALUE;
    }

  private:
    HANDLE lock = INVALID_HANDLE_VALUE;
};
} // namespace lenia
