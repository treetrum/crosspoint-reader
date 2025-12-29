/**
 * XtcTypes.h
 *
 * XTC file format type definitions
 * XTC ebook support for CrossPoint Reader
 *
 * XTC is the native binary ebook format for XTeink X4 e-reader.
 * It stores pre-rendered bitmap images per page.
 *
 * Format based on EPUB2XTC converter by Rafal-P-Mazur
 */

#pragma once

#include <cstdint>
#include <string>

namespace xtc {

// XTC file magic numbers (little-endian)
// "XTC\0" = 0x58, 0x54, 0x43, 0x00
constexpr uint32_t XTC_MAGIC = 0x00435458;  // "XTC\0" in little-endian (1-bit fast mode)
// "XTCH" = 0x58, 0x54, 0x43, 0x48
constexpr uint32_t XTCH_MAGIC = 0x48435458;  // "XTCH" in little-endian (2-bit high quality mode)
// "XTG\0" = 0x58, 0x54, 0x47, 0x00
constexpr uint32_t XTG_MAGIC = 0x00475458;  // "XTG\0" for 1-bit page data
// "XTH\0" = 0x58, 0x54, 0x48, 0x00
constexpr uint32_t XTH_MAGIC = 0x00485458;  // "XTH\0" for 2-bit page data

// XTeink X4 display resolution
constexpr uint16_t DISPLAY_WIDTH = 480;
constexpr uint16_t DISPLAY_HEIGHT = 800;

// XTC file header (56 bytes) - Legacy format (EPUB2XTC converter)
#pragma pack(push, 1)
struct XtcHeader {
  uint32_t magic;            // 0x00: Magic number "XTC\0" (0x00435458)
  uint16_t version;          // 0x04: Format version (typically 1)
  uint16_t pageCount;        // 0x06: Total page count
  uint32_t flags;            // 0x08: Flags/reserved
  uint32_t headerSize;       // 0x0C: Size of header section (typically 88)
  uint32_t reserved1;        // 0x10: Reserved
  uint32_t tocOffset;        // 0x14: TOC offset (0 if unused) - 4 bytes, not 8!
  uint64_t pageTableOffset;  // 0x18: Page table offset
  uint64_t dataOffset;       // 0x20: First page data offset
  uint64_t reserved2;        // 0x28: Reserved
  uint32_t titleOffset;      // 0x30: Title string offset
  uint32_t padding;          // 0x34: Padding to 56 bytes
};
#pragma pack(pop)

// XTC file header V2 (56 bytes) - New format per XTC_FORMAT.md
#pragma pack(push, 1)
struct XtcHeaderV2 {
  uint32_t magic;           // 0x00: File identifier "XTC\0" or "XTCH"
  uint16_t version;         // 0x04: Version number (0x0100 = v1.0)
  uint16_t pageCount;       // 0x06: Total pages
  uint8_t readDirection;    // 0x08: Reading direction (0=L→R, 1=R→L, 2=Top→Bottom)
  uint8_t hasMetadata;      // 0x09: Has metadata (0-1)
  uint8_t hasThumbnails;    // 0x0A: Has thumbnails (0-1)
  uint8_t hasChapters;      // 0x0B: Has chapters (0-1)
  uint32_t currentPage;     // 0x0C: Current page (1-based)
  uint64_t metadataOffset;  // 0x10: Metadata offset
  uint64_t indexOffset;     // 0x18: Index table offset (page table)
  uint64_t dataOffset;      // 0x20: Data area offset
  uint64_t thumbOffset;     // 0x28: Thumbnail offset
  uint64_t chapterOffset;   // 0x30: Chapter data offset
};
#pragma pack(pop)

// XTC Metadata Structure (256 bytes, optional)
#pragma pack(push, 1)
struct XtcMetadata {
  char title[128];        // 0x00: Title (UTF-8, null-terminated)
  char author[64];        // 0x80: Author (UTF-8, null-terminated)
  char publisher[32];     // 0xC0: Publisher/source (UTF-8, null-terminated)
  char language[16];      // 0xE0: Language code (e.g., "zh-CN", "en-US")
  uint32_t createTime;    // 0xF0: Creation time (Unix timestamp)
  uint16_t coverPage;     // 0xF4: Cover page (0-based, 0xFFFF=none)
  uint16_t chapterCount;  // 0xF6: Number of chapters
  uint64_t reserved;      // 0xF8: Reserved (zero-filled)
};
#pragma pack(pop)

// Chapter Structure (96 bytes per chapter, optional)
#pragma pack(push, 1)
struct XtcChapter {
  char chapterName[80];  // 0x00: Chapter name (UTF-8, null-terminated)
  uint16_t startPage;    // 0x50: Start page (0-based)
  uint16_t endPage;      // 0x52: End page (0-based, inclusive)
  uint32_t reserved1;    // 0x54: Reserved 1 (zero-filled)
  uint32_t reserved2;    // 0x58: Reserved 2 (zero-filled)
  uint32_t reserved3;    // 0x5C: Reserved 3 (zero-filled)
};
#pragma pack(pop)

// Chapter info for runtime use
struct ChapterInfo {
  std::string name;
  uint16_t startPage;
  uint16_t endPage;
};

// Page table entry (16 bytes per page)
#pragma pack(push, 1)
struct PageTableEntry {
  uint64_t dataOffset;  // 0x00: Absolute offset to page data
  uint32_t dataSize;    // 0x08: Page data size in bytes
  uint16_t width;       // 0x0C: Page width (480)
  uint16_t height;      // 0x0E: Page height (800)
};
#pragma pack(pop)

// XTG/XTH page data header (22 bytes)
// Used for both 1-bit (XTG) and 2-bit (XTH) formats
#pragma pack(push, 1)
struct XtgPageHeader {
  uint32_t magic;       // 0x00: File identifier (XTG: 0x00475458, XTH: 0x00485458)
  uint16_t width;       // 0x04: Image width (pixels)
  uint16_t height;      // 0x06: Image height (pixels)
  uint8_t colorMode;    // 0x08: Color mode (0=monochrome)
  uint8_t compression;  // 0x09: Compression (0=uncompressed)
  uint32_t dataSize;    // 0x0A: Image data size (bytes)
  uint64_t md5;         // 0x0E: MD5 checksum (first 8 bytes, optional)
  // Followed by bitmap data at offset 0x16 (22)
  //
  // XTG (1-bit): Row-major, 8 pixels/byte, MSB first
  //   dataSize = ((width + 7) / 8) * height
  //
  // XTH (2-bit): Two bit planes, column-major (right-to-left), 8 vertical pixels/byte
  //   dataSize = ((width * height + 7) / 8) * 2
  //   First plane: Bit1 for all pixels
  //   Second plane: Bit2 for all pixels
  //   pixelValue = (bit1 << 1) | bit2
};
#pragma pack(pop)

// Page information (internal use, optimized for memory)
struct PageInfo {
  uint32_t offset;   // File offset to page data (max 4GB file size)
  uint32_t size;     // Data size (bytes)
  uint16_t width;    // Page width
  uint16_t height;   // Page height
  uint8_t bitDepth;  // 1 = XTG (1-bit), 2 = XTH (2-bit grayscale)
  uint8_t padding;   // Alignment padding
};  // 16 bytes total

// Error codes
enum class XtcError {
  OK = 0,
  FILE_NOT_FOUND,
  INVALID_MAGIC,
  INVALID_VERSION,
  CORRUPTED_HEADER,
  PAGE_OUT_OF_RANGE,
  READ_ERROR,
  WRITE_ERROR,
  MEMORY_ERROR,
  DECOMPRESSION_ERROR,
};

// Convert error code to string
inline const char* errorToString(XtcError err) {
  switch (err) {
    case XtcError::OK:
      return "OK";
    case XtcError::FILE_NOT_FOUND:
      return "File not found";
    case XtcError::INVALID_MAGIC:
      return "Invalid magic number";
    case XtcError::INVALID_VERSION:
      return "Unsupported version";
    case XtcError::CORRUPTED_HEADER:
      return "Corrupted header";
    case XtcError::PAGE_OUT_OF_RANGE:
      return "Page out of range";
    case XtcError::READ_ERROR:
      return "Read error";
    case XtcError::WRITE_ERROR:
      return "Write error";
    case XtcError::MEMORY_ERROR:
      return "Memory allocation error";
    case XtcError::DECOMPRESSION_ERROR:
      return "Decompression error";
    default:
      return "Unknown error";
  }
}

/**
 * Check if filename has XTC/XTCH extension
 */
inline bool isXtcExtension(const char* filename) {
  if (!filename) return false;
  const char* ext = strrchr(filename, '.');
  if (!ext) return false;
  return (strcasecmp(ext, ".xtc") == 0 || strcasecmp(ext, ".xtch") == 0);
}

}  // namespace xtc
