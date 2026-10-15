#include "dds_io.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <utility>
#include <filesystem>
#include <fstream>

#include "texture.hpp"

#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

namespace aurora::gfx::dds {
// OpenGL ES 3.0 guarantees no BC formats, so block-compressed images (the
// usual Dolphin texture pack DDS: BC1, BC2, BC3, BC7) decode to RGBA8 here.
enum class DDSEncoding { RGBA8, BGRA8, BC1, BC2, BC3, BC4, BC5, BC7 };

struct ParsedDDSLayout {
  DDSEncoding encoding = DDSEncoding::RGBA8;
  size_t dataOffset = 0;
};

struct DDSPixelFormat {
  uint32_t size;
  uint32_t flags;
  uint32_t fourCC;
  uint32_t rgbBitCount;
  uint32_t rBitMask;
  uint32_t gBitMask;
  uint32_t bBitMask;
  uint32_t aBitMask;
};

static_assert(sizeof(DDSPixelFormat) == 32);

struct DDSHeader {
  uint32_t size;
  uint32_t flags;
  uint32_t height;
  uint32_t width;
  uint32_t pitchOrLinearSize;
  uint32_t depth;
  uint32_t mipMapCount;
  uint32_t reserved1[11];
  DDSPixelFormat ddspf;
  uint32_t caps;
  uint32_t caps2;
  uint32_t caps3;
  uint32_t caps4;
  uint32_t reserved2;
};

static_assert(sizeof(DDSHeader) == 124);

struct DDSHeaderDX10 {
  uint32_t dxgiFormat;
  uint32_t resourceDimension;
  uint32_t miscFlag;
  uint32_t arraySize;
  uint32_t miscFlags2;
};

static_assert(sizeof(DDSHeaderDX10) == 20);

constexpr uint32_t kDDSMagic = 0x20534444; // "DDS"

bool ensure_directory(const std::filesystem::path& dir) noexcept {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return !ec;
}

bool write_binary_file(const std::filesystem::path& path, ArrayRef<uint8_t> data) noexcept {
  if (!ensure_directory(path.parent_path())) {
    return false;
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);

  if (!out) {
    return false;
  }

  out.write(reinterpret_cast<const char*>(data.data()), data.size());
  return static_cast<bool>(out);
}

std::optional<ByteBuffer> read_binary_file(const std::filesystem::path& path) noexcept {
  std::ifstream file(path, std::ios::binary | std::ios::ate);

  if (!file) {
    return std::nullopt;
  }

  const auto size = static_cast<size_t>(file.tellg());

  if (size == 0) {
    return std::nullopt;
  }

  ByteBuffer bytes{size};
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

  if (!file) {
    return std::nullopt;
  }

  return bytes;
}

bool validate_dds_header(const DDSHeader& header) noexcept {
  if (header.size != sizeof(DDSHeader) || header.ddspf.size != sizeof(DDSPixelFormat)) {
    return false;
  }

  if (header.width == 0 || header.height == 0) {
    return false;
  }

  if ((header.caps2 & (0x00000200 | 0x00200000)) != 0) { // Unsupported
    return false;
  }

  return true;
}

std::optional<DDSEncoding> resolve_dx10_format(uint32_t dxgiFormat) noexcept {
  switch (dxgiFormat) {
  case 28: // R8G8B8A8_UNORM
  case 29: // _SRGB
    return DDSEncoding::RGBA8;
  case 87: // B8G8R8A8_UNORM
  case 91: // _SRGB
    return DDSEncoding::BGRA8;
  case 71: // BC1_UNORM
  case 72:
    return DDSEncoding::BC1;
  case 74: // BC2_UNORM
  case 75:
    return DDSEncoding::BC2;
  case 77: // BC3_UNORM
  case 78:
    return DDSEncoding::BC3;
  case 80: // BC4_UNORM
    return DDSEncoding::BC4;
  case 83: // BC5_UNORM
    return DDSEncoding::BC5;
  case 98: // BC7_UNORM
  case 99:
    return DDSEncoding::BC7;
  default:
    return std::nullopt;
  }
}

constexpr uint32_t fourcc(char a, char b, char c, char d) {
  return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}

std::optional<ParsedDDSLayout> resolve_dds_layout(ArrayRef<uint8_t> bytes, const DDSHeader& header) noexcept {
  ParsedDDSLayout out{.dataOffset = sizeof(uint32_t) + sizeof(DDSHeader)};

  if ((header.ddspf.flags & 0x00000004) != 0) { // DDS has FourCC
    switch (header.ddspf.fourCC) {
    case fourcc('D', 'X', 'T', '1'):
      out.encoding = DDSEncoding::BC1;
      return out;
    case fourcc('D', 'X', 'T', '2'):
    case fourcc('D', 'X', 'T', '3'):
      out.encoding = DDSEncoding::BC2;
      return out;
    case fourcc('D', 'X', 'T', '4'):
    case fourcc('D', 'X', 'T', '5'):
      out.encoding = DDSEncoding::BC3;
      return out;
    case fourcc('A', 'T', 'I', '1'):
    case fourcc('B', 'C', '4', 'U'):
      out.encoding = DDSEncoding::BC4;
      return out;
    case fourcc('A', 'T', 'I', '2'):
    case fourcc('B', 'C', '5', 'U'):
      out.encoding = DDSEncoding::BC5;
      return out;
    case fourcc('D', 'X', '1', '0'):
      break;
    default:
      return std::nullopt;
    }

    if (bytes.size() < out.dataOffset + sizeof(DDSHeaderDX10)) {
      return std::nullopt;
    }

    DDSHeaderDX10 dx10;
    std::memcpy(&dx10, bytes.data() + out.dataOffset, sizeof(dx10));
    out.dataOffset += sizeof(DDSHeaderDX10);

    if (dx10.resourceDimension != 3 || dx10.arraySize != 1) {
      return std::nullopt;
    }

    const auto encoding = resolve_dx10_format(dx10.dxgiFormat);

    if (!encoding.has_value()) {
      return std::nullopt;
    }

    out.encoding = *encoding;
    return out;
  }

  if ((header.ddspf.flags & 0x00000040) != 0 && header.ddspf.rgbBitCount == 32) {
    // Images without alpha (DDPF_ALPHAPIXELS clear) are opaque.
    if (header.ddspf.rBitMask == 0x000000FF && header.ddspf.gBitMask == 0x0000FF00 && header.ddspf.bBitMask == 0x00FF0000) {
      return out;
    }

    if (header.ddspf.rBitMask == 0x00FF0000 && header.ddspf.gBitMask == 0x0000FF00 && header.ddspf.bBitMask == 0x000000FF) {
      out.encoding = DDSEncoding::BGRA8;
      return out;
    }
  }

  return std::nullopt;
}

bool block_compressed(DDSEncoding encoding) noexcept { return encoding != DDSEncoding::RGBA8 && encoding != DDSEncoding::BGRA8; }

size_t encoded_level_bytes(DDSEncoding encoding, uint32_t width, uint32_t height) noexcept {
  if (!block_compressed(encoding)) {
    return size_t(width) * height * 4;
  }

  const size_t blocks = size_t((width + 3) / 4) * ((height + 3) / 4);
  return blocks * (encoding == DDSEncoding::BC1 || encoding == DDSEncoding::BC4 ? 8 : 16);
}

// One level to RGBA8 (width x height x 4 bytes at `out`).
void decode_level(DDSEncoding encoding, bool opaque, const uint8_t* src, uint32_t width, uint32_t height, uint8_t* out) noexcept {
  if (!block_compressed(encoding)) {
    std::memcpy(out, src, size_t(width) * height * 4);

    for (size_t i = 0; i < size_t(width) * height * 4; i += 4) {
      if (encoding == DDSEncoding::BGRA8) {
        std::swap(out[i], out[i + 2]);
      }

      if (opaque) {
        out[i + 3] = 0xFF;
      }
    }

    return;
  }

  const size_t blockBytes = encoding == DDSEncoding::BC1 || encoding == DDSEncoding::BC4 ? 8 : 16;
  uint8_t block[4 * 4 * 4];

  for (uint32_t by = 0; by < (height + 3) / 4; ++by) {
    for (uint32_t bx = 0; bx < (width + 3) / 4; ++bx, src += blockBytes) {
      switch (encoding) {
      case DDSEncoding::BC1:
        bcdec_bc1(src, block, 16);
        break;
      case DDSEncoding::BC2:
        bcdec_bc2(src, block, 16);
        break;
      case DDSEncoding::BC3:
        bcdec_bc3(src, block, 16);
        break;
      case DDSEncoding::BC7:
        bcdec_bc7(src, block, 16);
        break;
      case DDSEncoding::BC4: {
        uint8_t r[16];
        bcdec_bc4(src, r, 4);

        for (int i = 0; i < 16; ++i) {
          block[i * 4] = block[i * 4 + 1] = block[i * 4 + 2] = r[i];
          block[i * 4 + 3] = 0xFF;
        }

        break;
      }
      case DDSEncoding::BC5: {
        uint8_t rg[32];
        bcdec_bc5(src, rg, 8);

        for (int i = 0; i < 16; ++i) {
          block[i * 4] = rg[i * 2];
          block[i * 4 + 1] = rg[i * 2 + 1];
          block[i * 4 + 2] = 0;
          block[i * 4 + 3] = 0xFF;
        }

        break;
      }
      default:
        break;
      }

      for (uint32_t y = 0; y < 4 && by * 4 + y < height; ++y) {
        const uint32_t columns = std::min(4u, width - bx * 4);
        std::memcpy(out + (size_t(by * 4 + y) * width + bx * 4) * 4, block + y * 16, columns * 4);
      }
    }
  }
}

std::optional<ConvertedTexture> parse_dds_bytes(ArrayRef<uint8_t> bytes) noexcept {
  if (bytes.size() < sizeof(uint32_t) + sizeof(DDSHeader)) {
    return std::nullopt;
  }

  uint32_t magic = 0;
  std::memcpy(&magic, bytes.data(), sizeof(magic));

  if (magic != kDDSMagic) {
    return std::nullopt;
  }

  DDSHeader header;
  std::memcpy(&header, bytes.data() + sizeof(uint32_t), sizeof(header));

  if (!validate_dds_header(header) || header.width > 16384 || header.height > 16384) {
    return std::nullopt;
  }

  const auto parsedLayout = resolve_dds_layout(bytes, header);

  if (!parsedLayout.has_value()) {
    return std::nullopt;
  }

  // A stored mip chain (DDSD_MIPMAPCOUNT) is kept down to the levels present;
  // levels that would read past the file end are dropped.
  uint32_t mipCount = (header.flags & 0x00020000) && header.mipMapCount > 1 ? header.mipMapCount : 1u;
  const uint32_t fullChain = 32u - uint32_t(std::countl_zero(std::max(header.width, header.height)));
  mipCount = std::min(mipCount, fullChain);
  const bool opaque = !block_compressed(parsedLayout->encoding) && !(header.ddspf.flags & 0x00000001);
  size_t source = parsedLayout->dataOffset;
  uint32_t levels = 0;
  uint64_t outputBytes = 0;

  for (uint32_t mip = 0; mip < mipCount; ++mip) {
    const uint32_t w = std::max(header.width >> mip, 1u), h = std::max(header.height >> mip, 1u);
    const size_t need = encoded_level_bytes(parsedLayout->encoding, w, h);

    if (source + need > bytes.size()) {
      break;
    }

    source += need;
    outputBytes += uint64_t(w) * h * 4;
    ++levels;
  }

  if (levels == 0) {
    return std::nullopt;
  }

  ByteBuffer data{static_cast<size_t>(outputBytes)};
  source = parsedLayout->dataOffset;
  size_t output = 0;

  for (uint32_t mip = 0; mip < levels; ++mip) {
    const uint32_t w = std::max(header.width >> mip, 1u), h = std::max(header.height >> mip, 1u);
    decode_level(parsedLayout->encoding, opaque, bytes.data() + source, w, h, data.data() + output);
    source += encoded_level_bytes(parsedLayout->encoding, w, h);
    output += size_t(w) * h * 4;
  }

  return ConvertedTexture{
      .format = gl::Format::RGBA8,
      .width = header.width,
      .height = header.height,
      .mips = levels,
      .data = std::move(data),
  };
}

std::optional<ConvertedTexture> load_dds_file(const std::filesystem::path& path) noexcept {
  const auto bytes = read_binary_file(path);

  if (!bytes.has_value()) {
    return std::nullopt;
  }

  return parse_dds_bytes(*bytes);
}

ByteBuffer encode_rgba8_dds(uint32_t width, uint32_t height, ArrayRef<uint8_t> pixels) {
  DDSHeader header{};
  header.size = sizeof(DDSHeader);
  header.flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x8;
  header.height = height;
  header.width = width;
  header.pitchOrLinearSize = width * 4;
  header.mipMapCount = 1;
  header.ddspf = {
      .size = sizeof(DDSPixelFormat),
      .flags = 0x00000040 | 0x00000001,
      .fourCC = 0,
      .rgbBitCount = 32,
      .rBitMask = 0x000000FF,
      .gBitMask = 0x0000FF00,
      .bBitMask = 0x00FF0000,
      .aBitMask = 0xFF000000,
  };

  header.caps = 0x00001000;

  ByteBuffer bytes{sizeof(uint32_t) + sizeof(DDSHeader) + pixels.size()};
  std::memcpy(bytes.data(), &kDDSMagic, sizeof(kDDSMagic));
  std::memcpy(bytes.data() + sizeof(uint32_t), &header, sizeof(header));
  std::memcpy(bytes.data() + sizeof(uint32_t) + sizeof(DDSHeader), pixels.data(), pixels.size());
  return bytes;
}

bool write_rgba8_dds(const std::filesystem::path& path, uint32_t width, uint32_t height, ArrayRef<uint8_t> pixels) noexcept {
  return write_binary_file(path, encode_rgba8_dds(width, height, pixels));
}
} // namespace aurora::gfx::dds
