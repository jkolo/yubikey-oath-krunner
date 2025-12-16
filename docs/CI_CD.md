# CI/CD Pipeline Documentation

Complete guide to the GitLab CI/CD pipeline for KRunner YubiKey OATH Plugin.

## Table of Contents

- [Overview](#overview)
- [Pipeline Stages](#pipeline-stages)
- [Setup](#setup)
- [Release Workflow](#release-workflow)
- [Monitoring](#monitoring)
- [Troubleshooting](#troubleshooting)

## Overview

### Architecture

```
GitLab CI/CD (git.kolosowscy.pl)
  ├─ Docker Executor (with podman backend)
  ├─ Container Registry (registry.kolosowscy.pl)
  └─ Artifacts Storage (coverage, tarballs, test results)
       │
       ├─ GitHub Mirror (github.com/jkolo/yubikey-oath-krunner)
       │   └─ Automated releases on git tags
       │
       └─ AUR Package (aur.archlinux.org/krunner-yubikey-oath)
           └─ Automated PKGBUILD updates
```

### Key Features

✅ **Automated container builds** - OCI images built and cached on every push
✅ **Parallel execution** - Build debug + release simultaneously
✅ **Comprehensive testing** - Unit, service, storage, and E2E tests
✅ **Coverage reporting** - HTML + Cobertura XML, integrated in MRs
✅ **Static analysis** - clang-tidy on all code
✅ **Release automation** - Git tag → GitHub release → AUR update
✅ **Artifact caching** - ccache + layer caching for 60% faster builds

### Pipeline Duration

| Stage | Duration | Parallelization |
|-------|----------|-----------------|
| build-image | ~8 min | 2 jobs (builder + test) |
| build | ~5 min | 2 jobs (debug + release) |
| test | ~2 min | 2 jobs (unit + e2e) |
| coverage | ~1 min | 1 job |
| analysis | ~3 min | 1 job (allow_failure) |
| package | ~30s | 1 job (tags only) |
| deploy | ~1 min | 2 jobs (tags only) |

**Total:** ~12 minutes (branches), ~15 minutes (tags with deploy)

## Pipeline Stages

### Stage 1: build-image

**Purpose:** Build OCI container images for subsequent stages

**Jobs:**
- `build:container-builder` - Builds `builder` stage (compile environment)
- `build:container-test` - Builds `test` stage (runtime + test tools)

**Images produced:**
- `registry.kolosowscy.pl/krunner-yubikey-oath:builder-${CI_COMMIT_REF_SLUG}`
- `registry.kolosowscy.pl/krunner-yubikey-oath:test-${CI_COMMIT_REF_SLUG}`
- Tagged as `-latest` on `main` branch

**Caching:** BuildKit layer caching from previous builds

**Triggers:** All branches, merge requests, tags

---

### Stage 2: build

**Purpose:** Compile the project with CMake

**Jobs:**

**`build:debug`**
- Preset: `clang-debug`
- Compiler: Clang 16
- Artifacts: `build-clang-debug/` (1 day retention)
- Cache: `.ccache/` (per-branch key)
- Parallel: Yes (runs simultaneously with `build:release`)

**`build:release`**
- Preset: `clang-release`
- Compiler: Clang 16
- Flags: `-DENABLE_COVERAGE=ON`
- Artifacts: `build-clang-release/` (7 days retention)
- Cache: `.ccache/` (per-branch key)
- Parallel: Yes

**Performance:** ccache reduces rebuild time by ~60%

**Triggers:** Branches, merge requests, tags

---

### Stage 3: test

**Purpose:** Run all tests with virtual devices

**Jobs:**

**`test:unit`**
- Filter: Unit, service, and storage tests (28 tests)
- Duration: ~30 seconds
- Output: JUnit XML for GitLab integration
- Environment: D-Bus session + Xvfb

**`test:e2e`**
- Filter: End-to-end tests (7 test cases)
- Duration: ~1 minute
- Output: JUnit XML
- Timeout: 5 minutes
- Environment: Full stack with virtual YubiKey/Nitrokey

**Test Isolation:**
- Fresh D-Bus session per run (`dbus-run-session`)
- Xvfb virtual display (`:99`)
- No PC/SC daemon (uses `VirtualYubiKey`/`VirtualNitrokey`)
- Mock KWallet (`MockSecretStorage`)

**Artifacts:**
- `test-results-unit.xml` (7 days)
- `test-results-e2e.xml` (7 days)
- CTest logs in `Testing/` directory

**Triggers:** Branches, merge requests, tags

---

### Stage 4: coverage

**Purpose:** Generate test coverage reports

**Job:** `coverage:report`

**Inputs:**
- Requires successful `test:unit` and `test:e2e`
- Uses `build:release` artifacts

**Outputs:**
- HTML report: `coverage-report/index.html`
- Cobertura XML: `coverage-report/coverage.xml`
- Retention: 30 days

**Integration:**
- Coverage percentage displayed in merge requests
- Regex: `/^TOTAL.*\s+(\d+\%)$/`

**Target:** ~85% line coverage, ~87% function coverage ✅

**Triggers:** Merge requests, `main` branch, tags

---

### Stage 5: analysis

**Purpose:** Static code analysis

**Job:** `lint:clang-tidy`

**Tools:** clang-tidy-16

**Configuration:**
- Uses `build-clang-release` compile database
- Runs `run-clang-tidy -p .` on all source files

**Output:**
- `clang-tidy-report.txt` (7 days retention)

**Policy:** `allow_failure: true` (warnings don't block pipeline)

**Triggers:** Merge requests, `main` branch

---

### Stage 6: package

**Purpose:** Create release tarballs from git tags

**Job:** `package:tarball`

**Process:**
1. Validates git tag exists
2. Creates tarball: `git archive --prefix=krunner-yubikey-oath-${VERSION}/`
3. Generates SHA256 checksum
4. Stores both as artifacts (permanent retention)

**Artifacts:**
- `krunner-yubikey-oath-${VERSION}.tar.gz`
- `krunner-yubikey-oath-${VERSION}.tar.gz.sha256`

**Triggers:** Git tags only (`$CI_COMMIT_TAG`)

---

### Stage 7: deploy

**Purpose:** Automated release to GitHub and AUR

**Jobs:**

**`deploy:github-release`**
1. Extracts release notes from `CHANGELOG.md`
2. Creates GitHub release using `gh` CLI
3. Uploads tarball + SHA256 checksum
4. Saves download URL for AUR

**`deploy:aur-update`**
1. Clones AUR repository: `ssh://aur@aur.archlinux.org/krunner-yubikey-oath.git`
2. Updates `PKGBUILD`:
   - `pkgver=${VERSION}`
   - `pkgrel=1`
   - `sha256sums` from tarball checksum
   - `source` URL from GitHub release
3. Generates `.SRCINFO`: `makepkg --printsrcinfo`
4. Commits and pushes to AUR

**Triggers:** Git tags only, sequential (GitHub → AUR)

---

## Setup

### Prerequisites

**GitLab Runner:**
- Docker executor with podman backend
- 4 CPU cores minimum
- 8GB RAM minimum
- 50GB disk space (for container images and cache)

**Container Registry:**
- Access to `registry.kolosowscy.pl`
- Credentials for push/pull

**External Services:**
- GitHub account with Personal Access Token
- AUR account with SSH key configured

### Required CI/CD Variables

Navigate to **Settings → CI/CD → Variables** and add:

| Variable | Type | Masked | Description |
|----------|------|--------|-------------|
| `REGISTRY_USER` | Variable | ✅ | Username for registry.kolosowscy.pl |
| `REGISTRY_PASSWORD` | Variable | ✅ | Password for registry.kolosowscy.pl |
| `GITHUB_TOKEN` | Variable | ✅ | GitHub PAT with `repo` scope |
| `AUR_SSH_KEY` | File | ✅ | SSH private key for aur@aur.archlinux.org |
| `AUR_GIT_EMAIL` | Variable | ❌ | Email for AUR commits (e.g., ci@kolosowscy.pl) |
| `AUR_GIT_NAME` | Variable | ❌ | Name for AUR commits (e.g., GitLab CI) |

### GitHub Personal Access Token

**Create token:**
1. GitHub → Settings → Developer settings → Personal access tokens → Tokens (classic)
2. Generate new token (classic)
3. Scopes: `repo` (Full control of private repositories)
4. Copy token and save to `GITHUB_TOKEN` GitLab variable

**Test authentication:**
```bash
export GITHUB_TOKEN=ghp_your_token_here
gh auth status
```

### AUR SSH Key

**Generate key:**
```bash
ssh-keygen -t ed25519 -C "ci@kolosowscy.pl" -f ~/.ssh/aur_ed25519
```

**Add to AUR:**
1. Login to https://aur.archlinux.org/
2. Account → SSH Public Keys → Add
3. Paste content of `~/.ssh/aur_ed25519.pub`

**Add to GitLab:**
1. Settings → CI/CD → Variables → Add variable
2. Key: `AUR_SSH_KEY`
3. Type: File
4. Value: Paste content of `~/.ssh/aur_ed25519` (private key)
5. Flags: Masked, Protected (optional)

**Test connection:**
```bash
ssh -i ~/.ssh/aur_ed25519 aur@aur.archlinux.org
# Should output: "Hi username, you've successfully authenticated..."
```

### Container Registry Access

**Manual login (for local testing):**
```bash
podman login registry.kolosowscy.pl
# Enter REGISTRY_USER and REGISTRY_PASSWORD
```

**CI/CD login:**
Automatic via `.gitlab-ci.yml` `before_script`:
```yaml
before_script:
  - echo "$REGISTRY_PASSWORD" | podman login -u "$REGISTRY_USER" --password-stdin registry.kolosowscy.pl
```

## Release Workflow

### Complete Release Process

**1. Prepare release**
```bash
# Update version in all files
vim CMakeLists.txt           # set(PROJECT_VERSION "2.1.0")
vim src/krunner/yubikeyrunner.json
vim src/krunner/plasma-runner-yubikey.desktop
vim src/shared/po/pl.po
vim README.md

# Update CHANGELOG.md
vim CHANGELOG.md
# Add section:
## [2.1.0] - 2024-11-17
### Added
- New feature...
### Fixed
- Bug fix...
```

**2. Commit changes**
```bash
git add .
git commit -m "chore: release version 2.1.0"
git push origin main
```

**3. Create and push tag**
```bash
git tag -a v2.1.0 -m "Release version 2.1.0"
git push origin v2.1.0
```

**4. Automated pipeline**
GitLab CI will automatically:
- ✅ Build container images
- ✅ Build project (debug + release)
- ✅ Run all tests (unit + E2E)
- ✅ Generate coverage report
- ✅ Run clang-tidy analysis
- ✅ Create release tarball
- ✅ Create GitHub release with tarball
- ✅ Update AUR PKGBUILD

**5. Verify release**
```bash
# Check GitLab pipeline
# https://git.kolosowscy.pl/jkolo/krunner-yubikey-oath/-/pipelines

# Check GitHub release
# https://github.com/jkolo/yubikey-oath-krunner/releases/tag/v2.1.0

# Check AUR package
# https://aur.archlinux.org/packages/krunner-yubikey-oath
```

**Total time:** ~15 minutes (fully automated)

### Manual Release (if CI fails)

If automated deployment fails, you can run scripts manually:

```bash
# 1. Create tarball
./scripts/create-release-tarball.sh v2.1.0

# 2. Create GitHub release
export GITHUB_TOKEN=ghp_your_token_here
./scripts/create-github-release.sh v2.1.0

# 3. Update AUR
# (Requires AUR SSH key in ~/.ssh/)
./scripts/update-aur-package.sh v2.1.0
```

## Monitoring

### Pipeline Status

**GitLab Web UI:**
- https://git.kolosowscy.pl/jkolo/krunner-yubikey-oath/-/pipelines
- View stages, job logs, artifacts
- Download coverage reports, test results

**CLI:**
```bash
# Install glab (GitLab CLI)
pacman -S glab

# View pipelines
glab pipeline list

# View specific pipeline
glab pipeline view 12345

# Download artifacts
glab pipeline artifact download 12345
```

### Test Results

**JUnit Integration:**
- Merge requests show test results inline
- Failed tests highlighted with diffs

**Coverage:**
- Coverage percentage badge in README
- Trend graph in merge requests
- Click-through to line-by-line coverage

### Artifacts

**Available artifacts (downloadable from pipeline):**

| Artifact | Stage | Retention |
|----------|-------|-----------|
| `build-clang-debug/` | build | 1 day |
| `build-clang-release/` | build | 7 days |
| `test-results-unit.xml` | test | 7 days |
| `test-results-e2e.xml` | test | 7 days |
| `coverage-report/` | coverage | 30 days |
| `clang-tidy-report.txt` | analysis | 7 days |
| `*.tar.gz` | package | Permanent |

### Cache Performance

**View cache hits:**
```bash
# In job logs, look for:
# "ccache: cache hit (direct) 1234"
# "ccache: cache miss 56"
# "ccache: files compiled 1234"
```

**Cache statistics:**
- First build: ~10 minutes (cold cache)
- Incremental rebuild: ~4 minutes (warm cache, 60% reduction)

## Troubleshooting

### Common Pipeline Failures

**Issue: "Could not login to registry"**
```
Fix: Check REGISTRY_USER and REGISTRY_PASSWORD variables
Verify: podman login registry.kolosowscy.pl (manually)
```

**Issue: "Test failed: test_e2e_device_lifecycle"**
```
Check: D-Bus session properly isolated
View: Download test-results-e2e.xml artifact
Debug: Run locally with ./scripts/run-tests-podman.sh e2e
```

**Issue: "GitHub release failed: authentication error"**
```
Fix: Check GITHUB_TOKEN variable is set correctly
Test: gh auth status (with token)
Permissions: Token needs 'repo' scope
```

**Issue: "AUR push failed: Permission denied (publickey)"**
```
Fix: Check AUR_SSH_KEY variable contains private key
Test: ssh -i <key-file> aur@aur.archlinux.org
Verify: Public key added to AUR account settings
```

**Issue: "Coverage report empty"**
```
Check: ENABLE_COVERAGE=ON in CMake configuration
Rebuild: Clear cache and rebuild with coverage flags
Verify: *.gcda files present in build directory
```

**Issue: "clang-tidy timeout"**
```
Workaround: allow_failure: true (warnings don't block)
Fix: Run locally and fix issues incrementally
Skip: Remove lint:clang-tidy job temporarily
```

### Debugging Failed Jobs

**View job logs:**
```bash
# Web UI: Click on failed job
# Or via CLI:
glab pipeline view <pipeline-id>
```

**Download artifacts:**
```bash
# Web UI: Pipeline → right sidebar → Download artifacts
# Or via CLI:
glab pipeline artifact download <pipeline-id>
```

**Reproduce locally:**
```bash
# Use same container image as CI
podman pull registry.kolosowscy.pl/krunner-yubikey-oath:test-latest

# Run interactively
podman run --rm -it \
  -v ./:/workspace:rw \
  registry.kolosowscy.pl/krunner-yubikey-oath:test-latest \
  bash

# Inside container:
cd /workspace
cmake --preset clang-release
cmake --build build-clang-release -j$(nproc)
./scripts/ci-test-runner.sh
```

### Performance Issues

**Slow builds:**
- Check ccache statistics in logs
- Verify `.ccache/` cache is persisted (key: `build-*-${CI_COMMIT_REF_SLUG}`)
- Clear corrupted cache: delete cache key in Settings → CI/CD → Cache

**Slow tests:**
- Parallel execution: `CTEST_PARALLEL_LEVEL=4` (default)
- Increase to 8 for faster runners: edit `configmap.yaml`

**Large artifacts:**
- Coverage reports can be large (~50MB)
- Reduce retention: change `expire_in` in `.gitlab-ci.yml`

## Related Documentation

- [Container Development](CONTAINERS.md) - Local development with Podman
- [Kubernetes Guide](../k8s/README.md) - Kubernetes manifests
- [Testing Strategy](../TEST_IMPLEMENTATION.md) - Test architecture
- [Main README](../README.md) - Project overview

## Support

**GitLab Issues:**
- https://git.kolosowscy.pl/jkolo/krunner-yubikey-oath/-/issues

**GitHub Issues (public):**
- https://github.com/jkolo/yubikey-oath-krunner/issues

**Pipeline Debugging:**
1. Check job logs in GitLab UI
2. Download artifacts for detailed analysis
3. Reproduce locally using same container image
4. Check CI/CD variables are correctly set
