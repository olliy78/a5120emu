#include "tests/support/fixtures.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>
#include "tests/support/temp_path.h"

#ifndef A5120_TEST_DISK_DIR
#define A5120_TEST_DISK_DIR "."
#endif

namespace fs = std::filesystem;

namespace k1520test {

std::string diskPath(const std::string& name) {
    return std::string(A5120_TEST_DISK_DIR) + "/" + name;
}

std::string readFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

TempDisk::TempDisk(const std::string& fixture_name)
    : TempDisk(fixture_name, fixture_name) {}

TempDisk::TempDisk(const std::string& fixture_name, const std::string& temp_name) {
    path_ = k1520test::tempPath("k1520_" + temp_name);
    std::error_code ec;
    fs::copy_file(diskPath(fixture_name), path_,
                  fs::copy_options::overwrite_existing, ec);
}

TempDisk TempDisk::empty(const std::string& file_name) {
    TempDisk t;
    t.path_ = k1520test::tempPath("k1520_" + file_name);
    std::error_code ec;
    fs::remove(t.path_, ec);          // Rest eines früheren Laufs wegräumen
    return t;
}

TempDisk::TempDisk(TempDisk&& other) noexcept : path_(std::move(other.path_)) {
    other.path_.clear();
}

TempDisk::~TempDisk() {
    if (path_.empty()) return;
    std::error_code ec;
    fs::remove(path_, ec);
}

}  // namespace k1520test
