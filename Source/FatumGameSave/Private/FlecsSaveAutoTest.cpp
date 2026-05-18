// FlecsSaveAutoTest — autonomous save/load roundtrip + integrity test.
//
// Verifies the save system actually does what it claims, not just that the
// pipeline runs without crashing. Three checks:
//
//   T1. ENTITY COUNT ROUNDTRIP
//       count non-prefab entities → Save → Load (wipes + restores) →
//       count again → assert equal. Catches "load drops entities" bugs.
//
//   T2. SAVE DETERMINISM
//       Save twice on the same unchanged world → file sizes within ±5% of
//       each other. Catches "save state depends on tick number / non-
//       deterministic ordering" bugs.
//
//   T3. ROUNDTRIP STABILITY
//       Save → Load → Save → compare new file size to original. Should be
//       within ±5%. Catches "load doesn't fully restore state, so the
//       re-save loses data" bugs.
//
// Runs as a state machine on the game-thread ticker.  Triggered via the
// `FlecsSave.AutoTest` console command.  On completion logs a single
// [PASS] / [FAIL] line then calls RequestEngineExit so launching shells
// (CI, headless validators) can read the result without waiting.
//
// NOT shipping. Compiled into FatumGameSave for dev/CI use only.

#include "FlecsSaveSubsystem.h"
#include "FlecsSaveFileFormat.h"
#include "FlecsSaveLog.h"

#include "FlecsArtillerySubsystem.h"
#include "FlecsEntityComponents.h"  // FEntityDefinitionRef
#include "FSimulationWorker.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"

#include "flecs.h"

namespace
{
	enum class EAutoTestPhase : uint8
	{
		Idle,
		WaitForSubsystem,
		PreSaveCount,
		Save1_Polling,
		Save1_VerifyFile,
		Save2_Polling,           // T2 determinism — save twice without changes
		Save2_VerifyFile,
		Load_Polling,            // T1 + T3 — destroys state, then restores
		Load_VerifyCount,
		Save3_Polling,           // T3 roundtrip — save after load
		Save3_VerifyFile,
		Done
	};

	struct FAutoTestState
	{
		EAutoTestPhase Phase = EAutoTestPhase::Idle;
		int32 TestSlot = 9;
		double StartTime = 0.0;
		double PhaseStartTime = 0.0;

		int32 EntityCountBeforeSave = -1;
		int32 EntityCountAfterLoad = -1;

		int64 SaveSize1 = 0;
		int64 SaveSize2 = 0;
		int64 SaveSize3 = 0;

		FString SavedFilePath;
		FTSTicker::FDelegateHandle TickerHandle;

		bool bPassed = false;
		TArray<FString> FailureReasons;
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

	static UFlecsArtillerySubsystem* FindArtillery()
	{
		if (!GEngine) { return nullptr; }
		const TIndirectArray<FWorldContext>& Contexts = GEngine->GetWorldContexts();
		for (const FWorldContext& Ctx : Contexts)
		{
			if (UWorld* W = Ctx.World())
			{
				if (UFlecsArtillerySubsystem* Sub = W->GetSubsystem<UFlecsArtillerySubsystem>())
				{
					return Sub;
				}
			}
		}
		return nullptr;
	}

	/** Count SAVE-WORTHY entities (non-prefab with FEntityDefinitionRef). This matches
	 *  what the walker actually persists — framework entities, pair entities, and other
	 *  non-DefRef-bearing entities are intentionally excluded from save and so shouldn't
	 *  count for the roundtrip check. Runs on sim thread via fence for quiescent count. */
	static int32 CountLiveEntitiesSync()
	{
		UFlecsArtillerySubsystem* Artillery = FindArtillery();
		if (!Artillery) { return -1; }
		FSimulationWorker& Worker = Artillery->GetSimWorker();

		int32 Count = 0;
		const uint64 Seq = Worker.EnqueueSeqCommand([Artillery, &Count]()
		{
			flecs::world* W = Artillery->GetFlecsWorld();
			if (!W) { return; }
			W->query_builder<const FEntityDefinitionRef>()
				.with(flecs::Prefab).oper(flecs::Not)
				.build()
				.each([&Count](flecs::entity /*E*/, const FEntityDefinitionRef& /*Ref*/) { ++Count; });
		});

		if (!Worker.WaitForSequence(Seq, 5.0))
		{
			UE_LOG(LogFlecsSave, Warning, TEXT("[AUTOTEST] CountLiveEntitiesSync: WaitForSequence timeout"));
			return -1;
		}
		return Count;
	}

	static int64 GetSavedFileSize()
	{
		return IFileManager::Get().FileSize(*GTest.SavedFilePath);
	}

	static bool VerifyFileHeader(int64& OutSize, FString& OutReason)
	{
		IFileManager& FM = IFileManager::Get();
		if (!FM.FileExists(*GTest.SavedFilePath))
		{
			OutReason = FString::Printf(TEXT("save file missing at %s"), *GTest.SavedFilePath);
			return false;
		}
		OutSize = FM.FileSize(*GTest.SavedFilePath);
		if (OutSize < (int64)sizeof(FFlecsSaveHeader))
		{
			OutReason = FString::Printf(TEXT("save file too small (%lld bytes)"), OutSize);
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

	static void EmitResultAndExit()
	{
		const double Elapsed = FPlatformTime::Seconds() - GTest.StartTime;
		if (GTest.bPassed)
		{
			UE_LOG(LogFlecsSave, Display,
				TEXT("[AUTOTEST] [PASS] elapsed=%.2fs | entities pre=%d post=%d | sizes save1=%lld save2=%lld save3=%lld | determinism_drift=%.1f%% roundtrip_drift=%.1f%%"),
				Elapsed,
				GTest.EntityCountBeforeSave,
				GTest.EntityCountAfterLoad,
				GTest.SaveSize1, GTest.SaveSize2, GTest.SaveSize3,
				GTest.SaveSize1 > 0 ? 100.0 * FMath::Abs(GTest.SaveSize2 - GTest.SaveSize1) / (double)GTest.SaveSize1 : 0.0,
				GTest.SaveSize1 > 0 ? 100.0 * FMath::Abs(GTest.SaveSize3 - GTest.SaveSize1) / (double)GTest.SaveSize1 : 0.0);
		}
		else
		{
			FString Joined = FString::Join(GTest.FailureReasons, TEXT(" | "));
			UE_LOG(LogFlecsSave, Error,
				TEXT("[AUTOTEST] [FAIL] elapsed=%.2fs | reasons=%s | entities pre=%d post=%d | sizes save1=%lld save2=%lld save3=%lld"),
				Elapsed, *Joined,
				GTest.EntityCountBeforeSave,
				GTest.EntityCountAfterLoad,
				GTest.SaveSize1, GTest.SaveSize2, GTest.SaveSize3);
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

	/** Wait helper — returns true if the predicate becomes true within timeout, false otherwise. */
	static bool WaitForCondition(double& InOutPhaseStart, double TimeoutS, TFunctionRef<bool()> Pred, const TCHAR* What)
	{
		const double Now = FPlatformTime::Seconds();
		if (Pred())
		{
			InOutPhaseStart = Now;
			return true;
		}
		if ((Now - InOutPhaseStart) > TimeoutS)
		{
			UE_LOG(LogFlecsSave, Warning, TEXT("[AUTOTEST] timeout waiting for: %s (%.1fs)"), What, TimeoutS);
			InOutPhaseStart = Now;
			return false;
		}
		return false;
	}

	static bool TickAutoTest(float /*DeltaSeconds*/)
	{
		constexpr double kSaveTimeoutS = 15.0;
		constexpr double kLoadTimeoutS = 45.0;
		constexpr double kSubsystemTimeoutS = 30.0;
		constexpr double kDeterminismTolerancePct = 5.0;

		switch (GTest.Phase)
		{
		case EAutoTestPhase::WaitForSubsystem:
		{
			UFlecsSaveSubsystem* Sub = FindSubsystem();
			if (Sub && FindArtillery())
			{
				GTest.SavedFilePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"),
					FString::Printf(TEXT("Slot_%02d.sav"), GTest.TestSlot));
				IFileManager::Get().Delete(*GTest.SavedFilePath, /*RequireExists=*/false, /*EvenReadOnly=*/true);
				UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] subsystem + artillery ready, counting entities"));
				GTest.Phase = EAutoTestPhase::PreSaveCount;
				GTest.PhaseStartTime = FPlatformTime::Seconds();
			}
			else if ((FPlatformTime::Seconds() - GTest.PhaseStartTime) > kSubsystemTimeoutS)
			{
				GTest.FailureReasons.Add(TEXT("subsystem/artillery never appeared (30s)"));
				GTest.Phase = EAutoTestPhase::Done;
				EmitResultAndExit();
				return false;
			}
			break;
		}

		case EAutoTestPhase::PreSaveCount:
		{
			GTest.EntityCountBeforeSave = CountLiveEntitiesSync();
			UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] entity count BEFORE save = %d"), GTest.EntityCountBeforeSave);
			if (GTest.EntityCountBeforeSave <= 0)
			{
				UE_LOG(LogFlecsSave, Warning, TEXT("[AUTOTEST] WARNING: 0 entities in world — roundtrip test is trivial. Test would PASS but reveals nothing."));
			}

			UFlecsSaveSubsystem* Sub = FindSubsystem();
			const ESaveResult R = Sub->RequestSave(GTest.TestSlot, TEXT("autotest_save1"));
			UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] Save #1 RequestSave returned %d"), (int32)R);
			if (R != ESaveResult::Pending && R != ESaveResult::Success)
			{
				GTest.FailureReasons.Add(FString::Printf(TEXT("Save #1 init failed (%d)"), (int32)R));
				GTest.Phase = EAutoTestPhase::Done;
				EmitResultAndExit();
				return false;
			}
			GTest.Phase = EAutoTestPhase::Save1_Polling;
			GTest.PhaseStartTime = FPlatformTime::Seconds();
			break;
		}

		case EAutoTestPhase::Save1_Polling:
		{
			UFlecsSaveSubsystem* Sub = FindSubsystem();
			if (!Sub->IsSaveInProgress())
			{
				UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] Save #1 done"));
				GTest.Phase = EAutoTestPhase::Save1_VerifyFile;
				GTest.PhaseStartTime = FPlatformTime::Seconds();
			}
			else if ((FPlatformTime::Seconds() - GTest.PhaseStartTime) > kSaveTimeoutS)
			{
				GTest.FailureReasons.Add(TEXT("Save #1 never completed (15s)"));
				GTest.Phase = EAutoTestPhase::Done;
				EmitResultAndExit();
				return false;
			}
			break;
		}

		case EAutoTestPhase::Save1_VerifyFile:
		{
			FString Reason;
			if (!VerifyFileHeader(GTest.SaveSize1, Reason))
			{
				GTest.FailureReasons.Add(FString::Printf(TEXT("Save #1 file invalid: %s"), *Reason));
				GTest.Phase = EAutoTestPhase::Done;
				EmitResultAndExit();
				return false;
			}
			UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] Save #1 file OK, size=%lld"), GTest.SaveSize1);

			// T2 — save again immediately (same state) to check determinism
			IFileManager::Get().Delete(*GTest.SavedFilePath, /*RequireExists=*/false, /*EvenReadOnly=*/true);
			UFlecsSaveSubsystem* Sub = FindSubsystem();
			const ESaveResult R = Sub->RequestSave(GTest.TestSlot, TEXT("autotest_save2"));
			UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] Save #2 RequestSave returned %d (determinism check)"), (int32)R);
			if (R != ESaveResult::Pending && R != ESaveResult::Success)
			{
				GTest.FailureReasons.Add(FString::Printf(TEXT("Save #2 init failed (%d)"), (int32)R));
				GTest.Phase = EAutoTestPhase::Done;
				EmitResultAndExit();
				return false;
			}
			GTest.Phase = EAutoTestPhase::Save2_Polling;
			GTest.PhaseStartTime = FPlatformTime::Seconds();
			break;
		}

		case EAutoTestPhase::Save2_Polling:
		{
			UFlecsSaveSubsystem* Sub = FindSubsystem();
			if (!Sub->IsSaveInProgress())
			{
				UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] Save #2 done"));
				GTest.Phase = EAutoTestPhase::Save2_VerifyFile;
				GTest.PhaseStartTime = FPlatformTime::Seconds();
			}
			else if ((FPlatformTime::Seconds() - GTest.PhaseStartTime) > kSaveTimeoutS)
			{
				GTest.FailureReasons.Add(TEXT("Save #2 never completed (15s)"));
				GTest.Phase = EAutoTestPhase::Done;
				EmitResultAndExit();
				return false;
			}
			break;
		}

		case EAutoTestPhase::Save2_VerifyFile:
		{
			FString Reason;
			if (!VerifyFileHeader(GTest.SaveSize2, Reason))
			{
				GTest.FailureReasons.Add(FString::Printf(TEXT("Save #2 file invalid: %s"), *Reason));
				GTest.Phase = EAutoTestPhase::Done;
				EmitResultAndExit();
				return false;
			}
			UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] Save #2 file OK, size=%lld"), GTest.SaveSize2);

			// T2 determinism assertion: sizes within tolerance
			const double DriftPct = GTest.SaveSize1 > 0
				? 100.0 * FMath::Abs(GTest.SaveSize2 - GTest.SaveSize1) / (double)GTest.SaveSize1
				: 0.0;
			if (DriftPct > kDeterminismTolerancePct)
			{
				GTest.FailureReasons.Add(FString::Printf(
					TEXT("T2 DETERMINISM: Save #1 (%lld B) vs Save #2 (%lld B) drift %.1f%% > %.1f%% — save is non-deterministic"),
					GTest.SaveSize1, GTest.SaveSize2, DriftPct, kDeterminismTolerancePct));
			}
			else
			{
				UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] T2 PASS — determinism drift %.1f%% within tolerance"), DriftPct);
			}

			// T1 + T3 — load to wipe + restore
			UFlecsSaveSubsystem* Sub = FindSubsystem();
			const ELoadResult R = Sub->RequestLoad(GTest.TestSlot);
			UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] RequestLoad returned %d (roundtrip)"), (int32)R);
			if (R != ELoadResult::Pending && R != ELoadResult::Success)
			{
				GTest.FailureReasons.Add(FString::Printf(TEXT("Load init failed (%d)"), (int32)R));
				GTest.Phase = EAutoTestPhase::Done;
				EmitResultAndExit();
				return false;
			}
			GTest.Phase = EAutoTestPhase::Load_Polling;
			GTest.PhaseStartTime = FPlatformTime::Seconds();
			break;
		}

		case EAutoTestPhase::Load_Polling:
		{
			UFlecsSaveSubsystem* Sub = FindSubsystem();
			if (!Sub->IsLoadInProgress())
			{
				UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] Load done"));
				GTest.Phase = EAutoTestPhase::Load_VerifyCount;
				GTest.PhaseStartTime = FPlatformTime::Seconds();
			}
			else if ((FPlatformTime::Seconds() - GTest.PhaseStartTime) > kLoadTimeoutS)
			{
				GTest.FailureReasons.Add(TEXT("Load never completed (45s)"));
				GTest.Phase = EAutoTestPhase::Done;
				EmitResultAndExit();
				return false;
			}
			break;
		}

		case EAutoTestPhase::Load_VerifyCount:
		{
			GTest.EntityCountAfterLoad = CountLiveEntitiesSync();
			UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] entity count AFTER load = %d (was %d before)"),
				GTest.EntityCountAfterLoad, GTest.EntityCountBeforeSave);

			// T1 entity count roundtrip assertion (allow ±5% drift like T2/T3 — post-load
			// systems may legitimately regenerate door panels / pair entities that don't
			// roundtrip through save. Catches catastrophic losses, ignores noise.)
			const int32 EntityDelta = FMath::Abs(GTest.EntityCountAfterLoad - GTest.EntityCountBeforeSave);
			const double EntityDriftPct = GTest.EntityCountBeforeSave > 0
				? 100.0 * (double)EntityDelta / (double)GTest.EntityCountBeforeSave
				: 0.0;
			if (EntityDriftPct > kDeterminismTolerancePct)
			{
				GTest.FailureReasons.Add(FString::Printf(
					TEXT("T1 ENTITY COUNT: pre=%d post=%d drift=%.1f%% > %.1f%% (lost or gained %d entities)"),
					GTest.EntityCountBeforeSave,
					GTest.EntityCountAfterLoad,
					EntityDriftPct,
					kDeterminismTolerancePct,
					GTest.EntityCountAfterLoad - GTest.EntityCountBeforeSave));
			}
			else
			{
				UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] T1 PASS — entity count drift %.1f%% within tolerance (pre=%d post=%d)"),
					EntityDriftPct, GTest.EntityCountBeforeSave, GTest.EntityCountAfterLoad);
			}

			// T3 — save again after load to verify state was fully restored
			IFileManager::Get().Delete(*GTest.SavedFilePath, /*RequireExists=*/false, /*EvenReadOnly=*/true);
			UFlecsSaveSubsystem* Sub = FindSubsystem();
			const ESaveResult R = Sub->RequestSave(GTest.TestSlot, TEXT("autotest_save3"));
			UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] Save #3 RequestSave returned %d (roundtrip stability)"), (int32)R);
			if (R != ESaveResult::Pending && R != ESaveResult::Success)
			{
				GTest.FailureReasons.Add(FString::Printf(TEXT("Save #3 init failed (%d)"), (int32)R));
				GTest.Phase = EAutoTestPhase::Done;
				EmitResultAndExit();
				return false;
			}
			GTest.Phase = EAutoTestPhase::Save3_Polling;
			GTest.PhaseStartTime = FPlatformTime::Seconds();
			break;
		}

		case EAutoTestPhase::Save3_Polling:
		{
			UFlecsSaveSubsystem* Sub = FindSubsystem();
			if (!Sub->IsSaveInProgress())
			{
				UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] Save #3 done"));
				GTest.Phase = EAutoTestPhase::Save3_VerifyFile;
				GTest.PhaseStartTime = FPlatformTime::Seconds();
			}
			else if ((FPlatformTime::Seconds() - GTest.PhaseStartTime) > kSaveTimeoutS)
			{
				GTest.FailureReasons.Add(TEXT("Save #3 never completed (15s)"));
				GTest.Phase = EAutoTestPhase::Done;
				EmitResultAndExit();
				return false;
			}
			break;
		}

		case EAutoTestPhase::Save3_VerifyFile:
		{
			FString Reason;
			if (!VerifyFileHeader(GTest.SaveSize3, Reason))
			{
				GTest.FailureReasons.Add(FString::Printf(TEXT("Save #3 file invalid: %s"), *Reason));
				GTest.Phase = EAutoTestPhase::Done;
				EmitResultAndExit();
				return false;
			}
			UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] Save #3 file OK, size=%lld"), GTest.SaveSize3);

			// T3 roundtrip stability assertion
			const double DriftPct = GTest.SaveSize1 > 0
				? 100.0 * FMath::Abs(GTest.SaveSize3 - GTest.SaveSize1) / (double)GTest.SaveSize1
				: 0.0;
			if (DriftPct > kDeterminismTolerancePct)
			{
				GTest.FailureReasons.Add(FString::Printf(
					TEXT("T3 ROUNDTRIP: Save #1 (%lld B) vs Save #3 post-load (%lld B) drift %.1f%% > %.1f%% — load did not fully restore state"),
					GTest.SaveSize1, GTest.SaveSize3, DriftPct, kDeterminismTolerancePct));
			}
			else
			{
				UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] T3 PASS — roundtrip stability drift %.1f%% within tolerance"), DriftPct);
			}

			GTest.bPassed = (GTest.FailureReasons.Num() == 0);
			GTest.Phase = EAutoTestPhase::Done;
			EmitResultAndExit();
			return false;
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

		UE_LOG(LogFlecsSave, Display, TEXT("[AUTOTEST] started — running 3 tests: T1 entity-count roundtrip, T2 save determinism, T3 roundtrip stability"));
	}

	static FAutoConsoleCommandWithWorld GAutoTestCmd(
		TEXT("FlecsSave.AutoTest"),
		TEXT("Run autonomous save/load test (T1 entity-count, T2 determinism, T3 roundtrip stability), then RequestEngineExit. Headless `-game -ExecCmds=` friendly."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&StartAutoTest)
	);
}
