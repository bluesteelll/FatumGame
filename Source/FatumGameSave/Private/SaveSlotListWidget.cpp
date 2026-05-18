// USaveSlotListWidget — C++ data layer for save/load UI panels.

#include "SaveSlotListWidget.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveSubsystem.h"
#include "FlecsSaveFileFormat.h"
#include "FlecsSaveFileIO.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE — bind/unbind subsystem delegates
// ═══════════════════════════════════════════════════════════════

void USaveSlotListWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UFlecsSaveSubsystem* Sub = GetSaveSubsystem())
	{
		Sub->OnSaveComplete.AddDynamic(this, &USaveSlotListWidget::HandleSaveComplete);
		Sub->OnLoadComplete.AddDynamic(this, &USaveSlotListWidget::HandleLoadComplete);
		bSubscribed = true;
	}
	else
	{
		UE_LOG(LogFlecsSave, Warning,
			TEXT("USaveSlotListWidget [%s]: subsystem unavailable at NativeConstruct"),
			*GetName());
	}
}

void USaveSlotListWidget::NativeDestruct()
{
	if (bSubscribed)
	{
		if (UFlecsSaveSubsystem* Sub = GetSaveSubsystem())
		{
			Sub->OnSaveComplete.RemoveDynamic(this, &USaveSlotListWidget::HandleSaveComplete);
			Sub->OnLoadComplete.RemoveDynamic(this, &USaveSlotListWidget::HandleLoadComplete);
		}
		bSubscribed = false;
	}

	Super::NativeDestruct();
}

UFlecsSaveSubsystem* USaveSlotListWidget::GetSaveSubsystem() const
{
	UWorld* W = GetWorld();
	if (!W) return nullptr;
	UGameInstance* GI = W->GetGameInstance();
	if (!GI) return nullptr;
	return GI->GetSubsystem<UFlecsSaveSubsystem>();
}

// ═══════════════════════════════════════════════════════════════
// QUERY
// ═══════════════════════════════════════════════════════════════

int32 USaveSlotListWidget::GetSlotCount() const
{
	return UFlecsSaveSubsystem::kTotalSlots;
}

namespace
{
	// Fill FSaveSlotInfo for a single slot. Reads just the file header (36 bytes) — full
	// CRC/decompress is deferred to RequestLoad. Phase 6 metadata is "is the slot occupied
	// and how big / how fresh", sufficient to render a slot list. Display name + level name
	// are left empty (Phase 7 metadata index).
	void PopulateSlotInfo(int32 SlotIndex, FSaveSlotInfo& OutInfo)
	{
		OutInfo = FSaveSlotInfo{};
		OutInfo.SlotName = UFlecsSaveSubsystem::GetSlotNameForIndex(SlotIndex);

		const FString FilePath = FlecsSaveIO::GetSlotFilePath(OutInfo.SlotName, 0);

		IFileManager& FM = IFileManager::Get();
		if (!FM.FileExists(*FilePath))
		{
			return; // bExists already false
		}

		OutInfo.bExists      = true;
		OutInfo.FileSizeBytes = FM.FileSize(*FilePath);
		OutInfo.Timestamp     = FM.GetTimeStamp(*FilePath);

		// Best-effort header parse for GameBuildHash. We open the file directly rather than
		// loading the whole blob (saves can grow into the megabytes once Phase 2+ encoders
		// land). Failure to read the header is logged but not fatal — we still surface
		// bExists=true so the UI can offer Delete/Overwrite.
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		IFileHandle* Handle = PF.OpenRead(*FilePath);
		if (!Handle)
		{
			OutInfo.bCorrupt = true;
			return;
		}

		FFlecsSaveHeader Header{};
		const bool bReadOk = Handle->Read(reinterpret_cast<uint8*>(&Header), sizeof(Header));
		delete Handle;

		if (!bReadOk || Header.MagicBytes != FatumSave::kMagic)
		{
			OutInfo.bCorrupt = true;
			return;
		}

		OutInfo.GameBuildHash = static_cast<int64>(Header.GameBuildHash);
		// Header version range check — corrupt flag also picks up "saved from future build".
		if (Header.Version < FatumSave::kMinVersion || Header.Version > FatumSave::kVersion)
		{
			OutInfo.bCorrupt = true;
		}
	}
}

TArray<FSaveSlotInfo> USaveSlotListWidget::GetAllSlotInfos() const
{
	TArray<FSaveSlotInfo> Out;
	const int32 Total = GetSlotCount();
	Out.Reserve(Total);
	for (int32 i = 0; i < Total; ++i)
	{
		FSaveSlotInfo Info;
		PopulateSlotInfo(i, Info);
		Out.Add(MoveTemp(Info));
	}
	return Out;
}

FSaveSlotInfo USaveSlotListWidget::GetSlotInfo(int32 SlotIndex) const
{
	FSaveSlotInfo Info;
	if (SlotIndex < 0 || SlotIndex >= GetSlotCount())
	{
		UE_LOG(LogFlecsSave, Warning,
			TEXT("USaveSlotListWidget::GetSlotInfo: slot %d out of range"), SlotIndex);
		return Info;
	}
	PopulateSlotInfo(SlotIndex, Info);
	return Info;
}

// ═══════════════════════════════════════════════════════════════
// COMMANDS
// ═══════════════════════════════════════════════════════════════

ESaveResult USaveSlotListWidget::TriggerSave(int32 SlotIndex, const FString& DisplayName)
{
	UFlecsSaveSubsystem* Sub = GetSaveSubsystem();
	if (!Sub)
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("TriggerSave: subsystem missing"));
		return ESaveResult::NoWorld;
	}
	return Sub->RequestSave(SlotIndex, DisplayName);
}

ELoadResult USaveSlotListWidget::TriggerLoad(int32 SlotIndex)
{
	UFlecsSaveSubsystem* Sub = GetSaveSubsystem();
	if (!Sub)
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("TriggerLoad: subsystem missing"));
		return ELoadResult::NoWorld;
	}
	return Sub->RequestLoad(SlotIndex);
}

bool USaveSlotListWidget::DeleteSlot(int32 SlotIndex)
{
	UFlecsSaveSubsystem* Sub = GetSaveSubsystem();
	if (!Sub)
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("DeleteSlot: subsystem missing"));
		return false;
	}
	return Sub->DeleteSlot(SlotIndex);
}

bool USaveSlotListWidget::IsBusy() const
{
	UFlecsSaveSubsystem* Sub = GetSaveSubsystem();
	return Sub && (Sub->IsSaveInProgress() || Sub->IsLoadInProgress());
}

// ═══════════════════════════════════════════════════════════════
// SUBSYSTEM EVENT FORWARDING
// ═══════════════════════════════════════════════════════════════

void USaveSlotListWidget::HandleSaveComplete(int32 SlotIndex, FString SlotName, ESaveResult Result)
{
	OnSaveComplete.Broadcast(SlotIndex, SlotName, Result);
}

void USaveSlotListWidget::HandleLoadComplete(int32 SlotIndex, FString SlotName, ELoadResult Result)
{
	OnLoadComplete.Broadcast(SlotIndex, SlotName, Result);
}
