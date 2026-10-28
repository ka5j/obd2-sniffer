## What

## Why

## How tested
- [ ] Firmware builds clean (`cmake --workflow --preset firmware`)
- [ ] C unit tests pass under ASan/UBSan and TSan (`host-tests`, `host-tests-tsan` presets)
- [ ] 100% MC/DC coverage on `lib/` (`scripts/coverage.py`)
- [ ] MISRA C:2012: 0 open findings (`scripts/misra.py`)
- [ ] Laptop tool tests pass at 100% coverage, ruff clean (`cd host && python -m pytest --cov`)
- [ ] Formatting passes (`./scripts/check-format.sh`)
- [ ] Verified on hardware (describe)
