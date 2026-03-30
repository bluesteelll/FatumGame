# Структура папок

> FatumGame использует вертикальную доменную раскладку папок. Каждый игровой домен (Weapon, Item, Destructible и т.д.) имеет свою папку верхнего уровня, содержащую компоненты, системы и библиотеки. Эта страница -- полная карта.

---

## Раскладка исходного кода

```
Source/FatumGame/
├── FatumGame.h / .cpp                     Точка входа модуля
├── FatumGame.Build.cs                     Правила сборки и зависимости
│
├── Core/                                  Ядро симуляции
│   ├── Public/
│   │   ├── FlecsArtillerySubsystem.h      Центральный узел (UTickableWorldSubsystem)
│   │   ├── FSimulationWorker.h            Поток симуляции 60 Гц
│   │   ├── FLateSyncBridge.h              Lock-free мост game→sim последних значений
│   │   ├── FSimStateCache.h               Lock-free кеш скаляров sim→game
│   │   ├── FlecsGameTags.h                Все ECS-теги + enum EContainerType
│   │   ├── FlecsLibraryHelpers.h          Внутренние хелперы ECS-запросов
│   │   └── Components/
│   │       ├── FlecsHealthComponents.h    FHealthStatic, FDamageStatic, FHealthInstance, FPendingDamage
│   │       ├── FlecsEntityComponents.h    FEntityDefinitionRef, FFocusCameraOverride, FLootStatic
│   │       └── FlecsInteractionComponents.h  FInteractionStatic, FInteractionInstance
│   └── Private/
│       ├── FlecsArtillerySubsystem.cpp              Init, Tick, Deinitialize
│       ├── FlecsArtillerySubsystem_Binding.cpp      Двунаправленная привязка entity↔Barrage
│       ├── FlecsArtillerySubsystem_Collision.cpp    Callback OnBarrageContact
│       ├── FlecsArtillerySubsystem_CollisionSystems.cpp  Настройка систем столкновений
│       ├── FlecsArtillerySubsystem_Items.cpp        Реестр prefab, спавн предметов
│       ├── FlecsArtillerySubsystem_Systems.cpp      RegisterComponents + SetupFlecsSystems
│       ├── FSimulationWorker.cpp                    Цикл Run() потока симуляции
│       ├── FLateSyncBridge.cpp
│       ├── FSimStateCache.cpp
│       └── FlecsGameTags.cpp
│
├── Character/                             Персонаж игрока / NPC
│   ├── Public/
│   │   ├── FlecsCharacter.h               AFlecsCharacter (подкласс ACharacter)
│   │   ├── FatumMovementComponent.h       Пользовательский CMC с физикой Barrage
│   │   └── FPostureStateMachine.h         Автомат состояний стойка/присед/лёжа
│   └── Private/
│       ├── FlecsCharacter.cpp             Init, Tick, BeginPlay
│       ├── FlecsCharacter_ADS.cpp         Пружина прицеливания
│       ├── FlecsCharacter_Combat.cpp      Ввод стрельбы, интеграция прицеливания
│       ├── FlecsCharacter_Input.cpp       Привязка Enhanced Input
│       ├── FlecsCharacter_Interaction.cpp Автомат состояний взаимодействия
│       ├── FlecsCharacter_Physics.cpp     ReadAndApplyBarragePosition
│       ├── FlecsCharacter_Recoil.cpp      Визуальная отдача (удар, тряска, паттерн)
│       ├── FlecsCharacter_RopeVisual.cpp  Визуал качания на верёвке
│       ├── FlecsCharacter_Test.cpp        Тестовый спавн / отладочные команды
│       ├── FlecsCharacter_UI.cpp          Подключение HUD, чтения SimStateCache
│       ├── FlecsCharacter_WeaponCollision.cpp  Избегание коллизии оружия
│       ├── FlecsCharacter_WeaponMotion.cpp     Раскачивание, покачивание, импакт приземления
│       ├── FatumMovementComponent.cpp
│       └── FPostureStateMachine.cpp
│
├── Movement/                              Локомоция персонажа
│   ├── Public/
│   │   ├── FlecsCharacterTypes.h          Обёртки atomics, atomics ввода/состояния
│   │   └── Components/
│   │       ├── FlecsMovementComponents.h  ECharacterPosture, FCharacterMoveState
│   │       └── FlecsMovementStatic.h      Профиль движения → ECS-компонент
│   └── Private/
│       ├── FlecsArtillerySubsystem_Character.cpp  PrepareCharacterStep (sim thread)
│       └── Components/FlecsMovementStatic.cpp
│
├── Weapon/                                Оружие и снаряды
│   ├── Public/
│   │   ├── Components/
│   │   │   ├── FlecsWeaponComponents.h    FAimDirection, FWeaponStatic/Instance
│   │   │   ├── FlecsProjectileComponents.h  FProjectileStatic/Instance
│   │   │   ├── FlecsRecoilTypes.h         FShotFiredEvent
│   │   │   └── FlecsRecoilState.h         FWeaponRecoilState (game thread)
│   │   └── Library/
│   │       ├── FlecsDamageLibrary.h       BP: ApplyDamage, Heal, Kill, GetHealth
│   │       └── FlecsWeaponLibrary.h       BP: StartFiring, StopFiring, Reload
│   └── Private/
│       ├── Components/
│       ├── Library/
│       └── Systems/
│           ├── FlecsArtillerySubsystem_WeaponSystems.cpp   Fire, Reload, Tick
│           └── FlecsArtillerySubsystem_DamageCollision.cpp DamageCollision, BounceCollision
│
├── Abilities/                             Система способностей
│   ├── Public/
│   │   ├── AbilityLifecycleManager.h      Потиковое обновление слотов способностей
│   │   ├── AbilityTickFunctions.h         Таблица диспатча по типам
│   │   ├── AbilityCapsuleHelper.h         Изменение размера капсулы для способностей
│   │   └── Components/
│   │       ├── FlecsAbilityTypes.h        EAbilityTypeId, FAbilitySlot, FAbilitySystem
│   │       ├── FlecsAbilityStates.h       Структуры состояний фаз способностей
│   │       └── FlecsResourceTypes.h       FResourcePool, FAbilityCostEntry
│   └── Private/
│       ├── AbilityLifecycleManager.cpp
│       ├── AbilityTickFunctions.cpp
│       ├── AbilityCapsuleHelper.cpp
│       ├── TickTelekinesis.cpp
│       └── Components/
│
├── Climbing/                              Лазание и качание на верёвке
│   ├── Public/Components/
│   │   ├── FlecsClimbableComponents.h     FClimbableStatic, FClimbInstance
│   │   └── FlecsSwingableComponents.h     FSwingableStatic, FRopeSwingInstance
│   └── Private/
│       ├── TickClimb.cpp
│       └── TickRopeSwing.cpp
│
├── Stealth/                               Система стелса
│   ├── Public/
│   │   ├── FlecsStealthTypes.h            ESurfaceNoise, EStealthLightType
│   │   ├── FlecsLightSourceActor.h        Актор игрового света
│   │   ├── FlecsNoiseZoneActor.h          Актор зоны шума
│   │   └── Components/FlecsStealthComponents.h
│   └── Private/
│       ├── FlecsLightSourceActor.cpp
│       ├── FlecsNoiseZoneActor.cpp
│       └── Systems/FlecsArtillerySubsystem_StealthSystems.cpp
│
├── Destructible/                          Разрушаемые объекты
│   ├── Public/
│   │   ├── FDebrisPool.h                  Предаллоцированный пул тел
│   │   ├── FlecsDestructibleSpawner.h     Спавн сущностей-фрагментов
│   │   ├── FlecsConstrainedGroupSpawner.h Спавн связанных констрейнтами групп
│   │   ├── Components/FlecsDestructibleComponents.h
│   │   └── Library/FlecsConstraintLibrary.h  BP создание констрейнтов
│   └── Private/
│       ├── FDebrisPool.cpp
│       └── Systems/
│           ├── FlecsArtillerySubsystem_DestructibleCollision.cpp
│           └── FlecsArtillerySubsystem_FragmentationSystems.cpp
│
├── Door/                                  Система дверей
│   ├── Public/Components/FlecsDoorComponents.h  FDoorStatic, FDoorInstance
│   └── Private/
│       └── Systems/FlecsArtillerySubsystem_DoorSystems.cpp
│
├── Item/                                  Предметы и контейнеры
│   ├── Public/
│   │   ├── ItemRegistry.h                 Синглтон-реестр TypeId→prefab
│   │   ├── Components/FlecsItemComponents.h  Статические+instance компоненты Item/Container
│   │   └── Library/FlecsContainerLibrary.h   BP: AddItem, Remove, Transfer, Pickup
│   └── Private/
│       ├── ItemRegistry.cpp
│       ├── Library/FlecsContainerLibrary.cpp
│       └── Systems/FlecsArtillerySubsystem_PickupCollision.cpp
│
├── Interaction/                           Система взаимодействия
│   ├── Public/
│   │   ├── FlecsInteractionTypes.h        EInteractionType, EInstantAction, EInteractionState
│   │   └── Library/FlecsInteractionLibrary.h
│   └── Private/Library/FlecsInteractionLibrary.cpp
│
├── Spawning/                              API спавна сущностей
│   ├── Public/
│   │   ├── FlecsEntitySpawner.h           FEntitySpawnRequest + UFlecsEntityLibrary
│   │   ├── FlecsEntitySpawnerActor.h      Размещаемый на уровне актор-спаунер
│   │   └── Library/FlecsSpawnLibrary.h    BP-хелперы спавна
│   └── Private/
│
├── Rendering/                             Управление ISM и VFX
│   ├── Public/
│   │   ├── FlecsRenderManager.h           Жизненный цикл ISM + интерполяция
│   │   ├── FlecsNiagaraManager.h          Интеграция Niagara Array DI
│   │   └── FRopeVisualRenderer.h          Рендеринг сплайна верёвки/цепи
│   └── Private/
│
├── UI/                                    Игровой UI (HUD, инвентарь, лут)
│   ├── Public/
│   │   ├── FlecsUISubsystem.h             Фабрика моделей, состояние тройного буфера
│   │   ├── FlecsMessageSubsystem.h        Типизированная шина сообщений pub/sub
│   │   ├── FlecsUIMessages.h              Все структуры UI-сообщений
│   │   ├── FlecsHUDWidget.h               HUD-виджет (подписчик сообщений)
│   │   ├── FlecsInventoryWidget.h         Панель инвентаря
│   │   ├── FlecsInventoryItemWidget.h     Перетаскиваемая плитка предмета
│   │   ├── FlecsInventorySlotWidget.h     Виджет ячейки сетки
│   │   ├── FlecsContainerGridWidget.h     Полный рендерер сетки
│   │   ├── FlecsInventoryDragPayload.h    Payload операции перетаскивания
│   │   └── FlecsLootPanel.h              Двухконтейнерный вид лута
│   └── Private/
│
├── Input/                                 Система ввода
│   ├── Public/
│   │   ├── FatumInputConfig.h             Маппинг InputAction→GameplayTag
│   │   ├── FatumInputComponent.h          Компонент Enhanced Input
│   │   └── FatumInputTags.h              Константы Gameplay Tag
│   └── Private/
│
├── Vitals/                                Система жизненных показателей / выживания
│   ├── Public/
│   │   ├── Components/FlecsVitalsComponents.h  FVitalsStatic, FVitalsInstance, FVitalsItemStatic
│   │   └── Library/FlecsVitalsLibrary.h        BP: запросы и мутации показателей
│   └── Private/
│       ├── Components/FlecsVitalsComponents.cpp
│       ├── Library/FlecsVitalsLibrary.cpp
│       └── Systems/FlecsArtillerySubsystem_VitalsSystems.cpp
│
├── Definitions/                           ВСЕ Data Assets и профили
│   └── Public/
│       ├── FlecsEntityDefinition.h        Мастер data asset
│       ├── FlecsItemDefinition.h          Data asset метаданных предмета
│       ├── FlecsHealthProfile.h
│       ├── FlecsDamageProfile.h
│       ├── FlecsPhysicsProfile.h
│       ├── FlecsRenderProfile.h
│       ├── FlecsProjectileProfile.h
│       ├── FlecsWeaponProfile.h
│       ├── FlecsContainerProfile.h
│       ├── FlecsInteractionProfile.h
│       ├── FlecsNiagaraProfile.h
│       ├── FlecsDestructibleProfile.h
│       ├── FlecsDestructibleGeometry.h
│       ├── FlecsMovementProfile.h
│       ├── FlecsAbilityDefinition.h
│       ├── FlecsAbilityLoadout.h
│       ├── FlecsResourcePoolProfile.h
│       ├── FlecsDoorProfile.h
│       ├── FlecsClimbProfile.h
│       ├── FlecsRopeSwingProfile.h
│       ├── FlecsStealthLightProfile.h
│       ├── FlecsNoiseZoneProfile.h
│       ├── FlecsContainerDefinition.h
│       ├── FlecsProjectileDefinition.h
│       ├── FlecsConstrainedGroupDefinition.h
│       ├── FlecsCaliberRegistry.h         Синглтон реестра калибров
│       ├── FlecsAmmoTypeDefinition.h      Data asset типа боеприпасов
│       ├── FlecsMagazineProfile.h         Профиль внутреннего магазина
│       ├── FlecsQuickLoadProfile.h        Профиль устройств быстрой зарядки (спидлоадеры, обоймы)
│       ├── FlecsVitalsProfile.h           Жизненные показатели персонажа (голод, жажда и т.д.)
│       ├── FlecsVitalsItemProfile.h       Эффекты предметов на показатели
│       └── FlecsTemperatureZoneProfile.h  Данные температурной зоны
│
└── Utils/                                 Общие утилиты
    ├── Public/
    │   ├── FTimeDilationStack.h           Стек приоритетов замедления времени
    │   ├── ConeImpulse.h                 Хелпер радиального конусного импульса
    │   ├── LedgeDetector.h               Обнаружение уступов на основе Barrage
    │   └── BarrageSpawnUtils.h           Удобные обёртки создания тел
    └── Private/
```

---

## Раскладка плагинов

```
Plugins/
├── Barrage/                  Интеграция Jolt Physics
│   ├── Barrage/              Runtime-модуль (UBarrageDispatch, FBarragePrimitive, слои)
│   └── BarrageEditor/        Визуальный отладчик
│
├── SkeletonKey/              Система идентификации сущностей
│                             FSkeletonKey, ISkeletonLord, хеш-карта LibCuckoo
│
├── FlecsIntegration/         Flecs ECS для UE
│   ├── UnrealFlecs/          Ядро обёртки Flecs
│   └── FlecsLibrary/         UE-специфичные хелперы
│
├── FlecsBarrage/             Мост ECS ↔ физика
│                             FBarrageBody, FISMRender, FCollisionPair, теги столкновений
│
├── FlecsUI/                  UI-фреймворк
│                             UFlecsUIPanel, UFlecsUIWidget, UFlecsContainerModel,
│                             UFlecsValueModel, UFlecsActionRouter, TTripleBuffer
│
├── LocomoCore/               Математические утилиты (SIMD-сортировка, LSH-группировка)
│
├── UE4CMake/                 CMake-интеграция для сборки Jolt
│
└── NiagaraUIRenderer/        Стороннее: эффекты Niagara в UMG
```

---

## Раскладка контента

```
Content/
├── FlecsDA/                  Data Assets (определения сущностей)
│   ├── DA_PlayerCharacter.uasset
│   ├── DA_Bullet.uasset
│   ├── DA_Bread.uasset
│   └── ...
│
├── Widgets/                  UMG виджет-блюпринты
│   └── WBP_MainHUD.uasset
│
├── FlecsTestMap2.umap        Основной тестовый уровень
│
├── Input/                    Ассеты input action / mapping
└── Materials/                Общие материалы
```

---

## Соглашения об именах

| Тип | Паттерн | Пример |
|-----|---------|--------|
| Data Asset | `DA_<Name>` | `DA_Bullet`, `DA_PlayerCharacter` |
| Виджет Blueprint | `WBP_<Name>` | `WBP_MainHUD` |
| Статический компонент | `F<Name>Static` | `FHealthStatic`, `FWeaponStatic` |
| Instance-компонент | `F<Name>Instance` | `FHealthInstance`, `FWeaponInstance` |
| Тег | `FTag<Name>` | `FTagDead`, `FTagProjectile` |
| Профиль | `UFlecs<Name>Profile` | `UFlecsHealthProfile` |
| Blueprint-библиотека | `UFlecs<Name>Library` | `UFlecsContainerLibrary` |
| Подсистема | `UFlecs<Name>Subsystem` | `UFlecsArtillerySubsystem` |
| Метод настройки систем | `Setup<Domain>Systems()` | `SetupWeaponSystems()` |
| Метод sim thread | `FlecsArtillerySubsystem_<Domain>.cpp` | `_WeaponSystems.cpp` |
| Частичный файл персонажа | `FlecsCharacter_<Aspect>.cpp` | `_Combat.cpp`, `_Interaction.cpp` |
