// FlecsSaveFileFormat — packed on-disk header layout for FatumGame save files.
//
// Per v2 §5.7 + C1 fix:
//  - #pragma pack(push,1) for byte-exact layout
//  - 8 static_asserts (size + 7 field offsets) — compile-time defense against drift
//  - kMagic = 0x56535446u ('F','T','S','V' little-endian) — verified via magic-encoding assert
//
// CRC is computed over the compressed payload (not decompressed) so the reader can
// verify-before-decompress. Reserved fields MUST be zeroed by header writers.

#pragma once

#include "CoreMinimal.h"

namespace FatumSave
{
	/** Magic bytes: 'F','T','S','V' (FatumGame SaVe) in little-endian. */
	inline constexpr uint32 kMagic       = 0x56535446u;

	/** Current file format version. Bump when header layout or payload framing changes. */
	inline constexpr uint32 kVersion     = 1u;

	/** Minimum supported file format version. Loader refuses anything below. */
	inline constexpr uint32 kMinVersion  = 1u;

	/** Size of FFlecsSaveHeader in bytes. Locked in by static_assert below. */
	inline constexpr uint32 kHeaderSize  = 0x24u;   // 36 bytes

	/** Magic for the payload header (after decompression). 'F','T','S','P' (Payload). */
	inline constexpr uint32 kPayloadMagic = 0x50535446u;

	/** Magic for the payload footer. 'F','T','E','D' (End). */
	inline constexpr uint32 kFooterMagic  = 0x44455446u;

	/** Current payload version (independent of file format version). Bump when entity
	 *  table framing changes globally. Per-component encoders carry their own version.
	 *
	 *  Version 2 (Phase 7): payload header is followed by a length-prefixed WorldName
	 *  string (uint32 ByteLen + UTF-8 bytes + pad-to-4) used for same-level-only load
	 *  enforcement per Q10. Old v1 saves cannot be migrated and are rejected with
	 *  ELoadResult::VersionMismatch — kMinSupportedPayloadVersion bumped accordingly. */
	inline constexpr uint32 kPayloadVersion              = 2u;

	/** Minimum payload version the reader will accept. Phase 7 raised this to 2 with
	 *  the WorldName field; pre-Phase-7 saves have no WorldName and are not supported.
	 *  No migration tool — pre-shipping codebase. */
	inline constexpr uint32 kMinSupportedPayloadVersion  = 2u;
}

// ═══════════════════════════════════════════════════════════════
// FILE HEADER (raw on-disk layout — 36 bytes, packed)
// ═══════════════════════════════════════════════════════════════

#pragma pack(push, 1)

/** Fixed-layout, byte-packed header at the start of every save file. Never compressed.
 *  Writers MUST value-initialize (`FFlecsSaveHeader Hdr{};`) before populating fields,
 *  so Reserved1/Reserved2 are guaranteed zero. */
struct FFlecsSaveHeader
{
	/** offset 0x00 — must equal FatumSave::kMagic ('FTSV' little-endian) */
	uint32 MagicBytes;

	/** offset 0x04 — file format version */
	uint32 Version;

	/** offset 0x08 — length of compressed payload that follows the header */
	uint32 PayloadSizeBytes;

	/** offset 0x0C — 64-bit hash of game build configuration (engine major.minor + git sha) */
	uint64 GameBuildHash;

	/** offset 0x14 — CRC32 of compressed payload (NOT decompressed) */
	uint32 CrcOfPayload;

	/** offset 0x18 — uncompressed payload size, for buffer pre-sizing on read */
	uint32 UncompressedSize;

	/** offset 0x1C — MUST be 0 */
	uint32 Reserved1;

	/** offset 0x20 — MUST be 0 */
	uint32 Reserved2;
	// total size = 0x24 = 36 bytes
};

#pragma pack(pop)

// ═══════════════════════════════════════════════════════════════
// COMPILE-TIME LAYOUT ENFORCEMENT
// ═══════════════════════════════════════════════════════════════

static_assert(sizeof(FFlecsSaveHeader) == FatumSave::kHeaderSize,
              "FFlecsSaveHeader size drift — must be exactly 36 bytes (pragma pack failed?)");
static_assert(offsetof(FFlecsSaveHeader, MagicBytes)       == 0x00, "MagicBytes offset");
static_assert(offsetof(FFlecsSaveHeader, Version)          == 0x04, "Version offset");
static_assert(offsetof(FFlecsSaveHeader, PayloadSizeBytes) == 0x08, "PayloadSizeBytes offset");
static_assert(offsetof(FFlecsSaveHeader, GameBuildHash)    == 0x0C, "GameBuildHash offset");
static_assert(offsetof(FFlecsSaveHeader, CrcOfPayload)     == 0x14, "CrcOfPayload offset");
static_assert(offsetof(FFlecsSaveHeader, UncompressedSize) == 0x18, "UncompressedSize offset");
static_assert(offsetof(FFlecsSaveHeader, Reserved1)        == 0x1C, "Reserved1 offset");
static_assert(offsetof(FFlecsSaveHeader, Reserved2)        == 0x20, "Reserved2 offset");

// Magic byte order sanity (in case host endian differs from x86 — never on our supported
// targets, but the assert documents the on-disk byte order unambiguously).
static_assert(FatumSave::kMagic ==
                  (uint32('F')        |
                   (uint32('T') <<  8) |
                   (uint32('S') << 16) |
                   (uint32('V') << 24)),
              "kMagic must encode 'F','T','S','V' little-endian");

// ═══════════════════════════════════════════════════════════════
// PAYLOAD FRAMING (in the decompressed blob)
// ═══════════════════════════════════════════════════════════════

#pragma pack(push, 1)

/** Header at the start of the decompressed payload. */
struct FFlecsSavePayloadHeader
{
	/** offset 0 — must equal FatumSave::kPayloadMagic ('FTSP') */
	uint32 PayloadMagic;

	/** offset 4 — payload framing version */
	uint32 PayloadVersion;

	/** offset 8 — number of entity records that follow */
	uint32 EntityCount;

	/** offset 12 — number of spawner-override entries (overrides level-placed spawners on load) */
	uint32 SpawnerDedupCount;
	// total size = 16 bytes
};

/** Footer at the end of the decompressed payload — used as a sanity check. */
struct FFlecsSavePayloadFooter
{
	/** offset 0 — must equal FatumSave::kFooterMagic ('FTED') */
	uint32 FooterMagic;

	/** offset 4 — must equal FFlecsSavePayloadHeader::EntityCount */
	uint32 EntityCountEcho;
	// total size = 8 bytes
};

#pragma pack(pop)

static_assert(sizeof(FFlecsSavePayloadHeader) == 16, "FFlecsSavePayloadHeader must be exactly 16 bytes");
static_assert(sizeof(FFlecsSavePayloadFooter) ==  8, "FFlecsSavePayloadFooter must be exactly 8 bytes");
