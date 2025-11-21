# Scripts Directory

Helper scripts for building, testing, and managing the YubiKey OATH KRunner plugin.

## Container Testing

### `test-in-container.sh`

Host-side wrapper for running tests in Docker containers.

**Usage:**
```bash
./scripts/test-in-container.sh test          # Run all tests
./scripts/test-in-container.sh coverage      # Run with coverage
./scripts/test-in-container.sh shell         # Interactive debug shell
./scripts/test-in-container.sh clean         # Clean up containers
```

**See:** [CONTAINERIZED_TESTING.md](../CONTAINERIZED_TESTING.md) for full documentation.

### `run-tests-container.sh`

In-container test runner (executed inside Docker container).

**Features:**
- Sets up isolated XDG directories
- Starts private D-Bus session
- Configures Qt/KDE environment
- Builds and runs tests
- Generates coverage reports

**Not meant to be run directly** - use `test-in-container.sh` instead.

## Icon Generation

### `generate-icon-sizes.sh`

Generates multiple icon sizes from source 1000px PNGs.

**Usage:**
```bash
./scripts/generate-icon-sizes.sh
```

**Requires:** ImageMagick 7+, optipng (optional)

**Output:** 7 sizes (16×16 to 256×256) in `resources/icons/devices/generated/`

## Other Scripts

Additional scripts may be added here for:
- Release automation
- Deployment helpers
- Development utilities
- CI/CD integration

---

For project documentation, see:
- [CLAUDE.md](../CLAUDE.md) - Project architecture
- [TEST_IMPLEMENTATION.md](../TEST_IMPLEMENTATION.md) - Test strategy
- [CONTAINERIZED_TESTING.md](../CONTAINERIZED_TESTING.md) - Container testing guide
