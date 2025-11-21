# Containerized Testing Guide

This document describes how to run tests in isolated containers (Docker or Podman) to prevent interference with the host system.

## Overview

The containerized test environment provides:

- **Full isolation** - No interference with host system's D-Bus, KWallet, or SQLite databases
- **Reproducible builds** - Consistent environment across different machines
- **Clean state** - Fresh environment for each test run
- **Coverage reports** - Integrated code coverage analysis
- **Parallel execution** - Multiple test configurations in parallel

## Quick Start

### Prerequisites

**Option 1: Podman (RECOMMENDED)**
- Podman (version 4.0+)
- podman-compose (version 1.0+)
- **Benefits:** Rootless by default, no daemon, better security, OCI compliant

**Option 2: Docker (alternative)**
- Docker (version 20.10+)
- Docker Compose (version 1.29+)
- **Note:** Requires daemon, typically needs root/group membership

**Common Requirements:**
- At least 4GB RAM available for containers
- 10GB free disk space

**Installation:**
```bash
# Arch Linux - Podman (RECOMMENDED - rootless, no daemon)
sudo pacman -S podman podman-compose

# Fedora/RHEL - Podman is pre-installed
sudo dnf install podman-compose

# Arch Linux - Docker (if Podman not available)
sudo pacman -S docker docker-compose
sudo systemctl enable --now docker
sudo usermod -aG docker $USER  # Logout/login required
```

**Auto-detection:** Scripts automatically detect and prefer Podman over Docker when both are available.

### Running Tests

**Basic test run:**
```bash
./scripts/test-in-container.sh test
```

**With coverage report:**
```bash
./scripts/test-in-container.sh coverage
```

**With clean build:**
```bash
./scripts/test-in-container.sh test --clean
```

**Interactive shell for debugging:**
```bash
./scripts/test-in-container.sh shell
```

## Podman vs Docker

Both container runtimes are fully supported. **Podman is preferred and recommended** for security and simplicity.

### Why Podman? (RECOMMENDED)

- **Rootless by default** - Containers run without root privileges, enhanced security
- **No daemon** - Containers run as child processes, simpler architecture
- **Drop-in replacement** - Compatible with Docker commands (`alias docker=podman`)
- **Systemd integration** - Native systemd support in containers
- **OCI compliant** - Follows open container initiative standards
- **Better resource isolation** - User namespaces by default
- **No single point of failure** - No daemon to crash or hang

### Docker (alternative)

- **Mature ecosystem** - More third-party documentation and examples
- **BuildKit** - Advanced build features (though Podman supports BuildKit too)
- **Wider CI/CD support** - More integrations available in commercial CI systems
- **Note:** Requires daemon, typically needs root or docker group membership

**Recommendation:** Use Podman for all testing. It's safer, simpler, and the scripts prefer it automatically. Only use Docker if you specifically need Docker-only features or are on Windows/macOS where Podman support is limited.

## Architecture

### Components

1. **Dockerfile.test** - Container image definition with all dependencies
   - Based on Arch Linux (matches AUR package environment)
   - Includes Qt 6.7+, KDE Frameworks 6.0+, PC/SC Lite, etc.
   - Creates isolated test user (not root)

2. **scripts/run-tests-container.sh** - In-container test runner
   - Sets up isolated XDG directories
   - Starts private D-Bus session
   - Configures Qt/KDE environment
   - Builds and runs tests
   - Generates coverage reports

3. **docker-compose.test.yml** - Service definitions
   - `tests` - Standard test run (Debug build)
   - `tests-coverage` - Tests with coverage analysis
   - `tests-release` - Performance testing (Release build)
   - `shell` - Interactive debugging shell

4. **scripts/test-in-container.sh** - Host-side wrapper
   - Convenient CLI for container operations
   - Manages Docker Compose services
   - Extracts coverage reports

### Environment Isolation

The test environment isolates the following:

#### XDG Directories
```bash
XDG_DATA_HOME=/tmp/test-home/.local/share    # SQLite databases, KWallet
XDG_CONFIG_HOME=/tmp/test-home/.config       # Configuration files
XDG_CACHE_HOME=/tmp/test-home/.cache         # Cache data
XDG_RUNTIME_DIR=/tmp/test-home/.runtime      # D-Bus socket, runtime data
XDG_STATE_HOME=/tmp/test-home/.local/state   # State data
```

#### D-Bus Session
- Private session bus at `${XDG_RUNTIME_DIR}/dbus-session`
- No connection to host D-Bus
- Clean daemon registration without conflicts

#### KWallet
- Test mode enabled (`KWALLETD_TESTMODE=1`)
- Isolated wallet storage in test XDG directories
- No password prompts during tests

#### Qt Platform
- Offscreen platform (`QT_QPA_PLATFORM=offscreen`)
- No X11 server required
- No GUI windows displayed

## Usage Examples

### 1. Standard Test Run

Run all tests in Debug mode:

```bash
./scripts/test-in-container.sh test
```

Output shows:
- Environment setup
- Build progress
- Test results
- Summary

### 2. Coverage Analysis

Generate code coverage report:

```bash
./scripts/test-in-container.sh coverage
```

Coverage report is automatically copied to `./coverage-report/coverage_html/index.html`.

Open with:
```bash
xdg-open ./coverage-report/coverage_html/index.html
```

### 3. Clean Build

Force rebuild from scratch:

```bash
./scripts/test-in-container.sh test --clean
```

Or set environment variable:
```bash
CLEAN_BUILD=true ./scripts/test-in-container.sh test
```

### 4. Preserve Test Data

Keep test data after run for inspection:

```bash
./scripts/test-in-container.sh test --preserve
```

Test data location will be printed in output.

### 5. Interactive Debugging

Open shell inside test container:

```bash
./scripts/test-in-container.sh shell
```

Inside the shell:
```bash
# Run individual test
cd build-container-test
ctest -R test_result --verbose --output-on-failure

# Check D-Bus status
dbus-monitor --session &
./tests/test_e2e_device_lifecycle

# Inspect test data
ls -la /tmp/test-home/.local/share/krunner-yubikey/
sqlite3 /tmp/test-home/.local/share/krunner-yubikey/devices.db
```

### 6. Release Build Testing

Test with optimizations enabled:

```bash
./scripts/test-in-container.sh release
```

Useful for:
- Performance benchmarking
- Release candidate validation
- Optimization verification

## Docker Compose Services

### Direct Docker Compose Usage

You can also use Docker Compose directly:

**Run tests:**
```bash
docker-compose -f docker-compose.test.yml run --rm tests
```

**Run with coverage:**
```bash
docker-compose -f docker-compose.test.yml run --rm tests-coverage
```

**Open shell:**
```bash
docker-compose -f docker-compose.test.yml run --rm shell
```

**Clean up:**
```bash
docker-compose -f docker-compose.test.yml down -v
```

### Service Configuration

Each service can be customized via environment variables:

```bash
# Clean build
CLEAN_BUILD=true docker-compose -f docker-compose.test.yml run --rm tests

# Preserve test data
PRESERVE_TEST_DATA=true docker-compose -f docker-compose.test.yml run --rm tests

# Custom build directory
BUILD_DIR=build-custom docker-compose -f docker-compose.test.yml run --rm tests
```

## Maintenance

### Updating Base Image

Pull latest Arch Linux base:

```bash
./scripts/test-in-container.sh build --pull
```

Or manually:
```bash
docker pull archlinux:latest
docker-compose -f docker-compose.test.yml build --pull tests
```

### Cleaning Up

Remove all containers, volumes, and images:

```bash
./scripts/test-in-container.sh clean
```

Or manually:
```bash
docker-compose -f docker-compose.test.yml down -v
docker volume rm yubikey-oath-build yubikey-oath-coverage yubikey-oath-release
docker image rm yubikey-oath-krunner-test:latest
```

### Disk Space Management

Check Docker disk usage:
```bash
docker system df
```

Clean up unused data:
```bash
docker system prune -a --volumes
```

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Build test image
        run: docker-compose -f docker-compose.test.yml build tests

      - name: Run tests
        run: docker-compose -f docker-compose.test.yml run --rm tests

  coverage:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Build test image
        run: docker-compose -f docker-compose.test.yml build tests-coverage

      - name: Run tests with coverage
        run: docker-compose -f docker-compose.test.yml run --rm tests-coverage

      - name: Extract coverage report
        run: |
          CONTAINER_ID=$(docker ps -a -q -f name=yubikey-oath-tests-coverage | head -1)
          docker cp "${CONTAINER_ID}:/home/testuser/yubikey-oath-krunner/build-container-coverage/coverage_html" ./coverage-report/

      - name: Upload coverage to Codecov
        uses: codecov/codecov-action@v3
        with:
          files: ./coverage-report/coverage.info
```

### GitLab CI Example

```yaml
test:
  image: docker:latest
  services:
    - docker:dind
  before_script:
    - apk add --no-cache docker-compose
  script:
    - docker-compose -f docker-compose.test.yml build tests
    - docker-compose -f docker-compose.test.yml run --rm tests
  after_script:
    - docker-compose -f docker-compose.test.yml down -v

coverage:
  image: docker:latest
  services:
    - docker:dind
  before_script:
    - apk add --no-cache docker-compose
  script:
    - docker-compose -f docker-compose.test.yml build tests-coverage
    - docker-compose -f docker-compose.test.yml run --rm tests-coverage
  artifacts:
    paths:
      - coverage-report/
    expire_in: 30 days
  coverage: '/lines\.*:\s+(\d+\.\d+)%/'
```

## Troubleshooting

### Container Runtime Detection

**Problem:** Script can't find Docker or Podman

**Solution:** Verify installation
```bash
# Check Docker
docker --version
docker ps

# Check Podman
podman --version
podman ps

# Check Compose
docker-compose --version  # or
podman-compose --version
```

### Build Failures

**Problem:** Image build fails with package errors

**Solution:** Update package databases and retry
```bash
# Docker
docker pull archlinux:latest
./scripts/test-in-container.sh build --pull

# Podman
podman pull archlinux:latest
./scripts/test-in-container.sh build --pull
```

### Test Failures

**Problem:** Tests pass on host but fail in container

**Solution:** Open interactive shell and debug
```bash
./scripts/test-in-container.sh shell

# Inside container:
cd build-container-test
ctest -R failing_test --verbose --output-on-failure
```

### D-Bus Errors

**Problem:** `DBUS_SESSION_BUS_ADDRESS not set` or connection errors

**Solution:** Verify D-Bus is running in container
```bash
# The run-tests-container.sh script automatically starts D-Bus
# If manually debugging, ensure D-Bus is started:
dbus-daemon --session --nofork --print-address &
export DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/test-home/.runtime/dbus-session
```

### Permission Errors

**Problem:** Permission denied when accessing files

**Solution:** Ensure container runs as test user, not root
```bash
# In Dockerfile.test, verify:
USER testuser

# Check user inside container:
./scripts/test-in-container.sh shell
whoami  # Should print: testuser
```

### Out of Memory

**Problem:** Container killed due to OOM

**Solution:** Increase memory limit in docker-compose.test.yml
```yaml
services:
  tests:
    mem_limit: 8g  # Increase from 4g
    memswap_limit: 8g
```

### Slow Build Times

**Problem:** Fresh builds take too long

**Solution:** Use cached volumes
```bash
# Docker Compose automatically uses named volumes for build artifacts
# These persist across runs, speeding up rebuilds

# To verify volumes (Docker):
docker volume ls | grep yubikey-oath

# To verify volumes (Podman):
podman volume ls | grep yubikey-oath

# To clean and start fresh:
./scripts/test-in-container.sh clean
```

### Podman-Specific Issues

**Problem:** Permission denied in rootless Podman

**Solution:** Check user namespace configuration
```bash
# Verify subuid/subgid mappings
cat /etc/subuid
cat /etc/subgid

# Should show entries like:
# username:100000:65536

# If missing, add them:
sudo usermod --add-subuids 100000-165535 --add-subgids 100000-165535 $USER

# Restart user session
```

**Problem:** Podman compose not found

**Solution:** Install podman-compose
```bash
# Arch Linux
sudo pacman -S podman-compose

# Fedora/RHEL
sudo dnf install podman-compose

# Alternative: Python pip
pip install --user podman-compose
```

**Problem:** Slow Podman performance

**Solution:** Enable fuse-overlayfs storage driver
```bash
# Edit ~/.config/containers/storage.conf
mkdir -p ~/.config/containers
cat > ~/.config/containers/storage.conf << EOF
[storage]
driver = "overlay"

[storage.options.overlay]
mount_program = "/usr/bin/fuse-overlayfs"
EOF
```

## Performance Considerations

### Build Cache

The Docker layer cache significantly speeds up image builds:

- Base packages (Qt, KDE Frameworks) are cached in early layers
- Only source code changes trigger recompilation
- Named volumes preserve build artifacts between runs

### Parallel Testing

Tests run in parallel by default:

```bash
# In run-tests-container.sh:
ctest --parallel $(nproc)
```

Disable for debugging:
```bash
# Edit run-tests-container.sh or run manually:
cd build-container-test
ctest --output-on-failure  # Sequential execution
```

### Resource Limits

Default limits in docker-compose.test.yml:

- Memory: 4GB
- CPUs: 4 cores

Adjust based on your system:

```yaml
services:
  tests:
    mem_limit: 8g
    cpus: 8
```

## Comparison: Container vs Host Testing

| Aspect | Host Testing | Container Testing (Docker/Podman) |
|--------|-------------|-----------------------------------|
| Isolation | ❌ Shares system D-Bus/KWallet | ✅ Fully isolated |
| Reproducibility | ⚠️ Depends on host system | ✅ Consistent environment |
| Setup | ✅ Fast (no container build) | ⚠️ Initial build required |
| Debugging | ✅ Direct access to processes | ⚠️ Need shell access |
| CI/CD | ⚠️ Requires system setup | ✅ Portable, easy integration |
| Safety | ❌ Can affect production daemon | ✅ Cannot interfere with host |
| Rootless | N/A | ✅ Yes (Podman default, Docker optional) |
| Security | ⚠️ Runs with user privileges | ✅ Containerized with limited capabilities |

## Best Practices

1. **Default to containerized tests** for CI/CD and release validation
2. **Use host testing** for rapid development iteration
3. **Run coverage in container** to ensure consistent metrics
4. **Clean builds periodically** to catch dependency issues
5. **Preserve test data** when debugging failures
6. **Use release builds** before tagging versions
7. **Monitor resource usage** to optimize container limits
8. **Keep Dockerfile updated** with latest dependency versions

## See Also

- [CLAUDE.md](CLAUDE.md) - Project architecture and conventions
- [TEST_IMPLEMENTATION.md](TEST_IMPLEMENTATION.md) - Test strategy and roadmap
- [COVERAGE.md](COVERAGE.md) - Coverage goals and achievements
- [tests/CMakeLists.txt](tests/CMakeLists.txt) - Test configuration
