# Plano 1 — Núcleo e temas

> **Para trabalhadores agênticos:** SUB-SKILL OBRIGATÓRIA: use
> superpowers:subagent-driven-development (recomendado) ou
> superpowers:executing-plans para implementar este plano tarefa a tarefa. Os
> passos usam checkbox (`- [ ]`) para acompanhamento.

**Goal:** Entregar `styler_core` — a biblioteca com todo o parsing de temas, sem
dependência de Windows — mais os 55 temas convertidos de tabelas C++ para JSON,
com a conversão provada sem perda por round-trip.

**Architecture:** Toda a lógica de risco do projeto é parsing de string. Ela vive
numa lib estática sem dependência de XAML, COM ou Windows, testável com um runner
comum. Os temas saem das tabelas C++ do mod upstream por um conversor Python que
também sabe fazer o caminho inverso — se `emit(convert(x)) == x` byte a byte, a
conversão é comprovadamente fiel.

**Tech Stack:** C++20 (MSVC), CMake 3.25 + Ninja (ambos inclusos no Visual
Studio), doctest, nlohmann/json, Python 3 para o conversor, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-09-12-taskbar-styler-design.md`

## Global Constraints

- **Licença GPL-3.0.** Obra derivada de `windows-11-taskbar-styler` (m417z).
  Todo arquivo fonte novo leva o cabeçalho de licença curto (SPDX).
- **Idioma:** identificadores e comentários de código em **inglês**; documentação
  (`README.md`, `THEMES.md`, `docs/`) em **português**.
- **Padrão C++:** C++20. Compilador: MSVC do Visual Studio 2026 Community.
- **Sem dependência de Windows em `styler_core`.** Nada de `<windows.h>`,
  `winrt`, COM. Se um teste precisa de Windows, o código está no alvo errado.
- **Strings:** o core trabalha em `std::wstring` / `std::wstring_view` (UTF-16),
  como os dados de origem. A conversão UTF-8 → UTF-16 acontece só no carregador
  de JSON.
- **Zero acesso à rede em runtime.** O conversor pode acessar a rede; ele roda
  uma vez e o resultado é commitado.
- **Fonte upstream:** `vendor/upstream/windows-11-taskbar-styler.wh.cpp`,
  versão 1.9, 20409 linhas, baixado de
  `https://raw.githubusercontent.com/ramensoftware/windhawk-mods/main/mods/windows-11-taskbar-styler.wh.cpp`.
  Commitado sem modificação — é a entrada do conversor e a referência do
  round-trip.

## Estrutura de arquivos

| Arquivo | Responsabilidade |
|---|---|
| `CMakeLists.txt` | Raiz: opções, FetchContent, subdiretórios |
| `src/core/CMakeLists.txt` | Alvo `styler_core` |
| `src/core/include/styler/selector.h` | Tipos e API do parser de seletor |
| `src/core/selector.cpp` | Parser de seletor |
| `src/core/include/styler/style_rule.h` | Tipos e API do parser de regra |
| `src/core/style_rule.cpp` | Parser de regra de estilo |
| `src/core/include/styler/theme.h` | Modelo de tema |
| `src/core/include/styler/theme_loader.h` | API de carga de tema |
| `src/core/theme_loader.cpp` | Carga e validação de JSON |
| `src/core/include/styler/utf.h` | UTF-8 ↔ UTF-16 |
| `src/core/utf.cpp` | Implementação |
| `tests/core/*.cpp` | Testes por unidade |
| `tools/extract_themes.py` | `convert` e `emit` |
| `tools/fetch_theme_credits.py` | Busca autores, uma vez |
| `themes/*.json` | Saída, commitada |
| `.github/workflows/ci.yml` | Build + testes |

---

### Task 1: Esqueleto do projeto, testes e CI

Estabelece o alvo `styler_core`, o runner de testes e o CI verde. Nenhuma lógica
de domínio ainda — o objetivo é que a próxima tarefa só precise escrever teste e
código.

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/core/CMakeLists.txt`
- Create: `src/core/include/styler/version.h`
- Create: `tests/core/CMakeLists.txt`
- Create: `tests/core/test_smoke.cpp`
- Create: `.github/workflows/ci.yml`
- Create: `vendor/upstream/windows-11-taskbar-styler.wh.cpp` (download)

**Interfaces:**
- Consumes: nada.
- Produces: alvo CMake `styler_core` (lib estática) e `styler_core_tests`
  (executável, registrado no `ctest` como `core`). Cabeçalhos do core são
  incluídos como `#include <styler/xxx.h>`.

- [ ] **Step 1: Baixar o fonte upstream**

```bash
mkdir -p vendor/upstream
curl -sSL -o vendor/upstream/windows-11-taskbar-styler.wh.cpp \
  https://raw.githubusercontent.com/ramensoftware/windhawk-mods/main/mods/windows-11-taskbar-styler.wh.cpp
wc -l vendor/upstream/windows-11-taskbar-styler.wh.cpp
```

Esperado: `20409`. Se divergir, o upstream mudou — pare e reporte antes de
seguir, porque o conversor e o round-trip assumem esta versão.

- [ ] **Step 2: Escrever o CMake raiz**

```cmake
cmake_minimum_required(VERSION 3.25)
project(taskbar_styler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TASKBAR_STYLER_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(src/core)

if(TASKBAR_STYLER_BUILD_TESTS)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11)
  FetchContent_MakeAvailable(doctest)

  enable_testing()
  add_subdirectory(tests/core)
endif()
```

- [ ] **Step 3: Escrever o CMake do core e um cabeçalho mínimo**

`src/core/CMakeLists.txt`:

```cmake
add_library(styler_core STATIC
  version.cpp
)

target_include_directories(styler_core PUBLIC include)
target_link_libraries(styler_core PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)
  target_compile_options(styler_core PRIVATE /W4 /permissive-)
endif()
```

`src/core/include/styler/version.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace styler {

// Version of the theme JSON schema this build understands.
inline constexpr int kThemeSchemaVersion = 1;

std::wstring_view CoreVersion();

}  // namespace styler
```

`src/core/version.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/version.h>

namespace styler {

std::wstring_view CoreVersion() {
    return L"0.1.0";
}

}  // namespace styler
```

- [ ] **Step 4: Escrever o teste de fumaça**

`tests/core/CMakeLists.txt`:

```cmake
add_executable(styler_core_tests
  test_smoke.cpp
)

target_link_libraries(styler_core_tests PRIVATE
  styler_core
  doctest::doctest
)

add_test(NAME core COMMAND styler_core_tests)
```

`tests/core/test_smoke.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <styler/version.h>

TEST_CASE("core reports a version") {
    CHECK(styler::CoreVersion() == std::wstring_view(L"0.1.0"));
}
```

- [ ] **Step 5: Configurar e rodar**

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Esperado: `100% tests passed, 0 tests failed out of 1`.

Se `cmake` ou `ninja` não estiverem no PATH, rode antes:
`"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"`,
que adiciona ambos.

- [ ] **Step 6: Escrever o CI**

`.github/workflows/ci.yml`:

```yaml
name: CI

on:
  push:
    branches: [main]
  pull_request:

jobs:
  build-and-test:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4

      - uses: ilammy/msvc-dev-cmd@v1

      - name: Configure
        run: cmake -S . -B build -G Ninja

      - name: Build
        run: cmake --build build

      - name: Test
        run: ctest --test-dir build --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/core tests/core .github vendor
git commit -m "build: esqueleto CMake, doctest e CI"
git push
```

Confira que o CI ficou verde antes de seguir:

```bash
gh run watch
```

---

### Task 2: Parser de seletor

Um seletor é uma cadeia de matchers separados por `>`:
`Taskbar.TaskbarFrame > Grid#RootGrid[2]@CommonStates`. Cada matcher é um tipo,
opcionalmente com `#nome`, `@grupoDeEstadoVisual`, `[índice]` e `[Prop=valor]`.
Existem dois matchers especiais: `*` (zero ou mais ancestrais) e `:root`.

A gramática está implementada em `ElementMatcherFromString`,
`vendor/upstream/windows-11-taskbar-styler.wh.cpp:18051`. As mensagens de erro
de lá são a lista de entradas inválidas a cobrir.

**Files:**
- Create: `src/core/include/styler/selector.h`
- Create: `src/core/selector.cpp`
- Create: `tests/core/test_selector.cpp`
- Modify: `src/core/CMakeLists.txt` (somar `selector.cpp`)
- Modify: `tests/core/CMakeLists.txt` (somar `test_selector.cpp`)

**Interfaces:**
- Consumes: nada.
- Produces:
  - `styler::ElementMatcher` — struct com `kind` (`Element`/`Wildcard`/`Root`),
    `type`, `name`, `visual_state_group` (`std::optional<std::wstring>`),
    `one_based_index` (`int`, 0 = não especificado),
    `property_filters` (`std::vector<std::pair<std::wstring, std::wstring>>`).
  - `styler::ParseError` — exceção derivada de `std::runtime_error`.
  - `styler::ElementMatcher styler::ParseElementMatcher(std::wstring_view)`
  - `std::vector<styler::ElementMatcher> styler::ParseSelector(std::wstring_view)`
    — divide por `>` e parseia cada parte.

- [ ] **Step 1: Escrever o teste falhando**

`tests/core/test_selector.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/selector.h>

using styler::ElementMatcher;
using styler::ParseElementMatcher;
using styler::ParseError;
using styler::ParseSelector;

TEST_CASE("parses a bare type") {
    auto m = ParseElementMatcher(L"Grid");
    CHECK(m.kind == ElementMatcher::Kind::Element);
    CHECK(m.type == L"Grid");
    CHECK(m.name.empty());
    CHECK(m.one_based_index == 0);
    CHECK_FALSE(m.visual_state_group.has_value());
}

TEST_CASE("parses a dotted type with a name") {
    auto m = ParseElementMatcher(L"Taskbar.TaskbarFrame#RootGrid");
    CHECK(m.type == L"Taskbar.TaskbarFrame");
    CHECK(m.name == L"RootGrid");
}

TEST_CASE("parses the wildcard") {
    auto m = ParseElementMatcher(L"*");
    CHECK(m.kind == ElementMatcher::Kind::Wildcard);
}

TEST_CASE("parses the root marker") {
    auto m = ParseElementMatcher(L":root");
    CHECK(m.kind == ElementMatcher::Kind::Root);
}

TEST_CASE("parses a one-based index") {
    auto m = ParseElementMatcher(L"Grid[2]");
    CHECK(m.type == L"Grid");
    CHECK(m.one_based_index == 2);
}

TEST_CASE("parses a visual state group") {
    auto m = ParseElementMatcher(L"Button@CommonStates");
    CHECK(m.type == L"Button");
    CHECK(m.visual_state_group == std::wstring(L"CommonStates"));
}

TEST_CASE("parses a property filter") {
    auto m = ParseElementMatcher(L"Grid[Tag=Chrome]");
    REQUIRE(m.property_filters.size() == 1);
    CHECK(m.property_filters[0].first == L"Tag");
    CHECK(m.property_filters[0].second == L"Chrome");
}

TEST_CASE("parses all decorations at once") {
    auto m = ParseElementMatcher(L"Grid#Root@CommonStates[3][Tag=X]");
    CHECK(m.type == L"Grid");
    CHECK(m.name == L"Root");
    CHECK(m.visual_state_group == std::wstring(L"CommonStates"));
    CHECK(m.one_based_index == 3);
    REQUIRE(m.property_filters.size() == 1);
}

TEST_CASE("trims surrounding whitespace") {
    auto m = ParseElementMatcher(L"  Grid#Root  ");
    CHECK(m.type == L"Grid");
    CHECK(m.name == L"Root");
}

TEST_CASE("splits a selector on '>'") {
    auto parts = ParseSelector(L"Taskbar.TaskbarFrame > Grid#RootGrid > Rectangle");
    REQUIRE(parts.size() == 3);
    CHECK(parts[0].type == L"Taskbar.TaskbarFrame");
    CHECK(parts[1].name == L"RootGrid");
    CHECK(parts[2].type == L"Rectangle");
}

TEST_CASE("rejects malformed input") {
    CHECK_THROWS_AS(ParseElementMatcher(L"#OnlyName"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid#A#B"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid#"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid@A@B"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid[2"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid[]"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid[Tag]"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid[=X]"), ParseError);
}
```

- [ ] **Step 2: Rodar e ver falhar**

Adicione `test_selector.cpp` a `tests/core/CMakeLists.txt` e rode:

```bash
cmake --build build
```

Esperado: FALHA de compilação, `cannot open source file "styler/selector.h"`.

- [ ] **Step 3: Escrever o cabeçalho**

`src/core/include/styler/selector.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace styler {

// Thrown for any malformed theme text: selectors, style rules, JSON shape.
class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One segment of a selector chain.
struct ElementMatcher {
    enum class Kind {
        Element,   // A normal `Type#Name` matcher.
        Wildcard,  // `*`: matches zero or more intermediate ancestors.
        Root,      // `:root`: asserts the next element has no parent.
    };

    Kind kind = Kind::Element;
    std::wstring type;
    std::wstring name;
    std::optional<std::wstring> visual_state_group;
    int one_based_index = 0;  // 0 means unspecified.
    std::vector<std::pair<std::wstring, std::wstring>> property_filters;
};

ElementMatcher ParseElementMatcher(std::wstring_view str);

// Splits on '>' and parses each segment, outermost ancestor first.
std::vector<ElementMatcher> ParseSelector(std::wstring_view str);

}  // namespace styler
```

- [ ] **Step 4: Implementar**

`src/core/selector.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/selector.h>

#include <charconv>
#include <string>

namespace styler {
namespace {

constexpr std::wstring_view kWhitespace = L" \t\r\n";

std::wstring_view Trim(std::wstring_view s) {
    auto first = s.find_first_not_of(kWhitespace);
    if (first == std::wstring_view::npos) {
        return {};
    }
    auto last = s.find_last_not_of(kWhitespace);
    return s.substr(first, last - first + 1);
}

int ParseIndex(std::wstring_view s) {
    int value = 0;
    for (wchar_t c : s) {
        value = value * 10 + (c - L'0');
    }
    return value;
}

}  // namespace

ElementMatcher ParseElementMatcher(std::wstring_view str) {
    ElementMatcher result;

    auto trimmed = Trim(str);
    if (trimmed == L"*") {
        result.kind = ElementMatcher::Kind::Wildcard;
        return result;
    }
    if (trimmed == L":root") {
        result.kind = ElementMatcher::Kind::Root;
        return result;
    }

    auto i = trimmed.find_first_of(L"#@[");
    result.type = Trim(trimmed.substr(0, i));
    if (result.type.empty()) {
        throw ParseError("Bad target syntax, empty type");
    }

    while (i != std::wstring_view::npos) {
        auto next = trimmed.find_first_of(L"#@[", i + 1);
        auto part = trimmed.substr(
            i + 1, next == std::wstring_view::npos ? next : next - (i + 1));

        switch (trimmed[i]) {
            case L'#': {
                if (!result.name.empty()) {
                    throw ParseError("Bad target syntax, more than one name");
                }
                result.name = Trim(part);
                if (result.name.empty()) {
                    throw ParseError("Bad target syntax, empty name");
                }
                break;
            }

            case L'@': {
                if (result.visual_state_group.has_value()) {
                    throw ParseError(
                        "Bad target syntax, more than one visual state group");
                }
                result.visual_state_group = std::wstring(Trim(part));
                break;
            }

            case L'[': {
                auto rule = Trim(part);
                if (rule.empty() || rule.back() != L']') {
                    throw ParseError("Bad target syntax, missing ']'");
                }
                rule = Trim(rule.substr(0, rule.size() - 1));
                if (rule.empty()) {
                    throw ParseError("Bad target syntax, empty property");
                }

                if (rule.find_first_not_of(L"0123456789") ==
                    std::wstring_view::npos) {
                    result.one_based_index = ParseIndex(rule);
                    break;
                }

                auto eq = rule.find(L'=');
                if (eq == std::wstring_view::npos) {
                    throw ParseError(
                        "Bad target syntax, missing '=' in property");
                }

                auto key = Trim(rule.substr(0, eq));
                auto value = Trim(rule.substr(eq + 1));
                if (key.empty()) {
                    throw ParseError("Bad target syntax, empty property name");
                }

                result.property_filters.emplace_back(std::wstring(key),
                                                     std::wstring(value));
                break;
            }

            default:
                break;
        }

        i = next;
    }

    return result;
}

std::vector<ElementMatcher> ParseSelector(std::wstring_view str) {
    std::vector<ElementMatcher> parts;

    size_t pos = 0;
    while (pos <= str.size()) {
        auto sep = str.find(L'>', pos);
        auto piece = str.substr(
            pos, sep == std::wstring_view::npos ? sep : sep - pos);
        parts.push_back(ParseElementMatcher(piece));

        if (sep == std::wstring_view::npos) {
            break;
        }
        pos = sep + 1;
    }

    return parts;
}

}  // namespace styler
```

Some `selector.cpp` a `src/core/CMakeLists.txt`.

- [ ] **Step 5: Rodar e ver passar**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Esperado: todos os `TEST_CASE` de `test_selector.cpp` passando.

- [ ] **Step 6: Commit**

```bash
git add src/core tests/core
git commit -m "feat(core): parser de seletor de elemento"
```

---

### Task 3: Parser de regra de estilo

Uma regra é `Property=value`, `Property:=xamlValue`,
`Property@VisualState=value` ou `Property=>VarName` (captura). Referência:
`ParseRule` em `vendor/upstream/windows-11-taskbar-styler.wh.cpp:18150`.

Duas combinações são proibidas e devem lançar: `:=>` e `@VisualState=>`.

**Files:**
- Create: `src/core/include/styler/style_rule.h`
- Create: `src/core/style_rule.cpp`
- Create: `tests/core/test_style_rule.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`

**Interfaces:**
- Consumes: `styler::ParseError` de `styler/selector.h`.
- Produces:
  - `styler::ValueRule` — `{ std::wstring property_name; std::wstring
    visual_state; std::wstring value; bool is_xaml_value; }`, com método
    `bool IsDynamic() const` (verdadeiro se `value` contém `{{`).
  - `styler::CaptureRule` — `{ std::wstring property_name; std::wstring
    var_name; }`.
  - `using styler::StyleRule = std::variant<ValueRule, CaptureRule>;`
  - `styler::StyleRule styler::ParseStyleRule(std::wstring_view)`
  - `bool styler::IsValidStyleVariableIdentifier(std::wstring_view)` — letras
    ASCII, dígitos e `_`, não começando por dígito, não vazio.

- [ ] **Step 1: Escrever o teste falhando**

`tests/core/test_style_rule.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/style_rule.h>

using styler::CaptureRule;
using styler::ParseError;
using styler::ParseStyleRule;
using styler::ValueRule;

TEST_CASE("parses a plain value rule") {
    auto rule = ParseStyleRule(L"Visibility=Collapsed");
    auto& v = std::get<ValueRule>(rule);
    CHECK(v.property_name == L"Visibility");
    CHECK(v.value == L"Collapsed");
    CHECK_FALSE(v.is_xaml_value);
    CHECK(v.visual_state.empty());
}

TEST_CASE("parses a xaml value rule") {
    auto rule = ParseStyleRule(L"Fill:=$CommonBgBrush");
    auto& v = std::get<ValueRule>(rule);
    CHECK(v.property_name == L"Fill");
    CHECK(v.value == L"$CommonBgBrush");
    CHECK(v.is_xaml_value);
}

TEST_CASE("parses a visual state on a value rule") {
    auto rule = ParseStyleRule(L"Background@PointerOver=Red");
    auto& v = std::get<ValueRule>(rule);
    CHECK(v.property_name == L"Background");
    CHECK(v.visual_state == L"PointerOver");
    CHECK(v.value == L"Red");
}

TEST_CASE("parses a visual state combined with a xaml value") {
    auto rule = ParseStyleRule(L"Background@PointerOver:=<SolidColorBrush Color=\"Red\"/>");
    auto& v = std::get<ValueRule>(rule);
    CHECK(v.property_name == L"Background");
    CHECK(v.visual_state == L"PointerOver");
    CHECK(v.is_xaml_value);
    CHECK(v.value == L"<SolidColorBrush Color=\"Red\"/>");
}

TEST_CASE("keeps '=' inside the value") {
    auto rule = ParseStyleRule(L"Background:=<SolidColorBrush Color=\"Red\"/>");
    auto& v = std::get<ValueRule>(rule);
    CHECK(v.value == L"<SolidColorBrush Color=\"Red\"/>");
}

TEST_CASE("detects a dynamic value") {
    auto rule = ParseStyleRule(L"Width={{SomeExpr}}");
    CHECK(std::get<ValueRule>(rule).IsDynamic());

    auto plain = ParseStyleRule(L"Width=100");
    CHECK_FALSE(std::get<ValueRule>(plain).IsDynamic());
}

TEST_CASE("parses a capture rule") {
    auto rule = ParseStyleRule(L"Background=>SavedBg");
    auto& c = std::get<CaptureRule>(rule);
    CHECK(c.property_name == L"Background");
    CHECK(c.var_name == L"SavedBg");
}

TEST_CASE("trims whitespace around names and values") {
    auto rule = ParseStyleRule(L"  Visibility  =  Collapsed  ");
    auto& v = std::get<ValueRule>(rule);
    CHECK(v.property_name == L"Visibility");
    CHECK(v.value == L"Collapsed");
}

TEST_CASE("rejects malformed rules") {
    CHECK_THROWS_AS(ParseStyleRule(L"NoEqualsSign"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"=Value"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"Prop:=>Var"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"Prop@State=>Var"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"Prop=>"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"=>Var"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"Prop=>1Bad"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"Prop=>has-dash"), ParseError);
}
```

- [ ] **Step 2: Rodar e ver falhar**

```bash
cmake --build build
```

Esperado: FALHA, `cannot open source file "styler/style_rule.h"`.

- [ ] **Step 3: Escrever o cabeçalho**

`src/core/include/styler/style_rule.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <variant>

#include <styler/selector.h>  // for ParseError

namespace styler {

// `Property[@VisualState][:]=value` — sets a property on a matched element.
struct ValueRule {
    std::wstring property_name;
    std::wstring visual_state;
    std::wstring value;
    bool is_xaml_value = false;

    // A `{{...}}` placeholder means the value is re-resolved on every apply.
    bool IsDynamic() const {
        return value.find(L"{{") != std::wstring::npos;
    }
};

// `Property=>VarName` — reads a property into a global style variable.
struct CaptureRule {
    std::wstring property_name;
    std::wstring var_name;
};

using StyleRule = std::variant<ValueRule, CaptureRule>;

bool IsValidStyleVariableIdentifier(std::wstring_view name);

StyleRule ParseStyleRule(std::wstring_view str);

}  // namespace styler
```

- [ ] **Step 4: Implementar**

`src/core/style_rule.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/style_rule.h>

namespace styler {
namespace {

constexpr std::wstring_view kWhitespace = L" \t\r\n";

std::wstring_view Trim(std::wstring_view s) {
    auto first = s.find_first_not_of(kWhitespace);
    if (first == std::wstring_view::npos) {
        return {};
    }
    auto last = s.find_last_not_of(kWhitespace);
    return s.substr(first, last - first + 1);
}

}  // namespace

bool IsValidStyleVariableIdentifier(std::wstring_view name) {
    if (name.empty()) {
        return false;
    }
    if (name.front() >= L'0' && name.front() <= L'9') {
        return false;
    }
    for (wchar_t c : name) {
        bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                  (c >= L'0' && c <= L'9') || c == L'_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

StyleRule ParseStyleRule(std::wstring_view str) {
    auto eq = str.find(L'=');
    if (eq == std::wstring_view::npos) {
        throw ParseError("Bad style syntax, '=' is missing");
    }

    auto name = str.substr(0, eq);
    auto value = str.substr(eq + 1);

    if (!value.empty() && value.front() == L'>') {
        value = value.substr(1);

        if (!name.empty() && name.back() == L':') {
            throw ParseError(
                "Bad style syntax, ':=>' is not valid (':=' XAML value cannot "
                "be combined with '=>' capture)");
        }
        if (name.find(L'@') != std::wstring_view::npos) {
            throw ParseError(
                "Bad style syntax, '@VisualState' not allowed on a capture "
                "rule");
        }

        auto property_name = Trim(name);
        if (property_name.empty()) {
            throw ParseError("Bad style syntax, empty name");
        }

        auto var_name = Trim(value);
        if (var_name.empty()) {
            throw ParseError("Bad style syntax, empty capture variable name");
        }
        if (!IsValidStyleVariableIdentifier(var_name)) {
            throw ParseError("Bad style syntax, invalid capture variable name");
        }

        return CaptureRule{std::wstring(property_name),
                           std::wstring(var_name)};
    }

    ValueRule result;
    result.value = Trim(value);

    if (!name.empty() && name.back() == L':') {
        result.is_xaml_value = true;
        name = name.substr(0, name.size() - 1);
    }

    auto at = name.find(L'@');
    if (at != std::wstring_view::npos) {
        result.visual_state = Trim(name.substr(at + 1));
        name = name.substr(0, at);
    }

    result.property_name = Trim(name);
    if (result.property_name.empty()) {
        throw ParseError("Bad style syntax, empty name");
    }

    return result;
}

}  // namespace styler
```

- [ ] **Step 5: Rodar e ver passar**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add src/core tests/core
git commit -m "feat(core): parser de regra de estilo"
```

---

### Task 4: Modelo de tema, UTF e carregador de JSON

**Files:**
- Create: `src/core/include/styler/utf.h`, `src/core/utf.cpp`
- Create: `src/core/include/styler/theme.h`
- Create: `src/core/include/styler/theme_loader.h`, `src/core/theme_loader.cpp`
- Create: `tests/core/test_theme_loader.cpp`
- Create: `tests/core/data/valid_theme.json`
- Modify: `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`

**Interfaces:**
- Consumes: `ParseSelector`, `ParseStyleRule`, `ParseError`.
- Produces:
  - `styler::ThemeRule` — `{ std::wstring target; std::vector<ElementMatcher>
    selector; std::vector<StyleRule> styles; }`
  - `styler::OsFeatureVariant` — `{ uint32_t feature_id; std::wstring theme_id; }`
  - `styler::Theme` — `{ std::wstring id, name, author;
    std::map<std::wstring, std::wstring> constants, resource_variables;
    std::vector<ThemeRule> rules;
    std::optional<OsFeatureVariant> os_feature_variant; }`
  - `styler::Theme styler::LoadThemeFromJson(std::string_view utf8)`
  - `styler::Theme styler::LoadThemeFromFile(const std::filesystem::path&)`
  - `std::wstring styler::Utf8ToWide(std::string_view)` /
    `std::string styler::WideToUtf8(std::wstring_view)`

- [ ] **Step 1: Escrever o teste falhando**

`tests/core/data/valid_theme.json`:

```json
{
  "id": "TestTheme",
  "name": "Test Theme",
  "author": "Ninguém",
  "constants": {
    "Bg": "<WindhawkBlur BlurAmount=\"18\" TintColor=\"#25323232\"/>"
  },
  "resourceVariables": {},
  "rules": [
    {
      "target": "Taskbar.TaskbarFrame > Grid#RootGrid",
      "styles": ["Fill:=$Bg", "Visibility=Collapsed"]
    }
  ]
}
```

`tests/core/test_theme_loader.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/theme_loader.h>
#include <styler/utf.h>

using styler::LoadThemeFromJson;
using styler::ParseError;
using styler::ValueRule;

namespace {

constexpr const char* kMinimal = R"({
  "id": "T", "name": "T", "rules": []
})";

}  // namespace

TEST_CASE("round-trips utf-8 with non-ascii") {
    auto wide = styler::Utf8ToWide("Ninguém — ação");
    CHECK(styler::WideToUtf8(wide) == "Ninguém — ação");
}

TEST_CASE("loads a minimal theme") {
    auto theme = LoadThemeFromJson(kMinimal);
    CHECK(theme.id == L"T");
    CHECK(theme.rules.empty());
    CHECK_FALSE(theme.os_feature_variant.has_value());
}

TEST_CASE("loads constants, selectors and styles") {
    auto theme = LoadThemeFromJson(R"({
      "id": "T", "name": "T",
      "constants": { "Bg": "<WindhawkBlur BlurAmount=\"18\"/>" },
      "rules": [
        { "target": "Grid#RootGrid > Rectangle", "styles": ["Fill:=$Bg"] }
      ]
    })");

    REQUIRE(theme.constants.count(L"Bg") == 1);
    REQUIRE(theme.rules.size() == 1);
    CHECK(theme.rules[0].selector.size() == 2);
    CHECK(theme.rules[0].selector[1].type == L"Rectangle");
    REQUIRE(theme.rules[0].styles.size() == 1);
    CHECK(std::get<ValueRule>(theme.rules[0].styles[0]).is_xaml_value);
}

TEST_CASE("loads the os feature variant") {
    auto theme = LoadThemeFromJson(R"({
      "id": "Squircle", "name": "Squircle", "rules": [],
      "osFeatureVariant": { "featureId": 48660958, "themeId": "Squircle_WeatherOnTheRight" }
    })");

    REQUIRE(theme.os_feature_variant.has_value());
    CHECK(theme.os_feature_variant->feature_id == 48660958u);
    CHECK(theme.os_feature_variant->theme_id == L"Squircle_WeatherOnTheRight");
}

TEST_CASE("fails closed on malformed input") {
    CHECK_THROWS_AS(LoadThemeFromJson("{ not json"), ParseError);
    CHECK_THROWS_AS(LoadThemeFromJson(R"({ "name": "T", "rules": [] })"), ParseError);
    CHECK_THROWS_AS(LoadThemeFromJson(R"({ "id": "", "name": "T", "rules": [] })"), ParseError);
    CHECK_THROWS_AS(LoadThemeFromJson(R"({ "id": "T", "name": "T",
      "rules": [ { "target": "#bad", "styles": [] } ] })"), ParseError);
    CHECK_THROWS_AS(LoadThemeFromJson(R"({ "id": "T", "name": "T",
      "rules": [ { "target": "Grid", "styles": ["NoEquals"] } ] })"), ParseError);
}

// O carregador NAO valida resolucao de constante. O upstream
// (ApplyStyleConstants, vendor/upstream/...:17958) substitui `$Nome` por
// prefixo em qualquer posicao do valor e deixa `$` sem correspondencia passar
// como literal. Nos dados reais, 85 referencias sao embutidas no meio do valor
// e 10 nao resolvem contra constante alguma — lancar aqui rejeitaria os temas
// Luminosity_variant_Dock, Luminosity_variant_Compact e Fluid. A resolucao
// pertence ao TAP, no Plano 2.
TEST_CASE("accepts an unresolved constant reference, like upstream does") {
    auto theme = LoadThemeFromJson(R"({
      "id": "T", "name": "T",
      "rules": [ { "target": "Grid", "styles": ["Fill:=$Missing"] } ]
    })");
    REQUIRE(theme.rules.size() == 1);
    CHECK(std::get<ValueRule>(theme.rules[0].styles[0]).value == L"$Missing");
}

TEST_CASE("accepts a constant embedded mid-value") {
    auto theme = LoadThemeFromJson(R"({
      "id": "T", "name": "T",
      "constants": { "Gap": "8" },
      "rules": [ { "target": "Grid", "styles": ["Margin=0,0,$Gap,0"] } ]
    })");
    CHECK(std::get<ValueRule>(theme.rules[0].styles[0]).value == L"0,0,$Gap,0");
}
```

- [ ] **Step 2: Rodar e ver falhar**

```bash
cmake --build build
```

Esperado: FALHA, cabeçalhos inexistentes.

- [ ] **Step 3: Escrever `utf.h` / `utf.cpp`**

`src/core/include/styler/utf.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace styler {

std::wstring Utf8ToWide(std::string_view utf8);
std::string WideToUtf8(std::wstring_view wide);

}  // namespace styler
```

`src/core/utf.cpp` — implementação manual, sem `<codecvt>` (deprecado) e sem
Windows, para manter a restrição de portabilidade do core:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/utf.h>

#include <cstdint>

namespace styler {

std::wstring Utf8ToWide(std::string_view utf8) {
    std::wstring out;
    out.reserve(utf8.size());

    size_t i = 0;
    while (i < utf8.size()) {
        auto b0 = static_cast<unsigned char>(utf8[i]);
        char32_t cp = 0;
        size_t extra = 0;

        if (b0 < 0x80) {
            cp = b0;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1F;
            extra = 1;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0F;
            extra = 2;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07;
            extra = 3;
        } else {
            cp = 0xFFFD;
        }

        if (i + extra >= utf8.size()) {
            cp = 0xFFFD;
            extra = 0;
        }

        for (size_t k = 1; k <= extra; k++) {
            auto bk = static_cast<unsigned char>(utf8[i + k]);
            if ((bk & 0xC0) != 0x80) {
                cp = 0xFFFD;
                extra = k - 1;
                break;
            }
            cp = (cp << 6) | (bk & 0x3F);
        }

        i += extra + 1;

        if (cp > 0xFFFF) {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }

    return out;
}

std::string WideToUtf8(std::wstring_view wide) {
    std::string out;
    out.reserve(wide.size());

    for (size_t i = 0; i < wide.size(); i++) {
        char32_t cp = wide[i];

        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < wide.size() &&
            wide[i + 1] >= 0xDC00 && wide[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (wide[i + 1] - 0xDC00);
            i++;
        }

        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    return out;
}

}  // namespace styler
```

- [ ] **Step 4: Escrever `theme.h` e `theme_loader.h`**

`src/core/include/styler/theme.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <styler/selector.h>
#include <styler/style_rule.h>

namespace styler {

struct ThemeRule {
    std::wstring target;  // The original selector text, kept for diagnostics.
    std::vector<ElementMatcher> selector;
    std::vector<StyleRule> styles;
};

// The one runtime conditional in the upstream mod: a theme that swaps itself
// for a variant when a Windows feature flag is on.
struct OsFeatureVariant {
    std::uint32_t feature_id = 0;
    std::wstring theme_id;
};

struct Theme {
    std::wstring id;
    std::wstring name;
    std::wstring author;
    std::map<std::wstring, std::wstring> constants;
    std::map<std::wstring, std::wstring> resource_variables;
    std::vector<ThemeRule> rules;
    std::optional<OsFeatureVariant> os_feature_variant;
};

}  // namespace styler
```

`src/core/include/styler/theme_loader.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <string_view>

#include <styler/theme.h>

namespace styler {

// Parses a theme and validates what can be validated statically: the JSON
// shape, every selector, and every style rule. Throws ParseError on any of
// those — a half-parsed theme is worse than none, so loading fails closed.
//
// It deliberately does NOT validate `$Constant` references. Upstream resolves
// them by prefix substitution anywhere in a value and lets an unmatched `$`
// through as a literal; three shipped themes rely on that. Resolution happens
// at apply time, not load time.
Theme LoadThemeFromJson(std::string_view utf8);

Theme LoadThemeFromFile(const std::filesystem::path& path);

}  // namespace styler
```

- [ ] **Step 5: Implementar o carregador**

`src/core/theme_loader.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/theme_loader.h>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include <styler/utf.h>

namespace styler {
namespace {

using json = nlohmann::json;

std::wstring RequiredString(const json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        throw ParseError(std::string("Missing or non-string field: ") + key);
    }
    auto value = Utf8ToWide(it->get<std::string>());
    if (value.empty()) {
        throw ParseError(std::string("Empty field: ") + key);
    }
    return value;
}

std::map<std::wstring, std::wstring> OptionalMap(const json& obj,
                                                 const char* key) {
    std::map<std::wstring, std::wstring> out;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return out;
    }
    if (!it->is_object()) {
        throw ParseError(std::string("Field must be an object: ") + key);
    }
    for (auto& [k, v] : it->items()) {
        if (!v.is_string()) {
            throw ParseError(std::string("Non-string value in: ") + key);
        }
        out.emplace(Utf8ToWide(k), Utf8ToWide(v.get<std::string>()));
    }
    return out;
}

}  // namespace

Theme LoadThemeFromJson(std::string_view utf8) {
    json doc = json::parse(utf8, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        throw ParseError("Theme is not a JSON object");
    }

    Theme theme;
    theme.id = RequiredString(doc, "id");
    theme.name = RequiredString(doc, "name");

    if (auto it = doc.find("author"); it != doc.end() && it->is_string()) {
        theme.author = Utf8ToWide(it->get<std::string>());
    }

    theme.constants = OptionalMap(doc, "constants");
    theme.resource_variables = OptionalMap(doc, "resourceVariables");

    auto rules_it = doc.find("rules");
    if (rules_it == doc.end() || !rules_it->is_array()) {
        throw ParseError("Missing or non-array field: rules");
    }

    for (const auto& entry : *rules_it) {
        if (!entry.is_object()) {
            throw ParseError("Rule entry is not an object");
        }

        ThemeRule rule;
        rule.target = RequiredString(entry, "target");
        rule.selector = ParseSelector(rule.target);

        auto styles_it = entry.find("styles");
        if (styles_it == entry.end() || !styles_it->is_array()) {
            throw ParseError("Missing or non-array field: styles");
        }
        for (const auto& s : *styles_it) {
            if (!s.is_string()) {
                throw ParseError("Style entry is not a string");
            }
            rule.styles.push_back(
                ParseStyleRule(Utf8ToWide(s.get<std::string>())));
        }

        theme.rules.push_back(std::move(rule));
    }

    if (auto it = doc.find("osFeatureVariant");
        it != doc.end() && !it->is_null()) {
        if (!it->is_object()) {
            throw ParseError("osFeatureVariant must be an object");
        }
        OsFeatureVariant variant;
        auto fid = it->find("featureId");
        if (fid == it->end() || !fid->is_number_unsigned()) {
            throw ParseError("osFeatureVariant.featureId must be a number");
        }
        variant.feature_id = fid->get<std::uint32_t>();
        variant.theme_id = RequiredString(*it, "themeId");
        theme.os_feature_variant = std::move(variant);
    }

    return theme;
}

Theme LoadThemeFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw ParseError("Cannot open theme file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return LoadThemeFromJson(buffer.str());
}

}  // namespace styler
```

Ligue `nlohmann_json::nlohmann_json` como `PRIVATE` em `styler_core` (já está no
CMake da Task 1).

- [ ] **Step 6: Rodar e ver passar**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
git add src/core tests/core
git commit -m "feat(core): modelo de tema, utf e carregador de json"
```

---

### Task 5: Conversor — tabelas C++ para JSON

Extrai os 55 `const Theme g_themeXxx = {{...}};` do fonte upstream e escreve um
JSON por tema. O mapeamento de nome de struct para id selecionável vem das
comparações `wcscmp(themeName, L"...")` em `ProcessAllStylesFromSettings` — e
essas comparações às vezes estão quebradas em duas linhas, então normalize o
espaço em branco antes de casar.

Duas armadilhas conhecidas, ambas com teste:
- `g_themeOversimplified_Accentuated` tem id selecionável `Oversimplified&Accentuated`.
- `g_themeSquircle_variant_WeatherOnTheRight` **não** tem id selecionável: é
  alvo do `osFeatureVariant` do `Squircle`.

**Files:**
- Create: `tools/extract_themes.py`
- Create: `tools/test_extract_themes.py`
- Modify: `.github/workflows/ci.yml` (rodar os testes Python)

**Interfaces:**
- Consumes: `vendor/upstream/windows-11-taskbar-styler.wh.cpp`.
- Produces:
  - `parse_source(text) -> dict[str, ThemeTable]`, onde `ThemeTable` tem
    `.targets: list[tuple[str, list[str]]]`, `.constants: list[str]`,
    `.resource_variables: list[str]`, `.span: tuple[int, int]` (offsets no
    fonte, usados pelo round-trip da Task 6).
  - `selectable_ids(text) -> dict[str, str]` — nome de struct → id.
  - `to_theme_json(name, table, theme_id, author) -> dict`
  - CLI: `python tools/extract_themes.py convert --source <cpp> --out themes/`

- [ ] **Step 1: Escrever o teste falhando**

`tools/test_extract_themes.py`:

```python
"""Testes do extrator. Rode com: python -m pytest tools/ -q"""
import extract_themes as ex

SAMPLE = r'''
const Theme g_themeSample = {{
    ThemeTargetStyles{L"Grid#RootGrid > Rectangle#Fill", {
        L"Fill:=$Bg",
        L"Visibility=Collapsed"}},
    ThemeTargetStyles{L"Border", {
        L"CornerRadius=14"}},
}, {
    L"Bg=<WindhawkBlur BlurAmount=\"18\" TintColor=\"#25323232\"/>",
}};
'''


def test_parses_targets_and_styles():
    tables = ex.parse_source(SAMPLE)
    t = tables["Sample"]
    assert len(t.targets) == 2
    assert t.targets[0][0] == "Grid#RootGrid > Rectangle#Fill"
    assert t.targets[0][1] == ["Fill:=$Bg", "Visibility=Collapsed"]
    assert t.targets[1][1] == ["CornerRadius=14"]


def test_unescapes_embedded_quotes():
    tables = ex.parse_source(SAMPLE)
    const = tables["Sample"].constants[0]
    assert const == 'Bg=<WindhawkBlur BlurAmount="18" TintColor="#25323232"/>'


def test_splits_constants_into_a_map():
    tables = ex.parse_source(SAMPLE)
    doc = ex.to_theme_json("Sample", tables["Sample"], "Sample", "")
    assert doc["constants"]["Bg"].startswith("<WindhawkBlur")


def test_splits_constants_on_the_first_equals():
    """O valor pode conter '='; o nome, por construcao, nao pode."""
    bad = SAMPLE.replace(r'L"Bg=<Windhawk', r'L"B=g=<Windhawk')
    tables = ex.parse_source(bad)
    doc = ex.to_theme_json("Sample", tables["Sample"], "Sample", "")
    assert "B" in doc["constants"]
    assert doc["constants"]["B"].startswith("g=<Windhawk")


def test_rejects_a_constant_without_equals():
    bad = SAMPLE.replace(r'L"Bg=<Windhawk', r'L"Bg<Windhawk')
    tables = ex.parse_source(bad)
    import pytest
    with pytest.raises(ValueError):
        ex.to_theme_json("Sample", tables["Sample"], "Sample", "")


def test_rejects_a_duplicated_constant():
    dup = SAMPLE.replace(
        'L"Bg=<WindhawkBlur BlurAmount=\\"18\\" TintColor=\\"#25323232\\"/>",',
        'L"Bg=a", L"Bg=b",')
    tables = ex.parse_source(dup)
    import pytest
    with pytest.raises(ValueError):
        ex.to_theme_json("Sample", tables["Sample"], "Sample", "")


def test_sanitizes_the_filename_but_keeps_the_real_id():
    assert ex.safe_filename("Oversimplified&Accentuated") == \
        "Oversimplified_Accentuated"
    assert ex.safe_filename("WinXP_variant_Zune") == "WinXP_variant_Zune"

    tables = ex.parse_source(SAMPLE)
    doc = ex.to_theme_json("Sample", tables["Sample"],
                           "Oversimplified&Accentuated", "")
    assert doc["id"] == "Oversimplified&Accentuated"


def test_finds_selectable_ids_across_line_breaks():
    src = '''
    } else if (wcscmp(themeName,
                      L"OS26_Liquid_Glass_variant_DarkMacDockCompact") == 0) {
        theme = &g_themeOS26_Liquid_Glass_variant_DarkMacDockCompact;
    } else if (wcscmp(themeName, L"Oversimplified&Accentuated") == 0) {
        theme = &g_themeOversimplified_Accentuated;
    '''
    ids = ex.selectable_ids(src)
    assert ids["OS26_Liquid_Glass_variant_DarkMacDockCompact"] == \
        "OS26_Liquid_Glass_variant_DarkMacDockCompact"
    assert ids["Oversimplified_Accentuated"] == "Oversimplified&Accentuated"
```

- [ ] **Step 2: Rodar e ver falhar**

```bash
python -m pytest tools/ -q
```

Esperado: `ModuleNotFoundError: No module named 'extract_themes'`.

Se `pytest` não estiver instalado: `python -m pip install pytest`.

- [ ] **Step 3: Implementar o extrator**

`tools/extract_themes.py`:

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Converte as tabelas de tema C++ do mod upstream em JSON, e de volta.

O modo `emit` existe para o round-trip: se emit(convert(x)) == x byte a byte,
a conversao e comprovadamente sem perda.
"""
from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path

THEME_START = re.compile(r"^const Theme g_theme([A-Za-z0-9_&]+) = \{\{",
                         re.MULTILINE)


@dataclass
class ThemeTable:
    name: str
    targets: list[tuple[str, list[str]]] = field(default_factory=list)
    constants: list[str] = field(default_factory=list)
    resource_variables: list[str] = field(default_factory=list)
    span: tuple[int, int] = (0, 0)


def _read_wide_literals(text: str, start: int, end: int) -> list[str]:
    """Extrai cada L"..." de uma regiao, respeitando escapes."""
    out: list[str] = []
    i = start
    while i < end:
        if text.startswith('L"', i):
            i += 2
            buf: list[str] = []
            while i < end:
                c = text[i]
                if c == "\\":
                    nxt = text[i + 1]
                    buf.append({"n": "\n", "t": "\t", "r": "\r"}.get(nxt, nxt))
                    i += 2
                    continue
                if c == '"':
                    i += 1
                    break
                buf.append(c)
                i += 1
            out.append("".join(buf))
            continue
        i += 1
    return out


def _find_matching(text: str, open_pos: int) -> int:
    """Indice do fecha-chaves que casa com text[open_pos] == '{'."""
    depth = 0
    i = open_pos
    while i < len(text):
        c = text[i]
        if c == '"':
            i += 1
            while i < len(text):
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == '"':
                    break
                i += 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError("chave nao fechada")


def parse_source(text: str) -> dict[str, ThemeTable]:
    tables: dict[str, ThemeTable] = {}

    for m in THEME_START.finditer(text):
        name = m.group(1)
        outer_open = text.index("{", m.end() - 2)
        outer_close = _find_matching(text, outer_open)
        end = text.index(";", outer_close) + 1

        table = ThemeTable(name=name, span=(m.start(), end))

        # Primeiro bloco: os targets.
        targets_open = outer_open + 1
        targets_close = _find_matching(text, targets_open)

        pos = targets_open
        while True:
            idx = text.find("ThemeTargetStyles{", pos)
            if idx == -1 or idx > targets_close:
                break
            brace = idx + len("ThemeTargetStyles")
            close = _find_matching(text, brace)
            literals = _read_wide_literals(text, brace, close)
            table.targets.append((literals[0], literals[1:]))
            pos = close + 1

        # Blocos seguintes: constantes e variaveis de recurso.
        rest = text[targets_close + 1:outer_close]
        blocks: list[list[str]] = []
        pos = 0
        while True:
            open_idx = rest.find("{", pos)
            if open_idx == -1:
                break
            close_idx = _find_matching(rest, open_idx)
            blocks.append(_read_wide_literals(rest, open_idx, close_idx))
            pos = close_idx + 1

        if len(blocks) > 0:
            table.constants = blocks[0]
        if len(blocks) > 1:
            table.resource_variables = blocks[1]

        tables[name] = table

    return tables


def selectable_ids(text: str) -> dict[str, str]:
    """Nome de struct -> id selecionavel.

    As comparacoes as vezes quebram em duas linhas, entao normaliza o espaco
    em branco antes de casar. Para cada `wcscmp(themeName, L"Id")`, o struct
    correspondente e o primeiro `&g_themeXxx;` que aparece depois.
    """
    flat = re.sub(r"\s+", " ", text)

    out: dict[str, str] = {}
    for m in re.finditer(r'wcscmp\(themeName, L"([^"]*)"\)', flat):
        window = flat[m.end():m.end() + 200]
        s = re.search(r"&g_theme([A-Za-z0-9_&]+);", window)
        if s:
            out[s.group(1)] = m.group(1)
    return out


def safe_filename(theme_id: str) -> str:
    """Nome de arquivo seguro. O id verdadeiro vive no campo 'id' do JSON.

    `Oversimplified&Accentuated` e um id real; o '&' e legal no Windows mas
    atrapalha em shell e CI.
    """
    return re.sub(r"[^A-Za-z0-9_.-]", "_", theme_id)


def _split_pairs(entries: list[str]) -> dict[str, str]:
    out: dict[str, str] = {}
    for entry in entries:
        if "=" not in entry:
            raise ValueError(f"constante sem '=': {entry!r}")
        key, value = entry.split("=", 1)
        key = key.strip()
        if not key:
            raise ValueError(f"constante com nome vazio: {entry!r}")
        if key in out:
            raise ValueError(f"constante duplicada: {key!r}")
        out[key] = value
    return out


def to_theme_json(name: str, table: ThemeTable, theme_id: str,
                  author: str) -> dict:
    doc: dict = {
        "id": theme_id,
        "name": theme_id,
        "author": author,
        "constants": _split_pairs(table.constants),
        "resourceVariables": _split_pairs(table.resource_variables),
        "rules": [
            {"target": target, "styles": styles}
            for target, styles in table.targets
        ],
    }
    if theme_id == "Squircle":
        doc["osFeatureVariant"] = {
            "featureId": 48660958,
            "themeId": "Squircle_WeatherOnTheRight",
        }
    return doc


def cmd_convert(args: argparse.Namespace) -> int:
    text = Path(args.source).read_text(encoding="utf-8", errors="replace")
    tables = parse_source(text)
    ids = selectable_ids(text)

    credits: dict[str, str] = {}
    credits_path = Path(args.credits) if args.credits else None
    if credits_path and credits_path.exists():
        credits = json.loads(credits_path.read_text(encoding="utf-8"))

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    written = 0
    for name, table in tables.items():
        theme_id = ids.get(name, name.replace("_variant_", "_"))
        doc = to_theme_json(name, table, theme_id, credits.get(theme_id, ""))
        path = out_dir / f"{safe_filename(theme_id)}.json"
        path.write_text(
            json.dumps(doc, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8")
        written += 1

    print(f"{written} temas escritos em {out_dir}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("convert", help="C++ -> JSON")
    p.add_argument("--source", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--credits", default=None)
    p.set_defaults(func=cmd_convert)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Rodar e ver passar**

```bash
python -m pytest tools/ -q
```

Esperado: 8 passed.

- [ ] **Step 5: Rodar contra o fonte real e conferir a contagem**

```bash
python tools/extract_themes.py convert \
  --source vendor/upstream/windows-11-taskbar-styler.wh.cpp \
  --out /tmp/themes-dry
ls /tmp/themes-dry | wc -l
```

Esperado: `55`. Se vier diferente, o extrator perdeu ou duplicou tema — não
siga.

- [ ] **Step 6: Somar os testes Python ao CI**

Em `.github/workflows/ci.yml`, antes do passo `Configure`:

```yaml
      - uses: actions/setup-python@v5
        with:
          python-version: '3.12'

      - name: Python deps
        run: python -m pip install pytest

      - name: Converter tests
        run: python -m pytest tools/ -q
```

- [ ] **Step 7: Commit**

```bash
git add tools .github
git commit -m "feat(tools): extrator de tabelas de tema C++ para JSON"
```

---

### Task 6: Round-trip — a prova de que a conversão é fiel

O modo `emit` reconstrói o literal C++ a partir do JSON, no formato exato do
original. O teste compara com o trecho do fonte delimitado por `ThemeTable.span`.

**Files:**
- Modify: `tools/extract_themes.py` (somar `emit_theme_table` e o subcomando)
- Modify: `tools/test_extract_themes.py` (somar os testes de round-trip)

**Interfaces:**
- Consumes: `parse_source`, `ThemeTable.span`.
- Produces:
  - `emit_theme_table(name, table) -> str` — o literal C++.
  - CLI: `python tools/extract_themes.py roundtrip --source <cpp>` — sai com
    código 1 e imprime um diff unificado no primeiro tema que divergir.

- [ ] **Step 1: Escrever o teste falhando**

Adicione a `tools/test_extract_themes.py`:

```python
def test_roundtrip_of_the_sample():
    tables = ex.parse_source(SAMPLE)
    table = tables["Sample"]
    start, end = table.span
    assert ex.emit_theme_table("Sample", table) == SAMPLE[start:end]


def test_roundtrip_of_every_real_theme():
    """A prova de verdade: os 55 temas do fonte upstream."""
    from pathlib import Path
    src = Path("vendor/upstream/windows-11-taskbar-styler.wh.cpp")
    if not src.exists():
        import pytest
        pytest.skip("fonte upstream ausente")

    text = src.read_text(encoding="utf-8", errors="replace")
    tables = ex.parse_source(text)
    assert len(tables) == 55

    for name, table in tables.items():
        start, end = table.span
        assert ex.emit_theme_table(name, table) == text[start:end], name
```

- [ ] **Step 2: Rodar e ver falhar**

```bash
python -m pytest tools/ -q
```

Esperado: `AttributeError: module 'extract_themes' has no attribute
'emit_theme_table'`.

- [ ] **Step 3: Implementar o emissor**

Some a `tools/extract_themes.py`:

```python
def _escape_wide(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def emit_theme_table(name: str, table: ThemeTable) -> str:
    lines = [f"const Theme g_theme{name} = {{{{"]

    for target, styles in table.targets:
        lines.append(f'    ThemeTargetStyles{{L"{_escape_wide(target)}", {{')
        body = [f'        L"{_escape_wide(s)}"' for s in styles]
        lines.append(",\n".join(body) + "}},")

    lines.append("}, {")
    for c in table.constants:
        lines.append(f'    L"{_escape_wide(c)}",')

    if table.resource_variables:
        lines.append("}, {")
        for v in table.resource_variables:
            lines.append(f'    L"{_escape_wide(v)}",')

    lines.append("}};")
    return "\n".join(lines)


def cmd_roundtrip(args: argparse.Namespace) -> int:
    import difflib

    text = Path(args.source).read_text(encoding="utf-8", errors="replace")
    tables = parse_source(text)

    failures = 0
    for name, table in tables.items():
        start, end = table.span
        original = text[start:end]
        emitted = emit_theme_table(name, table)
        if emitted != original:
            failures += 1
            print(f"--- DIVERGENCIA em g_theme{name} ---")
            diff = difflib.unified_diff(
                original.splitlines(), emitted.splitlines(),
                fromfile="original", tofile="emitido", lineterm="")
            for line in list(diff)[:40]:
                print(line)
            if failures >= 3:
                print("... parando apos 3 divergencias")
                break

    if failures:
        print(f"\nFALHA: {failures} tema(s) divergiram")
        return 1

    print(f"OK: {len(tables)} temas reconstruidos byte a byte")
    return 0
```

E registre o subcomando em `main()`:

```python
    p = sub.add_parser("roundtrip", help="prova que a conversao e sem perda")
    p.add_argument("--source", required=True)
    p.set_defaults(func=cmd_roundtrip)
```

- [ ] **Step 4: Rodar e iterar até bater byte a byte**

```bash
python tools/extract_themes.py roundtrip \
  --source vendor/upstream/windows-11-taskbar-styler.wh.cpp
```

Esperado ao final: `OK: 55 temas reconstruidos byte a byte`.

Este passo **vai** falhar nas primeiras rodadas — o formatador do upstream tem
detalhes (indentação de listas de estilo, vírgula final, o segundo bloco
ausente quando não há `resourceVariables`). Ajuste `emit_theme_table` até o diff
zerar. **Não relaxe o teste para fazê-lo passar**: um `emit` frouxo destrói a
única garantia real de fidelidade que o projeto tem.

- [ ] **Step 5: Somar o round-trip ao CI**

Em `.github/workflows/ci.yml`, depois de `Converter tests`:

```yaml
      - name: Round-trip da conversao de temas
        run: >
          python tools/extract_themes.py roundtrip
          --source vendor/upstream/windows-11-taskbar-styler.wh.cpp
```

- [ ] **Step 6: Commit**

```bash
git add tools .github
git commit -m "test(tools): round-trip provando conversao de temas sem perda"
```

---

### Task 7: Créditos, geração dos temas e teste de corpus

Fecha o ciclo: busca os autores, gera os 55 JSONs de verdade, e prova que
`styler_core` parseia todos eles.

**Files:**
- Create: `tools/fetch_theme_credits.py`
- Create: `themes/*.json` (55 arquivos, gerados)
- Create: `themes/credits.json` (gerado)
- Create: `THEMES.md` (gerado)
- Create: `tests/core/test_corpus.cpp`
- Modify: `tests/core/CMakeLists.txt`, `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `LoadThemeFromFile`, `selectable_ids`.
- Produces: `themes/credits.json` — mapa `theme_id -> "Autor"`; `THEMES.md`.
  A macro CMake `STYLER_THEMES_DIR` é definida para o teste achar a pasta.

- [ ] **Step 1: Escrever o buscador de créditos**

`tools/fetch_theme_credits.py`:

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Busca o autor de cada tema no repositorio upstream do guia de estilos.

Roda uma vez; o resultado e commitado. O app nunca acessa a rede.
"""
from __future__ import annotations

import json
import re
import sys
import urllib.request
from pathlib import Path

BASE = ("https://raw.githubusercontent.com/ramensoftware/"
        "windows-11-taskbar-styling-guide/main/Themes/{}/README.md")

AUTHOR = re.compile(r"\*\*Author\*\*:\s*\[([^\]]+)\]")


def fetch(theme_id: str) -> str:
    # O guia usa o nome base, sem o sufixo de variante.
    base_name = theme_id.split("_variant_")[0]
    url = BASE.format(base_name)
    try:
        with urllib.request.urlopen(url, timeout=20) as r:
            if r.status != 200:
                return ""
            body = r.read().decode("utf-8", errors="replace")
    except Exception as exc:  # noqa: BLE001
        print(f"  aviso: {theme_id}: {exc}", file=sys.stderr)
        return ""

    m = AUTHOR.search(body)
    return m.group(1) if m else ""


def main() -> int:
    themes_dir = Path("themes")
    # O nome do arquivo e sanitizado; o id verdadeiro vive dentro do JSON.
    ids = sorted(
        json.loads(p.read_text(encoding="utf-8"))["id"]
        for p in themes_dir.glob("*.json")
        if p.name != "credits.json"
    )

    credits: dict[str, str] = {}
    for theme_id in ids:
        author = fetch(theme_id)
        credits[theme_id] = author
        print(f"{theme_id}: {author or '(nao encontrado)'}")

    (themes_dir / "credits.json").write_text(
        json.dumps(credits, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")

    lines = [
        "# Temas",
        "",
        "Todos os temas vieram do mod "
        "[windows-11-taskbar-styler](https://github.com/ramensoftware/windhawk-mods)"
        " e do "
        "[guia de estilos](https://github.com/ramensoftware/windows-11-taskbar-styling-guide),"
        " sob GPL-3.0. Credito de cada autor abaixo.",
        "",
        "| Tema | Autor |",
        "|---|---|",
    ]
    for theme_id, author in credits.items():
        base = theme_id.split("_variant_")[0]
        link = ("https://github.com/ramensoftware/"
                f"windows-11-taskbar-styling-guide/blob/main/Themes/{base}/README.md")
        lines.append(f"| [{theme_id}]({link}) | {author or '—'} |")

    Path("THEMES.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"\n{len(credits)} temas, THEMES.md escrito")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Gerar temas, créditos e regenerar com autor**

```bash
python tools/extract_themes.py convert \
  --source vendor/upstream/windows-11-taskbar-styler.wh.cpp --out themes/
python tools/fetch_theme_credits.py
python tools/extract_themes.py convert \
  --source vendor/upstream/windows-11-taskbar-styler.wh.cpp \
  --out themes/ --credits themes/credits.json
ls themes/*.json | wc -l
```

Esperado: `56` (55 temas + `credits.json`).

- [ ] **Step 3: Escrever o teste de corpus**

`tests/core/test_corpus.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include <styler/theme_loader.h>

#ifndef STYLER_THEMES_DIR
#error "STYLER_THEMES_DIR must be defined by CMake"
#endif

TEST_CASE("every shipped theme parses") {
    namespace fs = std::filesystem;

    int themes = 0;
    int rules = 0;

    for (const auto& entry : fs::directory_iterator(STYLER_THEMES_DIR)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        if (entry.path().filename() == "credits.json") {
            continue;
        }

        CAPTURE(entry.path().string());
        auto theme = styler::LoadThemeFromFile(entry.path());

        CHECK_FALSE(theme.id.empty());
        CHECK_FALSE(theme.rules.empty());

        themes++;
        rules += static_cast<int>(theme.rules.size());
    }

    CHECK(themes == 55);
    // Guarda contra o extrator perder alvos silenciosamente.
    CHECK(rules == 2396);
}
```

- [ ] **Step 4: Passar o caminho dos temas pelo CMake**

Em `tests/core/CMakeLists.txt`, some `test_corpus.cpp` às fontes e:

```cmake
target_compile_definitions(styler_core_tests PRIVATE
  STYLER_THEMES_DIR="${CMAKE_SOURCE_DIR}/themes"
)
```

- [ ] **Step 5: Rodar**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Esperado: passa, com `themes == 55` e `rules == 2396`.

Se a contagem de regras divergir, confira contra o fonte:

```bash
grep -c 'ThemeTargetStyles{' vendor/upstream/windows-11-taskbar-styler.wh.cpp
```

- [ ] **Step 6: Commit**

```bash
git add themes THEMES.md tools tests/core
git commit -m "feat(themes): 55 temas convertidos, creditados e cobertos por teste"
```

---

### Task 8: Licença, README e fechamento do Plano 1

**Files:**
- Create: `LICENSE` (GPL-3.0)
- Create: `NOTICE`
- Create: `README.md`

**Interfaces:**
- Consumes: nada.
- Produces: nada em código.

- [ ] **Step 1: Baixar o texto da GPL-3.0**

```bash
curl -sSL -o LICENSE https://www.gnu.org/licenses/gpl-3.0.txt
head -3 LICENSE
```

- [ ] **Step 2: Escrever o NOTICE**

`NOTICE`:

```
taskbar-styler
Copyright (C) 2026 Neves

Este programa e uma obra derivada do mod "Windows 11 Taskbar Styler"
(windows-11-taskbar-styler), versao 1.9, de m417z, distribuido sob a
GNU General Public License v3.0.

  Fonte original: https://github.com/ramensoftware/windhawk-mods
  Autor: m417z <https://m417z.com/>

Os temas incluidos foram contribuidos por diversos autores ao repositorio
windows-11-taskbar-styling-guide, tambem sob GPL-3.0. O credito individual
de cada um esta em THEMES.md.

Este programa e software livre: voce pode redistribui-lo e/ou modifica-lo
sob os termos da GNU General Public License, versao 3, conforme publicada
pela Free Software Foundation. Veja LICENSE para o texto completo.
```

- [ ] **Step 3: Escrever o README**

`README.md`:

````markdown
# taskbar-styler

Customiza a taskbar do Windows 11 sem depender do Windhawk.

Obra derivada do mod [windows-11-taskbar-styler](https://github.com/ramensoftware/windhawk-mods)
(m417z, GPL-3.0), reescrita como aplicativo independente.

## Como funciona

O Windows carrega uma DLL COM dentro do `explorer.exe` por meio da API de
diagnóstico do XAML (`InitializeXamlDiagnosticsEx`) — o mesmo mecanismo do Live
Visual Tree do Visual Studio. Essa DLL recebe a árvore visual da taskbar e
aplica os estilos do tema escolhido.

**Não há injeção de DLL nem patch de código.** Nada de `CreateRemoteThread`,
`WriteProcessMemory` ou hooks inline: apenas APIs sancionadas do Windows. E não
há nenhum acesso à rede em runtime.

## Estado

Em desenvolvimento. O que já existe:

- [x] `styler_core` — parsing de seletores, regras de estilo e temas
- [x] 55 temas convertidos para JSON, com conversão provada sem perda
- [ ] A DLL que aplica os estilos (Plano 2)
- [ ] O aplicativo de bandeja (Plano 3)

## Compilando

Requer Visual Studio 2026 com o toolchain C++ e o Windows SDK.

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Temas

Ficam em [`themes/`](themes/), um arquivo JSON por tema, editáveis sem
recompilar. Os créditos estão em [THEMES.md](THEMES.md).

## Licença

GPL-3.0. Veja [LICENSE](LICENSE) e [NOTICE](NOTICE).
````

- [ ] **Step 4: Commit e push**

```bash
git add LICENSE NOTICE README.md
git commit -m "docs: licenca GPL-3.0, notice de atribuicao e README"
git push
gh run watch
```

Esperado: CI verde. Plano 1 completo.

---

## Definição de pronto do Plano 1

- `ctest` verde: seletor, regra de estilo, carregador, corpus.
- `python -m pytest tools/ -q` verde.
- `extract_themes.py roundtrip` reporta 55 temas byte a byte.
- 55 JSONs em `themes/`, com autores em `THEMES.md`.
- CI verde no GitHub.
- `LICENSE` e `NOTICE` presentes.

Nada disso estiliza a taskbar ainda — isso é o Plano 2. O que está pronto é a
parte que, se estivesse errada, contaminaria tudo depois.
