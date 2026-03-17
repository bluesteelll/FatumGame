# Настройка сборки

> Требования и конфигурация для сборки FatumGame из исходного кода.

---

## Предварительные требования

| Инструмент | Версия | Примечания |
|-----------|--------|-----------|
| **Unreal Engine** | 5.7 | Сборка из исходников или лаунчер |
| **Visual Studio** | 2022 | Набор инструментов VC++ 14.44 |
| **Windows SDK** | 10.0.22621 | Win11 SDK |
| **CMake** | 3.30.2+ | Требуется для сборки Jolt Physics (через плагин UE4CMake) |
| **Git LFS** | Последний | Контентные ассеты отслеживаются через LFS |

---

## Зависимости модулей

`FatumGame.Build.cs` объявляет следующие публичные зависимости:

### Модули движка

| Модуль | Назначение |
|--------|-----------|
| `Core`, `CoreUObject`, `Engine` | Основы UE |
| `InputCore`, `EnhancedInput` | Система ввода |
| `UMG`, `Slate`, `SlateCore` | Рендеринг UI |
| `CommonUI` | Активируемые панели, маршрутизация ввода |
| `GameplayTags` | Классификация на основе тегов |
| `Niagara` | Эффекты частиц VFX |

### Плагины проекта

| Плагин | Модули | Назначение |
|--------|--------|-----------|
| **Barrage** | `Barrage` | Интеграция Jolt Physics |
| **SkeletonKey** | `SkeletonKey` | 64-битная идентификация сущностей |
| **FlecsIntegration** | `UnrealFlecs`, `FlecsLibrary` | Flecs ECS |
| **FlecsBarrage** | `FlecsBarrage` | Мост ECS ↔ физика |
| **FlecsUI** | `FlecsUI` | UI-фреймворк |
| **SolidMacros** | `SolidMacros` | Утилиты макросов |

### Пути включений

Файл сборки добавляет пути включений Public и Private для всех 16 доменных папок:

```
Core, Character, Movement, Weapon, Abilities, Climbing, Stealth,
Destructible, Door, Item, Interaction, Spawning, Rendering, UI,
Input, Utils, Definitions, Vitals
```

Директория `Public/` каждого домена добавляется как публичный путь включений. Директории `Private/` добавляются как приватные включения для предотвращения утечки заголовков.

---

## Цепочка сборки плагинов

### Jolt Physics (через Barrage)

Jolt собирается из исходников через CMake. Плагин `UE4CMake` обеспечивает интеграцию CMake -> UE сборки:

```
Plugins/UE4CMake/  → CMake target для Jolt
Plugins/Barrage/   → UE-обёртка вокруг API Jolt
```

Jolt компилируется как статическая библиотека, линкуемая в модуль Barrage. Отдельной DLL Jolt или runtime-зависимости нет.

### Flecs

Flecs компилируется как часть модуля `UnrealFlecs` (исходники включены в плагин). Модуль `FlecsLibrary` предоставляет UE-специфичные хелперы.

---

## Первая сборка

1. Клонируйте репозиторий (убедитесь, что Git LFS инициализирован):
   ```bash
   git clone <repo-url>
   cd FatumGame
   git lfs pull
   ```

2. Сгенерируйте файлы проекта:
   ```bash
   # Используя batch-файл UE
   "C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/GenerateProjectFiles.bat" \
       "D:/Unreal Engine Projects/FatumGame/FatumGame.uproject" -Game
   ```

3. Откройте `FatumGame.sln` в Visual Studio 2022

4. Конфигурация сборки: **Development Editor** / **Win64**

5. Соберите и запустите

---

## Типичные проблемы сборки

### CMake не найден

Сборка Jolt не удаётся, если CMake не в PATH. Установите CMake 3.30.2+ и убедитесь, что он доступен из VS Developer Command Prompt.

### Объекты Git LFS отсутствуют

Если файлы `.uasset` отображаются как pointer-файлы (маленькие текстовые файлы), выполните:
```bash
git lfs install
git lfs pull
```

### Повреждение промежуточных файлов

При необъяснимых ошибках сборки (особенно после смены версии движка):
```bash
# Удалить генерируемые файлы — принудительная полная пересборка
rm -rf Binaries/ Intermediate/
rm -rf Plugins/*/Binaries/ Plugins/*/Intermediate/
```

### TUniquePtr в заголовках UHT

`TUniquePtr<ForwardDeclaredType>` в заголовках UCLASS вызывает C4150, потому что UHT-генерируемые файлы `.gen.cpp` инстанциируют конструктор/деструктор. Используйте сырой указатель с ручным `new`/`delete` в `Initialize()`/`Deinitialize()`.
