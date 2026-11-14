# sqlite3 amalgamation (vendored)

- Source: https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip (SQLite 3.45.1, 2024-01-30)
- Files: `sqlite3.c` (amalgamated library), `sqlite3.h` (header). `shell.c` / `sqlite3ext.h` not needed.
- License: Public Domain (https://www.sqlite.org/copyright.html).
- Used only by `HistoryStore` (PRD FR-30: `history.db`, WAL, LIKE search — no FTS5, PRD decision 2).

Verify the vendored copy matches upstream:

```
shasum -a 256 sqlite3.c sqlite3.h
sqlite3.c  1a206854aa9fe0ccc1b609f5cfce67eb52ac0a8f5aa2f5e853f7c3ed84c710ab
sqlite3.h  41e066ccd4f89e938f136ceb48c996c54dc381b59fe566dea48864c3750b779e
```

Build notes: compiled as C (not C++). MSVC: per-file `WarningLevel` lowered —
the amalgamation is not /W4-clean and the Core project is /WX. CMake/Clang:
`-x c` + `-w` (see windows/README.md build command).
