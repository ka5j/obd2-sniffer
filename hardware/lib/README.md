# Shared KiCad libraries

Part of the [project documentation](../../docs/README.md). The folder's guide is [here](../README.md).

Symbols and footprints made for this project, or copied into it, go here, so a fresh clone opens with no missing parts.
KiCad's own built-in libraries don't need copying. Keeping `lib/` beside the board folders lets each revision share it.

Add a library in KiCad's symbol and footprint library managers, as a **project** library, with a path relative to the
project:

```
${KIPRJMOD}/../lib/<library name>
```

KiCad then records it in the project's `sym-lib-table` and `fp-lib-table`, which are committed. Check any part taken from
outside KiCad against its datasheet, and note where it came from.
