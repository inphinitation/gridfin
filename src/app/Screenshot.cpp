#include "app/Screenshot.hpp"

#include <array>
#include <fstream>
#include <vector>

namespace gridfin {

namespace {
void put16(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    put16(out, value & 0xFFFF);
    put16(out, value >> 16);
}
} // namespace

bool writeBmp(const std::string& path, std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height)
{
    if (rgba.size() < static_cast<std::size_t>(width) * height * 4) return false;
    const std::uint32_t rowBytes = (width * 3 + 3) & ~3u;
    const std::uint32_t imageBytes = rowBytes * height;

    std::vector<std::uint8_t> out;
    out.reserve(54 + imageBytes);
    out.push_back('B');
    out.push_back('M');
    put32(out, 54 + imageBytes);
    put32(out, 0);
    put32(out, 54);
    put32(out, 40);
    put32(out, width);
    put32(out, height);
    put16(out, 1);
    put16(out, 24);
    put32(out, 0);
    put32(out, imageBytes);
    put32(out, 2835);
    put32(out, 2835);
    put32(out, 0);
    put32(out, 0);

    // BMP rows are stored bottom-up in BGR order.
    for (std::uint32_t row = height; row-- > 0;) {
        const std::size_t start = static_cast<std::size_t>(row) * width * 4;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t i = start + static_cast<std::size_t>(x) * 4;
            out.push_back(rgba[i + 2]);
            out.push_back(rgba[i + 1]);
            out.push_back(rgba[i]);
        }
        for (std::uint32_t pad = width * 3; pad < rowBytes; ++pad) out.push_back(0);
    }

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(file);
}

} // namespace gridfin
