# Vendored third-party dependencies

All dependencies are vendored (no vcpkg/submodule fetch needed at build time).

| Directory | Library | Version | Upstream | License |
| --- | --- | --- | --- | --- |
| `nlohmann/` | nlohmann/json (single header `json.hpp`) | 3.11.3 | https://github.com/nlohmann/json/releases/tag/v3.11.3 | MIT (`LICENSE.MIT`) |
| `catch2/` | Catch2 v3 amalgamated (`catch_amalgamated.hpp/.cpp`) | 3.7.1 | https://github.com/catchorg/Catch2/releases/tag/v3.7.1 | BSL-1.0 (`LICENSE.txt`) |

## Verification

```bash
# nlohmann/json 3.11.3
shasum -a 256 nlohmann/json.hpp
# catch2 3.7.1
shasum -a 256 catch2/catch_amalgamated.hpp catch2/catch_amalgamated.cpp
```

Re-download commands (if updating):

```bash
curl -L -o nlohmann/json.hpp \
  https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
curl -L -o catch2/catch_amalgamated.hpp \
  https://github.com/catchorg/Catch2/releases/download/v3.7.1/catch_amalgamated.hpp
curl -L -o catch2/catch_amalgamated.cpp \
  https://github.com/catchorg/Catch2/releases/download/v3.7.1/catch_amalgamated.cpp
```

No other third-party code is linked into the Core library. The App layer's
WinUI 3 / Windows App SDK dependency is restored through NuGet at build time
(see `../App/TransFrame.App.vcxproj`).
