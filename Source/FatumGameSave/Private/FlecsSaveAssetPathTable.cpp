// FlecsSaveAssetPathTable — implementation.

#include "FlecsSaveAssetPathTable.h"
#include "FlecsSaveLog.h"
#include "FlecsEntityDefinition.h"

#include "Serialization/Archive.h"
#include "UObject/SoftObjectPath.h"

uint32 FFlecsSaveAssetPathTable::RegisterPath(const UFlecsEntityDefinition* Definition)
{
	if (!Definition)
	{
		// Caller is expected to filter null definitions; surface here defensively.
		return kInvalidIndex;
	}

	const TWeakObjectPtr<const UFlecsEntityDefinition> Key(Definition);
	if (const uint32* Existing = DefinitionToIndex.Find(Key))
	{
		return *Existing;
	}

	// Cache the canonical asset path string. GetPathName produces "/Game/.../DA_Foo.DA_Foo".
	FString Path = Definition->GetPathName();
	const uint32 NewIndex = static_cast<uint32>(PathStrings.Num());
	PathStrings.Add(MoveTemp(Path));
	DefinitionToIndex.Add(Key, NewIndex);
	return NewIndex;
}

void FFlecsSaveAssetPathTable::Serialize(FArchive& Ar)
{
	check(Ar.IsSaving());

	uint32 AssetCount = static_cast<uint32>(PathStrings.Num());
	Ar << AssetCount;

	for (const FString& Path : PathStrings)
	{
		const FTCHARToUTF8 Utf8(*Path);
		uint32 ByteLen = static_cast<uint32>(Utf8.Length());
		Ar << ByteLen;
		if (ByteLen > 0)
		{
			Ar.Serialize(const_cast<ANSICHAR*>(reinterpret_cast<const ANSICHAR*>(Utf8.Get())), ByteLen);
		}

		// Align-to-4 padding so subsequent uint32s in the payload stay aligned.
		const uint32 Pad = (4u - (ByteLen & 3u)) & 3u;
		for (uint32 i = 0; i < Pad; ++i)
		{
			uint8 Zero = 0;
			Ar << Zero;
		}
	}

	UE_LOG(LogFlecsSave, Verbose,
		TEXT("FFlecsSaveAssetPathTable::Serialize: wrote %u asset paths"),
		AssetCount);
}

void FFlecsSaveAssetPathTable::Deserialize(FArchive& Ar)
{
	check(Ar.IsLoading());

	uint32 AssetCount = 0;
	Ar << AssetCount;

	// Defensive cap to avoid a corrupted payload trying to allocate gigabytes.
	constexpr uint32 kMaxAssetCount = 1u << 20; // 1M assets is wildly more than any real save.
	checkf(AssetCount <= kMaxAssetCount,
		TEXT("FFlecsSaveAssetPathTable::Deserialize: AssetCount=%u exceeds sane cap %u — corrupted payload"),
		AssetCount, kMaxAssetCount);

	SoftPointers.Reset(static_cast<int32>(AssetCount));
	ResolvedCache.Reset(static_cast<int32>(AssetCount));
	SoftPointers.AddDefaulted(static_cast<int32>(AssetCount));
	ResolvedCache.AddDefaulted(static_cast<int32>(AssetCount));

	for (uint32 i = 0; i < AssetCount; ++i)
	{
		uint32 ByteLen = 0;
		Ar << ByteLen;

		// Same sanity cap on each path string.
		constexpr uint32 kMaxPathBytes = 4096u;
		checkf(ByteLen <= kMaxPathBytes,
			TEXT("FFlecsSaveAssetPathTable::Deserialize: path byte length %u exceeds cap %u"),
			ByteLen, kMaxPathBytes);

		FString Path;
		if (ByteLen > 0)
		{
			TArray<ANSICHAR> Buffer;
			Buffer.SetNumUninitialized(static_cast<int32>(ByteLen) + 1);
			Ar.Serialize(Buffer.GetData(), ByteLen);
			Buffer[static_cast<int32>(ByteLen)] = '\0';
			Path = UTF8_TO_TCHAR(Buffer.GetData());
		}

		// Skip the same align-to-4 padding the writer emitted.
		const uint32 Pad = (4u - (ByteLen & 3u)) & 3u;
		for (uint32 p = 0; p < Pad; ++p)
		{
			uint8 Zero = 0;
			Ar << Zero;
		}

		SoftPointers[static_cast<int32>(i)] = TSoftObjectPtr<UFlecsEntityDefinition>(FSoftObjectPath(Path));
	}

	UE_LOG(LogFlecsSave, Verbose,
		TEXT("FFlecsSaveAssetPathTable::Deserialize: read %u asset paths"),
		AssetCount);
}

UFlecsEntityDefinition* FFlecsSaveAssetPathTable::ResolveDefinition(uint32 Index)
{
	if (Index == kInvalidIndex || Index >= static_cast<uint32>(SoftPointers.Num()))
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("FFlecsSaveAssetPathTable::ResolveDefinition: index %u out of range (Num=%d)"),
			Index, SoftPointers.Num());
		return nullptr;
	}

	const int32 IndexI = static_cast<int32>(Index);

	if (UFlecsEntityDefinition* Cached = ResolvedCache[IndexI])
	{
		return Cached;
	}

	UFlecsEntityDefinition* Loaded = SoftPointers[IndexI].LoadSynchronous();
	if (!Loaded)
	{
		// Per v1 §7 line 1160-1163: missing prefab is a recoverable per-entity error.
		// Caller logs at entity-decode site and skips the entity.
		UE_LOG(LogFlecsSave, Error,
			TEXT("FFlecsSaveAssetPathTable::ResolveDefinition: failed to load asset '%s' (index %u)"),
			*SoftPointers[IndexI].ToString(), Index);
		return nullptr;
	}

	ResolvedCache[IndexI] = Loaded;
	return Loaded;
}
