# Test organization

- `arrays/`, `audio/`, `control_flow/`, `errors/`, `file_io/`, `graphics/`,
  `math/`, `procedures/`, `strings/`, and `user_types/` contain BASIC regression
  programs and their `.expected` outputs.
- `system/` contains command-line and startup tests.
- `manual/` contains exploratory programs that are not part of the automated
  suite.
- `scripts/` contains test helpers, including the performance regression guard.
- Shared binary fixtures and generated test output remain in this directory
  because BASIC programs and documentation refer to paths under `tests/`.

Run the complete suite from the repository root with `make test`, or run a
single BASIC case with `./basika tests/<category>/<case>.bas`.
