#pragma once
#include <filesystem>

extern bool install_doxygen(std::filesystem::path prefix, bool dry_run = false);
extern bool install_nasm(std::filesystem::path prefix, bool dry_run = false);
