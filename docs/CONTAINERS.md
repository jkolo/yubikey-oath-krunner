

# Container Development Guide

This guide explains how to build, test, and deploy KRunner YubiKey OATH Plugin using OCI containers (Podman/Docker/Kubernetes).

## Table of Contents

- [Quick Start](#quick-start)
- [Container Architecture](#container-architecture)
- [Local Development](#local-development)
- [Running Tests](#running-tests)
- [Kubernetes Deployment](#kubernetes-deployment)
- [Registry Management](#registry-management)
- [Troubleshooting](#troubleshooting)

## Quick Start

### Prerequisites

**Option 1: Podman (recommended)**
```bash
# Arch Linux
sudo pacman -S podman podman-compose

# Ubuntu/Debian
sudo apt install podman podman-compose
```

**Option 2: Docker**
```bash
# Arch Linux
sudo pacman -S docker docker-compose

# Ubuntu/Debian
sudo apt install docker.io docker-compose
```

**Option 3: Kubernetes (for CI simulation)**
```bash
# Install kind (Kubernetes in Docker)
sudo pacman -S kind kubectl

# Or download from: https://kind.sigs.k8s.io/
```

### Build Container Images

```bash
# Build all stages
./scripts/build-container.sh all

# Or build specific stage
./scripts/build-container.sh builder   # Build environment only
./scripts/build-container.sh test      # Test environment
./scripts/build-container.sh artifacts # Minimal runtime image
```

### Run Tests

```bash
# Using podman-compose (recommended for local dev)
podman-compose run --rm test

# Or using the helper script
./scripts/run-tests-podman.sh

# Run specific test category
./scripts/run-tests-podman.sh unit  # Unit tests only
./scripts/run-tests-podman.sh e2e   # E2E tests only
```

### Interactive Development

```bash
# Start interactive shell in dev container
podman-compose run --rm dev

# Inside container:
cmake --preset clang-debug
cmake --build build-clang-debug -j$(nproc)
cd build-clang-debug
ctest --output-on-failure
```

## Container Architecture

### Multi-Stage Build

The `Containerfile` uses a multi-stage build to optimize image size and build times:

```
┌─────────────────────────────────────┐
│  Stage 1: builder                   │
│  - KDE neon base (Ubuntu 22.04)    │
│  - KDE Frameworks 6 pre-installed  │
│  - Build dependencies (Qt6 dev)    │
│  - CMake, Clang, ccache             │
│  - Compiles the project             │
│  Size: ~10GB                        │
└─────────────────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  Stage 2: test                      │
│  - Runtime dependencies             │
│  - D-Bus, Xvfb for tests            │
│  - Coverage tools (lcov, gcovr)     │
│  - Runs CTest                       │
│  Size: ~2GB                         │
└─────────────────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  Stage 3: artifacts                 │
│  - Minimal runtime environment      │
│  - Only installed binaries          │
│  - No build tools                   │
│  Size: ~500MB                       │
└─────────────────────────────────────┘
```

### Container Images

| Image | Target | Purpose | Size |
|-------|--------|---------|------|
| `registry.kolosowscy.pl/krunner-yubikey-oath:builder-latest` | `builder` | Compile project | ~4GB |
| `registry.kolosowscy.pl/krunner-yubikey-oath:test-latest` | `test` | Run tests | ~2GB |
| `registry.kolosowscy.pl/krunner-yubikey-oath:latest` | `artifacts` | Runtime only | ~500MB |

## Local Development

### Using Podman Compose

**Compile the project:**
```bash
podman-compose up build
```

**Run tests:**
```bash
podman-compose run --rm test
```

**Interactive shell:**
```bash
podman-compose run --rm dev

# Inside container, you can:
# - Edit code (mounted from host at /workspace)
# - Rebuild incrementally (ccache speeds this up)
# - Run specific tests
# - Generate coverage reports
```

**Extract build artifacts:**
```bash
podman-compose run --rm artifacts

# Artifacts copied to ./dist/ directory on host
```

**Clean volumes (force fresh build):**
```bash
podman-compose down -v
```

### Using Podman Directly

**Build image:**
```bash
podman build \
    --format=oci \
    --target test \
    --tag krunner-yubikey-test \
    -f Containerfile \
    .
```

**Run container:**
```bash
podman run --rm -it \
    -v ./:/workspace:rw \
    krunner-yubikey-test \
    bash
```

### Development Workflow

1. **Edit code** on your host machine (changes reflected in container via volume mount)

2. **Rebuild** inside container:
   ```bash
   podman-compose run --rm dev
   # Inside container:
   cmake --build build-clang-release -j$(nproc)
   ```

3. **Run tests**:
   ```bash
   ctest --test-dir build-clang-release --output-on-failure
   ```

4. **View coverage**:
   ```bash
   ./scripts/generate-coverage.sh build-clang-release
   # Open coverage-report/index.html on host
   ```

## Running Tests

### Test Isolation

Tests run in isolated environments:

- **D-Bus session**: Each test run gets a fresh D-Bus session via `dbus-run-session`
- **Xvfb display**: Virtual X11 server (`:99`) for input tests (libportal, X11)
- **No PC/SC daemon**: Tests use virtual devices (`VirtualYubiKey`, `VirtualNitrokey`)
- **Mock KWallet**: `MockSecretStorage` instead of real KWallet

### Test Categories

**Unit tests** (~30 tests, fast, <5s):
```bash
./scripts/run-tests-podman.sh unit
```

**E2E tests** (7 test cases, slower, ~30s):
```bash
./scripts/run-tests-podman.sh e2e
```

**All tests** (~34 tests, ~2 minutes):
```bash
./scripts/run-tests-podman.sh
```

### Coverage Reports

Coverage is automatically generated after successful test runs:

```bash
podman-compose run --rm test

# Coverage report available at:
# - HTML: coverage-report/index.html
# - XML (Cobertura): coverage-report/coverage.xml
```

View HTML report:
```bash
xdg-open coverage-report/index.html
```

## Kubernetes Deployment

### Local Kubernetes with kind

**Create cluster:**
```bash
kind create cluster --name krunner-test
```

**Deploy and run tests:**
```bash
./scripts/k8s-deploy.sh full
```

**View logs:**
```bash
./scripts/k8s-deploy.sh logs build-job   # Build logs
./scripts/k8s-deploy.sh logs test-job    # Test logs
```

**Extract artifacts:**
```bash
# Coverage report extracted automatically to ./coverage-report/
# Test results extracted to ./k8s-test-results/
```

**Cleanup:**
```bash
./scripts/k8s-deploy.sh cleanup
kind delete cluster --name krunner-test
```

### Kubernetes Resources

The project includes full Kubernetes manifests in `k8s/`:

- `namespace.yaml` - Isolated namespace
- `configmap.yaml` - Build and test configuration
- `secret.yaml.example` - Template for registry credentials
- `pvc.yaml` - Persistent storage for build cache
- `build-job.yaml` - Kubernetes Job for building
- `test-job.yaml` - Kubernetes Job for testing (with D-Bus and Xvfb sidecars)

See [k8s/README.md](../k8s/README.md) for detailed Kubernetes documentation.

## Registry Management

### Login to Registry

```bash
# Interactive login
podman login registry.kolosowscy.pl

# Non-interactive (CI/CD)
echo "$REGISTRY_PASSWORD" | podman login -u "$REGISTRY_USER" --password-stdin registry.kolosowscy.pl
```

### Push Images

```bash
# Tag with version
podman tag krunner-yubikey-oath:builder-latest registry.kolosowscy.pl/krunner-yubikey-oath:builder-v2.1.0

# Push to registry
podman push registry.kolosowscy.pl/krunner-yubikey-oath:builder-v2.1.0
podman push registry.kolosowscy.pl/krunner-yubikey-oath:builder-latest
```

### Pull Images

```bash
# Pull from registry
podman pull registry.kolosowscy.pl/krunner-yubikey-oath:test-latest

# Run tests with pulled image
podman run --rm registry.kolosowscy.pl/krunner-yubikey-oath:test-latest
```

## Troubleshooting

### Common Issues

**Issue: "Permission denied" when accessing /var/run/dbus**
```bash
# D-Bus tests run in isolated session, no system bus needed
# This is expected and tests use dbus-run-session
```

**Issue: "Cannot connect to PC/SC daemon"**
```bash
# Tests use virtual devices, no pcscd required
# VirtualYubiKey and VirtualNitrokey are mocked in tests/mocks/
```

**Issue: "XDG_RUNTIME_DIR not set"**
```bash
# Set QT_QPA_PLATFORM=offscreen for headless Qt
# Already configured in docker-compose.yml
```

**Issue: "ccache: FATAL: Could not create temporary file"**
```bash
# Check ccache volume permissions
podman volume inspect krunner-yubikey-oath_ccache-data

# Or clear and recreate
podman-compose down -v
podman-compose up build
```

**Issue: Tests fail with "D-Bus socket not found"**
```bash
# Ensure dbus-daemon sidecar is running (Kubernetes)
# Or use dbus-run-session wrapper (podman-compose)

kubectl logs -n krunner-yubikey-ci <pod-name> -c dbus-daemon
```

**Issue: "No space left on device" during build**
```bash
# Clean up old images and volumes
podman system prune -a -f
podman volume prune -f

# Or increase storage in /var/lib/containers/storage
```

### Debug Shell Access

**Podman:**
```bash
# Start shell in test container
podman run --rm -it registry.kolosowscy.pl/krunner-yubikey-oath:test-latest bash

# Attach to running container
podman exec -it <container-id> bash
```

**Kubernetes:**
```bash
# Get pod name
kubectl get pods -n krunner-yubikey-ci

# Shell into test pod
kubectl exec -it <pod-name> -n krunner-yubikey-ci -c test-runner -- bash

# View logs
kubectl logs <pod-name> -n krunner-yubikey-ci -c test-runner
kubectl logs <pod-name> -n krunner-yubikey-ci -c dbus-daemon
kubectl logs <pod-name> -n krunner-yubikey-ci -c xvfb
```

### Performance Optimization

**Enable ccache for faster rebuilds:**
```bash
# Already configured in Containerfile and docker-compose.yml
# ccache reduces rebuild time by ~60%
```

**Use BuildKit for layer caching:**
```bash
export BUILDAH_FORMAT=oci
buildah bud --layers --cache-from=... -t ...
```

**Parallel test execution:**
```bash
# Set CTEST_PARALLEL_LEVEL in configmap.yaml (default: 4)
ctest --parallel 8  # Use 8 cores
```

### Container Size Optimization

**Check image sizes:**
```bash
podman images | grep krunner-yubikey
```

**Analyze layers:**
```bash
podman history registry.kolosowscy.pl/krunner-yubikey-oath:builder-latest
```

**Reduce image size:**
- Use multi-stage builds (already implemented)
- Clean up package manager cache: `rm -rf /var/lib/apt/lists/*`
- Remove build-only dependencies in final stage
- Use `.containerignore` to exclude unnecessary files

## Related Documentation

- [CI/CD Pipeline](CI_CD.md) - GitLab CI/CD configuration
- [Kubernetes Guide](../k8s/README.md) - Detailed Kubernetes documentation
- [Main README](../README.md) - Project overview
- [Testing Strategy](../TEST_IMPLEMENTATION.md) - Test architecture

## Support

For issues related to containerization:

1. Check this troubleshooting guide first
2. Review GitLab CI/CD logs: https://git.kolosowscy.pl/jkolo/krunner-yubikey-oath/-/pipelines
3. Report bugs: https://github.com/jkolo/yubikey-oath-krunner/issues
4. Check project documentation: https://git.kolosowscy.pl/jkolo/krunner-yubikey-oath/-/tree/main/docs
