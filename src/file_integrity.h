#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace lenia {
// FNV-1a detects accidental corruption; it is not cryptographic authentication.
inline uint64_t file_checksum(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Checkpoint or preview is missing or unreadable");
    uint64_t hash = 14695981039346656037ULL;
    // Large reads avoid thousands of round trips on WSL/network-backed workspaces.
    // Keep this buffer on the heap, outside Windows' limited thread stack.
    std::vector<char> buffer(4 * 1024 * 1024);
    while (file.read(buffer.data(), std::streamsize(buffer.size())) || file.gcount()) {
        const auto count = file.gcount();
        for (std::streamsize i = 0; i < count; ++i)
            hash = (hash ^ static_cast<unsigned char>(buffer[size_t(i)])) * 1099511628211ULL;
    }
    if (file.bad())
        throw std::runtime_error("Checkpoint or preview checksum read failed");
    return hash;
}
} // namespace lenia
