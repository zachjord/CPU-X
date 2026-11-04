# Processor package database

`data/processors/packages.json` is the source of truth. Generate the checked-in
C++ table with `python3 scripts/generate_package_database.py`; verify it with
`--check`. Normal builds need neither Python nor network access. This deliberately
starts with build-time data rather than a runtime parser/updater: packaging stays
simple and a missing external database cannot break identification.

The 125 inherited rules retain their upstream provenance and are **not** marked
manufacturer-verified. The initial new manufacturer record covers AMD Ryzen 9
7945HX, Dragon Range, FL1, from AMD's product specification retrieved 2026-11-04.
Only factual fields are transcribed; the manufacturer's page is not redistributed.
Do not infer a license for bulk redistribution from this single factual record.

## Matching

The matcher has no GTK, NCurses, libcpuid or hardware dependency. It ranks exact
model matches above model-qualified legacy rules above codename-only rules.
Within a legacy tier, longer predicates take precedence. Matching is ASCII
case-insensitive, whitespace-normalized and bounded at alphanumeric edges.
Equal-ranking matches for different sockets return an ambiguous/unknown result,
not an array-order winner. The original broad family rules remain inherited
heuristics; migration does not certify them as universally accurate.

Exact-model normalization currently accepts only the documented optional
` with Radeon Graphics` suffix. HX3D and engineering-sample suffixes are not
silently removed. Add explicit records/aliases and tests for new spellings.

The 7945HX record corrects the displayed codename after libcpuid identification.
The package matcher is still invoked only when the existing SMBIOS package
validation requests fallback. Plausible nonempty firmware package values retain
their existing precedence; firmware conflict reconciliation is not implemented.
Source IDs are retained in generated entries and logged for package fallback.
Raw CPUID fields are unchanged. No GUI model or daemon protocol changes occur.

## Verification

```
python3 scripts/generate_package_database.py --check
python3 -m unittest discover -s tests -p 'test_package_database.py' -v
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/package_match.cpp -o /tmp/package-match
/tmp/package-match
```

CMake also registers the matcher and, when Python is available, generator tests.
CI requires both. Tests are synthetic brand/codename fixtures, not a captured
7945HX CPUID dump; a real serialized libcpuid sample remains necessary to check
end-to-end identification on that machine.

## Next automation work

Generation, validation and drift checks are automated. Processor discovery and
manufacturer ingestion are **not yet implemented**. Evaluate stable data exports,
access terms and licensing before adding importers. Imported changes should
preserve source snapshots and produce reviewable diffs; conflicting facts must
not overwrite stronger evidence. Do not schedule a job that merely refreshes a
verification date without fetching and checking the underlying facts.
