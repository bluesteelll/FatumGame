// UI implementation for AFlecsCharacter.
// HUD, Inventory, Loot panel creation/management, inventory container spawning.

#include "FlecsCharacter.h"
#include "FlecsHUDWidget.h"
#include "FlecsInventoryWidget.h"
#include "FlecsLootPanel.h"
#include "FlecsEntityDefinition.h"
#include "FlecsContainerProfile.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsItemComponents.h"
#include "FlecsVitalsComponents.h"
#include "GameFramework/PlayerController.h"
#include "Async/Async.h"
#include "InputActionValue.h"

// ═══════════════════════════════════════════════════════════════════════════
// INIT (called from BeginPlay)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::InitUI()
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC) return;

	// Create HUD widget
	if (HUDWidgetClass)
	{
		HUDWidget = CreateWidget<UFlecsHUDWidget>(PC, HUDWidgetClass);
		if (HUDWidget)
		{
			HUDWidget->AddToViewport();
		}
	}

	// Create inventory widget (hidden by default)
	if (InventoryWidgetClass)
	{
		InventoryWidget = CreateWidget<UFlecsInventoryWidget>(PC, InventoryWidgetClass);
		if (InventoryWidget)
		{
			InventoryWidget->SetOwningCharacter(this);
			InventoryWidget->GameplayMappingContext = GameplayMappingContext;
			InventoryWidget->PanelMappingContext = InventoryMappingContext;
			InventoryWidget->AddToViewport(10); // Above HUD
			InventoryWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Create loot panel (hidden by default)
	if (LootPanelClass)
	{
		LootPanel = CreateWidget<UFlecsLootPanel>(PC, LootPanelClass);
		if (LootPanel)
		{
			LootPanel->SetOwningCharacter(this);
			LootPanel->GameplayMappingContext = GameplayMappingContext;
			LootPanel->PanelMappingContext = InventoryMappingContext;
			LootPanel->AddToViewport(10);
			LootPanel->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void AFlecsCharacter::InitInventoryContainers()
{
	if (!InventoryDefinition && !WeaponInventoryDefinition) return;

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return;

	FSkeletonKey Key = CharacterKey;
	UFlecsEntityDefinition* InvDef = InventoryDefinition;
	UFlecsEntityDefinition* WepInvDef = WeaponInventoryDefinition;

	FlecsSubsystem->EnqueueCommand([this, FlecsSubsystem, InvDef, WepInvDef, Key]()
	{
		flecs::world* World = FlecsSubsystem->GetFlecsWorld();
		if (!World) return;

		int64 CharEntityId = 0;
		flecs::entity CharEntity = FlecsSubsystem->GetEntityForBarrageKey(Key);
		if (CharEntity.is_valid())
		{
			CharEntityId = static_cast<int64>(CharEntity.id());
		}

		auto SpawnContainer = [&](UFlecsEntityDefinition* Def) -> int64
		{
			if (!Def || !Def->ContainerProfile) return 0;

			flecs::entity Prefab = FlecsSubsystem->GetOrCreateEntityPrefab(Def);
			if (!Prefab.is_valid()) return 0;

			flecs::entity E = World->entity().is_a(Prefab).add<FTagContainer>();

			FContainerInstance Inst;
			Inst.OwnerEntityId = CharEntityId;
			E.set<FContainerInstance>(Inst);

			const FContainerStatic* Static = E.try_get<FContainerStatic>();
			if (Static)
			{
				switch (Static->Type)
				{
				case EContainerType::Slot:
					{
						FContainerSlotsInstance SlotsInst;
						E.set<FContainerSlotsInstance>(SlotsInst);
					}
					break;
				case EContainerType::Grid:
					{
						FContainerGridInstance Grid;
						Grid.Initialize(Static->GridWidth, Static->GridHeight);
						E.set<FContainerGridInstance>(Grid);
					}
					break;
				case EContainerType::List:
					break;
				}
			}

			return static_cast<int64>(E.id());
		};

		int64 InvId = SpawnContainer(InvDef);
		int64 WepInvId = SpawnContainer(WepInvDef);

		// Update FCharacterInventoryRef on the character Flecs entity (vitals equipment scanning)
		if (CharEntity.is_valid() && InvId != 0)
		{
			FCharacterInventoryRef* InvRefPtr = CharEntity.try_get_mut<FCharacterInventoryRef>();
			if (InvRefPtr)
			{
				InvRefPtr->InventoryEntityId = InvId;
			}
		}

		AsyncTask(ENamedThreads::GameThread, [this, InvId, WepInvId]()
		{
			InventoryEntityId = InvId;
			WeaponInventoryEntityId = WepInvId;
			UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: Inventories spawned (General=%lld, Weapon=%lld)"),
				InvId, WepInvId);
		});
	});
}

// ═══════════════════════════════════════════════════════════════════════════
// CLEANUP (called from EndPlay)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::CleanupUI()
{
	if (LootPanel)
	{
		LootPanel->CloseLoot();
		LootPanel->RemoveFromParent();
		LootPanel = nullptr;
	}

	if (InventoryWidget)
	{
		InventoryWidget->CloseInventory();
		InventoryWidget->RemoveFromParent();
		InventoryWidget = nullptr;
	}

	if (HUDWidget)
	{
		HUDWidget->RemoveFromParent();
		HUDWidget = nullptr;
	}

	InventoryEntityId = 0;
	WeaponInventoryEntityId = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// INVENTORY UI
// ═══════════════════════════════════════════════════════════════════════════

bool AFlecsCharacter::IsInventoryOpen() const
{
	return InventoryWidget && InventoryWidget->IsInventoryOpen();
}

bool AFlecsCharacter::IsLootOpen() const
{
	return LootPanel && LootPanel->IsLootOpen();
}

void AFlecsCharacter::ToggleInventory(const FInputActionValue& Value)
{
	// I key closes loot panel if open
	if (IsLootOpen())
	{
		CloseLootPanel();
		return;
	}

	if (!InventoryWidget || InventoryEntityId == 0) return;

	if (InventoryWidget->IsInventoryOpen())
	{
		InventoryWidget->CloseInventory();
		InventoryWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		InventoryWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		InventoryWidget->OpenInventory(InventoryEntityId);
	}
}

void AFlecsCharacter::OpenLootPanel(int64 ExternalContainerEntityId, const FText& ExternalTitle)
{
	if (!LootPanel || InventoryEntityId == 0) return;

	// Close existing loot if open
	if (IsLootOpen()) CloseLootPanel();

	// Close standalone inventory if open
	if (IsInventoryOpen())
	{
		InventoryWidget->CloseInventory();
		InventoryWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	LootPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	LootPanel->OpenLoot(InventoryEntityId, ExternalContainerEntityId, ExternalTitle);
}

void AFlecsCharacter::CloseLootPanel()
{
	if (!LootPanel || !LootPanel->IsLootOpen()) return;

	LootPanel->CloseLoot();
	LootPanel->SetVisibility(ESlateVisibility::Collapsed);
}
