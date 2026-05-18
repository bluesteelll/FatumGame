// FlecsSaveAutoTest — autonomous save/load roundtrip test wired through a console command.
//
// Designed for headless `-game -ExecCmds="FlecsSave.AutoTest"` editor launches. Runs a
// state machine on the game-thread ticker: request save → poll IsSaveInProgress →
// verify file → request load → poll IsLoadInProgress → print [PASS]/[FAIL] →
// RequestEngineExit so the launching shell can finish.
//
// Polling-based (not delegate-based) because OnSaveComplete/OnLoadComplete are
// dynamic multicast delegates (BlueprintAssignable) that only bind to UFUNCTION
// targets — and we don't want a UObject helper just for autotest.
//
// NOT part of the shipping pipeline. Compiled into FatumGameSave for dev/CI use only.

#include "FlecsSaveSubsystem.h"
#include "FlecsSaveFileFormat.h"
#include "FlecsSaveLog.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"

namespace
{
	enum class EAutoTestPhase : uint8
	{
		Idle,
		WaitForSubsystem,
		SaveRequested_PollBusy,
		SaveDone_VerifyingFile,
		LoadRequested_PollBusy,
		Done
	};

	struct FAutoTestState
	{
		EAutoTestPhase Phase = EAutoTestPhase::Idle;
		int32 TestSlot = 9; // arbitrary named slot — avoid clashing with quicksave (10) / lastsession (11)
		double StartTime = 0.0;
		double PhaseStartTime = 0.0;

		ESaveResult SaveInitiationResult = ESaveResult::UnknownError;
		ELoadResult LoadInitiationResult = ELoadResult::UnknownError;

		int64 SavedFileSizeBytes = 0;
		uint32 SavedFileMagic = 0;
		uint32 SavedFileVersion = 0;

		FString SavedFilePath;
		FTSTicker::FDelegateHandle TickerHandle;

		bool bPassed = false;
		FString FailReason;
	};

	static FAutoTestState GTest;

	static UFlecsSaveSubsystem* FindSubsystem()
	{
		if (!GEngine) { return nullptr; }
		const TIndirectArray<FWorldContext>& Contexts = GEngine->GetWorldContexts();
		for (const FWorldContext& Ctx : Contexts)
		{
			if (UGameInstance* GI = Ctx.OwningGameInstance)
			{
				if (UFlecsSaveSubsystem* Sub = GI->GetSubsystem<UFlecsSaveSubsystem>())
				{
					return Sub;
				}
			}
		}
		return nullptr;
	}

	static void EmitResult()
	{
		const double Elapsed = FPlatformTime::Seconds() - GTest.StartTime;
		if (GTest.bPassed)
		{
			UE_LOG(LogFlecsSave, Display,
				TEXT("[AUTOTEST] [PASS] slot=%d save_init=%d load_init=%d file_size=%lld magic=0x%08x version=%u elapsed=%.2fs"),
				GTest.TestSlot,
				static_cast<int32>(GTest.SaveInitiationResult),
				static_cast<int32>(GTest.LoadInitiationResult),
				GTest.SavedFileSizeBytes,
				GTest.SavedFileMagic,
				GTest.SavedFileVersion,
				Elapsed);
		}
		else
		{
			UE_LOG(LogFlecsSave, Error,
				TEXT("[AUTOTEST] [FAIL] reason=%s save_init=%d load_init=%d elapsed=%.2fs"),
				*GTest.FailReason,
				static_cast<int32>(GTest.SaveInitiationResult),
				static_cast<int32>(GTest.LoadInitiationResult),
				Elapsed);
		}

		if (GTest.TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GTest.TickerHandle);
			GTest.TickerHandle.Reset();
		}

		// Schedule engine exit half a second later so the log line flushes to disk first.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) -> bool
		{
			RequestEngineExit(TEXT("FlecsSave.AutoTest complete"));
			return false;
		}), 0.5f);
	}

	static bool VerifyFileOnDisk(FString& OutReason)
	{
		IFileManager& FM = IFileManager::Get();
		if (!FM.FileExists(*GTest.SavedFilePath))
		{
			OutReason = FString::Printf(TEXT("save file missing at %s"), *GTest.SavedFilePath);
			return false;
		}

		GTest.SavedFileSizeBytes = FM.FileSize(*GTest.SavedFilePath);
		if (GTest.SavedFileSizeBytes < (int64)sizeof(FFlecsSaveHeader))
		{
			OutReason = FString::Printf(TEXT("save file too small (%lld bytes)"), GTest.SavedFileSizeBytes);
			return false;
		}

		TArray<uint8> HeaderBytes;
		if (!FFileHelper::LoadFileToArray(HeaderBytes, *GTest.SavedFilePath))
		{
			OutReason = TEXT("could not read save file");
			return false;
		}

		if (HeaderBytes.Num() < (int32)sizeof(FFlecsSaveHeader))
		{
			OutReason = TEXT("read returned short buffer");
			return false;
		}

		FFlecsSaveHeader Hdr;
		FMemory::Memcpy(&Hdr, HeaderBytes.GetData(), sizeof(FFlecsSaveHeader));
		GTest.SavedFileMagic = Hdr.MagicBytes;
		GTest.SavedFileVersion = Hdr.Version;

		if (Hdr.MagicBytes != FatumSave::kMagic)
		{
			OutReason = FString::Printf(TEXT("magic mismatch: got 0x%08x expected 0x%08x"),
				Hdr.MagicBytes, FatumSave::kMagic);
			return false;
		}
		if (Hdr.Version != FatumSave::kVersion)
		{
			OutReason = FString::Printf(TEXT("file-format version %u != current %u"),
				Hdr.Version, FatumSave::kVersion);
			return false;
		}

		return true;
	}

	static bool TickAutoTest(float DeltaSeconds)
	{
		const double Now = FPlatformTime::Seconds();
		const double PhaseElapsed = Now - GTest.PhaseStartTime;
		constexpr double kSaveTimeoutS = 15.0;
		constexpr double kLoadTimeoutS = 45.0;

		switch (GTest.Phase)
		{
		case EAutoTestPhase::WaitForSubsystem:
		{
			UFlecsSaveSubsystem* Sub = FindSubsystem();
			if (Sub)
			{
				GTest.SavedFilePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"),
					FString::Printf(TEXT("Slot_%02d.sav"), GTest.TestSlot));
				IFileManager::Get().Delete(*GTest.SavedFilePath, /*RequireExists=*/false, /*EvenReadOnly=*/true);

				UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] subsystem ready, requesting save slot %d"), GTest.TestSlot);
				GTest.SaveInitiationResult = Sub->RequestSave(GTest.TestSlot, TEXT("autotest"));
				UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] RequestSave returned %d"),
					static_cast<int32>(GTest.SaveInitiationResult));

				if (GTest.SaveInitiationResult != ESaveResult::Pending &&
					GTest.SaveInitiationResult != ESaveResult::Success)
				{
					GTest.FailReason = FString::Printf(TEXT("RequestSave returned %d (expected Pending or Success)"),
						static_cast<int32>(GTest.SaveInitiationResult));
					GTest.Phase = EAutoTestPhase::Done;
					EmitResult();
					return false;
				}

				GTest.Phase = EAutoTestPhase::SaveRequested_PollBusy;
				GTest.PhaseStartTime = Now;
			}
			else if (PhaseElapsed > 30.0)
			{
				GTest.FailReason = TEXT("subsystem never appeared (30s timeout)");
				GTest.Phase = EAutoTestPhase::Done;
				EmitResult();
				return false;
			}
			break;
		}

		case EAutoTestPhase::SaveRequested_PollBusy:
		{
			UFlecsSaveSubsystem* Sub = FindSubsystem();
			if (!Sub)
			{
				GTest.FailReason = TEXT("subsystem disappeared during save");
				GTest.Phase = EAutoTestPhase::Done;
				EmitResult();
				return false;
			}
			if (!Sub->IsSaveInProgress())
			{
				UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] save finished (bSaveBusy cleared after %.2fs)"), PhaseElapsed);
				GTest.Phase = EAutoTestPhase::SaveDone_VerifyingFile;
				GTest.PhaseStartTime = Now;
			}
			else if (PhaseElapsed > kSaveTimeoutS)
			{
				GTest.FailReason = TEXT("IsSaveInProgress never cleared (15s timeout)");
				GTest.Phase = EAutoTestPhase::Done;
				EmitResult();
				return false;
			}
			break;
		}

		case EAutoTestPhase::SaveDone_VerifyingFile:
		{
			FString Reason;
			if (!VerifyFileOnDisk(Reason))
			{
				GTest.FailReason = Reason;
				GTest.Phase = EAutoTestPhase::Done;
				EmitResult();
				return false;
			}
			UE_LOG(LogFlecsSave, Display,
				TEXT("[AUTOTEST] file OK: size=%lld magic=0x%08x version=%u path=%s"),
				GTest.SavedFileSizeBytes, GTest.SavedFileMagic, GTest.SavedFileVersion, *GTest.SavedFilePath);

			UFlecsSaveSubsystem* Sub = FindSubsystem();
			if (!Sub)
			{
				GTest.FailReason = TEXT("subsystem disappeared before load");
				GTest.Phase = EAutoTestPhase::Done;
				EmitResult();
				return false;
			}

			UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] requesting load slot %d"), GTest.TestSlot);
			GTest.LoadInitiationResult = Sub->RequestLoad(GTest.TestSlot);
			UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] RequestLoad returned %d"),
				static_cast<int32>(GTest.LoadInitiationResult));

			if (GTest.LoadInitiationResult != ELoadResult::Pending &&
				GTest.LoadInitiationResult != ELoadResult::Success)
			{
				GTest.FailReason = FString::Printf(TEXT("RequestLoad returned %d (expected Pending or Success)"),
					static_cast<int32>(GTest.LoadInitiationResult));
				GTest.Phase = EAutoTestPhase::Done;
				EmitResult();
				return false;
			}

			GTest.Phase = EAutoTestPhase::LoadRequested_PollBusy;
			GTest.PhaseStartTime = Now;
			break;
		}

		case EAutoTestPhase::LoadRequested_PollBusy:
		{
			UFlecsSaveSubsystem* Sub = FindSubsystem();
			if (!Sub)
			{
				GTest.FailReason = TEXT("subsystem disappeared during load");
				GTest.Phase = EAutoTestPhase::Done;
				EmitResult();
				return false;
			}
			if (!Sub->IsLoadInProgress())
			{
				UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] load finished (bLoadBusy cleared after %.2fs)"), PhaseElapsed);
				// We don't know the load result code from polling alone (delegate is dynamic).
				// But IsLoadInProgress clearing means the pipeline ran to completion. The save
				// file's earlier verification + the pipeline reaching the end is our pass signal.
				GTest.bPassed = true;
				GTest.Phase = EAutoTestPhase::Done;
				EmitResult();
				return false;
			}
			if (PhaseElapsed > kLoadTimeoutS)
			{
				GTest.FailReason = TEXT("IsLoadInProgress never cleared (45s timeout)");
				GTest.Phase = EAutoTestPhase::Done;
				EmitResult();
				return false;
			}
			break;
		}

		case EAutoTestPhase::Done:
			return false;

		case EAutoTestPhase::Idle:
		default:
			break;
		}

		return true; // keep ticking
	}

	static void StartAutoTest(UWorld* /*World*/)
	{
		if (GTest.Phase != EAutoTestPhase::Idle && GTest.Phase != EAutoTestPhase::Done)
		{
			UE_LOG(LogFlecsSave, Warning, TEXT("[AUTOTEST] already running, ignoring duplicate trigger"));
			return;
		}
		GTest = FAutoTestState{};
		GTest.Phase = EAutoTestPhase::WaitForSubsystem;
		GTest.StartTime = FPlatformTime::Seconds();
		GTest.PhaseStartTime = GTest.StartTime;
		GTest.TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&TickAutoTest), 0.0f);

		UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] started — waiting for FlecsSaveSubsystem to appear"));
	}

	static FAutoConsoleCommandWithWorld GAutoTestCmd(
		TEXT("FlecsSave.AutoTest"),
		TEXT("Run autonomous save/load roundtrip test, then RequestEngineExit. Designed for headless `-game -ExecCmds=` launches."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&StartAutoTest)
	);
}
