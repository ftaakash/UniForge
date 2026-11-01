#include "core/common/AtomicFile.hpp"

#include <fstream>
#include <string>
#include <system_error>

namespace uniforge {

namespace {

Error ioError(const std::string& what, const std::filesystem::path& path,
              const std::error_code& ec = {}) {
    std::string message = what + " '" + path.generic_u8string() + "'";
    if (ec) {
        message += ": " + ec.message();
    }
    return makeError(ErrorCode::IoError, message,
                     "Check that the folder exists, is writable and has free space.");
}

} // namespace

Result<void> writeFileAtomic(const std::filesystem::path& path, std::string_view content) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return ioError("Cannot create the folder for", path, ec);
        }
    }

    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return ioError("Cannot open for writing", tmp);
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return ioError("Write failed for", tmp);
        }
    }

    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        return ioError("Cannot replace", path, ec);
    }
    return {};
}

} // namespace uniforge
