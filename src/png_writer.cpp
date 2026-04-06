#include "png_writer.h"

#include <fstream>
#include <vector>

namespace pr {
namespace {

inline std::uint32_t png_crc32_update(std::uint32_t crc, std::uint8_t byte) {
  crc ^= byte;
  for (int i = 0; i < 8; ++i) crc = (crc & 1u) ? (0xedb88320u ^ (crc >> 1)) : (crc >> 1);
  return crc;
}

inline std::uint32_t png_crc32(const std::uint8_t* data, std::size_t size) {
  std::uint32_t crc = 0xffffffffu;
  for (std::size_t i = 0; i < size; ++i) crc = png_crc32_update(crc, data[i]);
  return ~crc;
}

inline std::uint32_t png_adler32(const std::uint8_t* data, std::size_t size) {
  constexpr std::uint32_t mod = 65521u;
  std::uint32_t a = 1u;
  std::uint32_t b = 0u;
  for (std::size_t i = 0; i < size; ++i) {
    a = (a + data[i]) % mod;
    b = (b + a) % mod;
  }
  return (b << 16) | a;
}

inline void png_write_u32_be(std::ofstream& out, std::uint32_t value) {
  char bytes[4] = {
      static_cast<char>((value >> 24) & 0xff),
      static_cast<char>((value >> 16) & 0xff),
      static_cast<char>((value >> 8) & 0xff),
      static_cast<char>(value & 0xff)};
  out.write(bytes, 4);
}

inline bool png_write_chunk(std::ofstream& out, const char type[4], const std::vector<std::uint8_t>& data) {
  png_write_u32_be(out, static_cast<std::uint32_t>(data.size()));
  out.write(type, 4);
  if (!data.empty()) out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  std::vector<std::uint8_t> crc_input(4 + data.size());
  for (int i = 0; i < 4; ++i) crc_input[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(type[i]);
  std::copy(data.begin(), data.end(), crc_input.begin() + 4);
  png_write_u32_be(out, png_crc32(crc_input.data(), crc_input.size()));
  return static_cast<bool>(out);
}

}  // namespace

bool save_png(const Framebuffer& framebuffer, const std::string& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;

  const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  out.write(reinterpret_cast<const char*>(signature), 8);

  std::vector<std::uint8_t> ihdr(13);
  ihdr[0] = static_cast<std::uint8_t>((framebuffer.width >> 24) & 0xff);
  ihdr[1] = static_cast<std::uint8_t>((framebuffer.width >> 16) & 0xff);
  ihdr[2] = static_cast<std::uint8_t>((framebuffer.width >> 8) & 0xff);
  ihdr[3] = static_cast<std::uint8_t>(framebuffer.width & 0xff);
  ihdr[4] = static_cast<std::uint8_t>((framebuffer.height >> 24) & 0xff);
  ihdr[5] = static_cast<std::uint8_t>((framebuffer.height >> 16) & 0xff);
  ihdr[6] = static_cast<std::uint8_t>((framebuffer.height >> 8) & 0xff);
  ihdr[7] = static_cast<std::uint8_t>(framebuffer.height & 0xff);
  ihdr[8] = 8;
  ihdr[9] = 6;
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;
  if (!png_write_chunk(out, "IHDR", ihdr)) return false;

  std::vector<std::uint8_t> raw;
  raw.reserve(static_cast<std::size_t>(framebuffer.width) * static_cast<std::size_t>(framebuffer.height) * 4 + framebuffer.height);
  for (int y = 0; y < framebuffer.height; ++y) {
    raw.push_back(0);
    for (int x = 0; x < framebuffer.width; ++x) {
      auto rgba = unpack_rgba(framebuffer.at(x, y));
      raw.insert(raw.end(), rgba.begin(), rgba.end());
    }
  }

  std::vector<std::uint8_t> zlib;
  zlib.push_back(0x78);
  zlib.push_back(0x01);
  std::size_t remaining = raw.size();
  std::size_t offset = 0;
  while (remaining > 0) {
    std::size_t chunk = std::min<std::size_t>(remaining, 65535);
    bool final = (chunk == remaining);
    zlib.push_back(static_cast<std::uint8_t>(final ? 0x01 : 0x00));
    std::uint16_t len = static_cast<std::uint16_t>(chunk);
    std::uint16_t nlen = static_cast<std::uint16_t>(~len);
    zlib.push_back(static_cast<std::uint8_t>(len & 0xff));
    zlib.push_back(static_cast<std::uint8_t>((len >> 8) & 0xff));
    zlib.push_back(static_cast<std::uint8_t>(nlen & 0xff));
    zlib.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xff));
    zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
    offset += chunk;
    remaining -= chunk;
  }
  std::uint32_t adler = png_adler32(raw.data(), raw.size());
  zlib.push_back(static_cast<std::uint8_t>((adler >> 24) & 0xff));
  zlib.push_back(static_cast<std::uint8_t>((adler >> 16) & 0xff));
  zlib.push_back(static_cast<std::uint8_t>((adler >> 8) & 0xff));
  zlib.push_back(static_cast<std::uint8_t>(adler & 0xff));
  if (!png_write_chunk(out, "IDAT", zlib)) return false;
  if (!png_write_chunk(out, "IEND", {})) return false;
  return static_cast<bool>(out);
}

}  // namespace pr
