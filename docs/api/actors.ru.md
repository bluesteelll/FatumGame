# Акторы

> Классы UE Actor, используемые в FatumGame. Большинство игровых сущностей -- это Flecs entity (не акторы), но персонажи, спаунеры и элементы стелса используют обёртки в виде акторов.

---

## AFlecsCharacter

**Заголовок:** `Character/Public/FlecsCharacter.h`
**Родитель:** `ACharacter`

Основной актор игрока и NPC. Связывает UE (ввод, камера, UI) с симуляцией Flecs/Barrage.

### Ключевые свойства

| Свойство | Тип | Описание |
|----------|-----|----------|
| `FollowCamera` | `UCameraComponent*` | Камера от третьего/первого лица |
| `SpringArm` | `USpringArmComponent*` | Штанга камеры |
| `DilationStack` | `FTimeDilationStack` | Стек приоритетов замедления времени |

### Тестовые свойства

| Свойство | Тип | Описание |
|----------|-----|----------|
| `TestContainerDefinition` | `UFlecsEntityDefinition*` | Контейнер для спавна (клавиша E) |
| `TestItemDefinition` | `UFlecsEntityDefinition*` | Предмет для добавления (клавиша E) |
| `TestEntityDefinition` | `UFlecsEntityDefinition*` | Обычная сущность для спавна (клавиша E) |

### Файлы реализации

14 файлов `.cpp`, разделённых по ответственности. Подробнее см. [Система персонажа](../systems/character-system.md).

---

## AFlecsEntitySpawnerActor

**Заголовок:** `Spawning/Public/FlecsEntitySpawnerActor.h`
**Родитель:** `AActor`

Спаунер сущностей, размещаемый на уровне. Создаёт ECS-сущность из data asset при BeginPlay.

### Свойства

| Свойство | Тип | По умолчанию | Описание |
|----------|-----|-------------|----------|
| `EntityDefinition` | `UFlecsEntityDefinition*` | -- | Data asset для создания (обязательно) |
| `InitialVelocity` | `FVector` | (0,0,0) | Начальная скорость |
| `bOverrideScale` | `bool` | false | Переопределить масштаб из RenderProfile |
| `ScaleOverride` | `FVector` | (1,1,1) | Пользовательский масштаб |
| `bSpawnOnBeginPlay` | `bool` | true | Автоматический спавн при начале игры |
| `bDestroyAfterSpawn` | `bool` | true | Уничтожить актор после создания сущности |
| `bShowPreview` | `bool` | true | Показывать превью меша в редакторе |

### Использование

1. Разместите на уровне
2. Установите `EntityDefinition` на любой data asset
3. Настройте `InitialVelocity` при необходимости
4. Запустите -- сущность создаётся, а актор самоуничтожается

Для ручного управления: установите `bSpawnOnBeginPlay = false`, вызовите `SpawnEntity()` из Blueprint/C++.

---

## AFlecsLightSourceActor

**Заголовок:** `Stealth/Public/FlecsLightSourceActor.h`
**Родитель:** `AActor`

Игровой источник стелс-света. Регистрирует Flecs-сущность с `FStealthLightStatic` при BeginPlay.

### Свойства

| Свойство | Тип | Описание |
|----------|-----|----------|
| `StealthLightProfile` | `UFlecsStealthLightProfile*` | Конфигурация света |

### Примечания

- **Не** излучает видимый свет -- размещайте рядом с визуальными световыми акторами
- Регистрирует ECS-сущность с `FTagStealthLight` + `FStealthLightStatic` + `FWorldPosition`
- Используется системами расчёта стелса для определения видимости персонажа

---

## AFlecsNoiseZoneActor

**Заголовок:** `Stealth/Public/FlecsNoiseZoneActor.h`
**Родитель:** `AActor`

Зона модификатора шума поверхности. Регистрирует Flecs-сущность с `FNoiseZoneStatic` при BeginPlay.

### Свойства

| Свойство | Тип | Описание |
|----------|-----|----------|
| `NoiseZoneProfile` | `UFlecsNoiseZoneProfile*` | Конфигурация зоны |

### Примечания

- Определяет объёмную зону, где шаги персонажа генерируют определённые уровни шума
- Регистрирует ECS-сущность с `FTagNoiseZone` + `FNoiseZoneStatic` + `FWorldPosition`
- Размеры зоны определяются `NoiseZoneProfile.Extent` (полуразмеры в см)
