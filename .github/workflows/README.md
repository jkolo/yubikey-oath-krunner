# GitHub Actions Workflows

This directory contains CI/CD workflows for automated testing and validation.

## Workflows Overview

### 🧪 `test.yml` - Continuous Testing

**Trigger:** Push to main/master/claude branches, Pull Requests

**Purpose:** Run full test suite on every push

**Jobs:**
- `test` - Run tests with Debug build
- `test-release` - Run tests with Release build (optimizations enabled)

**Artifacts:**
- Test results (7 days retention)
- Test logs

**Duration:** ~15-20 minutes

---

### 📊 `coverage.yml` - Code Coverage

**Trigger:** Push to main/master, Pull Requests

**Purpose:** Generate code coverage reports

**Jobs:**
- `coverage` - Build with coverage instrumentation, run tests, generate reports

**Artifacts:**
- HTML coverage report (30 days retention)
- LCOV info files

**Integrations:**
- Uploads to Codecov (requires `CODECOV_TOKEN` secret)
- Displays summary in GitHub UI

**Duration:** ~20-25 minutes

---

### ✅ `pr-checks.yml` - Pull Request Checks

**Trigger:** Pull Request events (opened, synchronized, reopened)

**Purpose:** Fast validation for PRs

**Jobs:**
- `quick-test` - Quick test run with caching
- `lint-check` - File structure validation
- Script permissions check
- Bash syntax validation

**Features:**
- Docker layer caching for faster builds
- Detailed test result summaries
- Early failure detection

**Duration:** ~10-15 minutes

---

### 🚀 `release.yml` - Release Validation

**Trigger:** Git tags (v*.*.*), Release creation

**Purpose:** Full validation before release

**Jobs:**
- `full-test-suite` - Matrix build (Debug + Release)
- `coverage-validation` - Coverage threshold check (≥85%)
- `release-summary` - Aggregate results

**Features:**
- Parallel execution (Debug and Release)
- Coverage threshold enforcement
- Release readiness gate
- 90-day artifact retention
- Automatic release comments

**Duration:** ~30-40 minutes

---

## Configuration

### Required Secrets

Add these secrets in **Settings → Secrets and variables → Actions**:

1. **`CODECOV_TOKEN`** (optional, for coverage reports)
   - Get from [codecov.io](https://codecov.io)
   - Navigate to repository settings → Copy upload token
   - Add as repository secret

### Optional Configuration

**Adjust Coverage Threshold:**

Edit `release.yml`, line ~65:

```yaml
THRESHOLD=85.0  # Change to desired percentage
```

**Modify Retention Periods:**

Each workflow has artifact retention settings:

```yaml
retention-days: 7    # Change as needed (1-90 days)
```

**Enable/Disable Workflows:**

Comment out unwanted jobs or add conditional logic:

```yaml
if: github.event_name == 'push'  # Only run on push
```

---

## Understanding Workflow Results

### ✅ Success Indicators

- All test jobs green
- Coverage meets threshold (≥85%)
- No syntax errors
- All required files present

### ❌ Failure Scenarios

1. **Test Failures**
   - Check artifact logs: `test-results/Testing/Temporary/LastTest.log`
   - Review failed test output in job logs
   - Run locally: `./scripts/test-in-container.sh test`

2. **Coverage Below Threshold**
   - Download coverage report artifact
   - View HTML report: `coverage_html/index.html`
   - Identify uncovered code sections

3. **Syntax Errors**
   - Check `lint-check` job output
   - Fix script syntax: `bash -n scripts/script.sh`

4. **Build Failures**
   - Review Docker build logs
   - Check Dockerfile.test for errors
   - Test locally: `docker-compose -f docker-compose.test.yml build tests`

---

## Local Testing

Test workflows locally before pushing:

### Test Container Build

```bash
docker-compose -f docker-compose.test.yml build tests
```

### Run Tests

```bash
docker-compose -f docker-compose.test.yml run --rm tests
```

### Generate Coverage

```bash
docker-compose -f docker-compose.test.yml run --rm tests-coverage
```

### Validate Scripts

```bash
bash -n scripts/*.sh
```

---

## Debugging Failed Workflows

### View Logs

1. Go to **Actions** tab
2. Click failed workflow run
3. Click failed job
4. Expand failed step

### Download Artifacts

1. Go to failed workflow run
2. Scroll to **Artifacts** section
3. Download relevant artifact
4. Extract and inspect logs

### Re-run Failed Jobs

1. Go to failed workflow run
2. Click **Re-run failed jobs**
3. Or: **Re-run all jobs** for full re-run

### Test in Container Locally

```bash
# Open interactive shell
./scripts/test-in-container.sh shell

# Inside container:
cd build-container-test
ctest -R failing_test --verbose --output-on-failure
```

---

## Performance Optimization

### Docker Layer Caching

`pr-checks.yml` uses BuildKit cache:

```yaml
- name: Cache Docker layers
  uses: actions/cache@v4
  with:
    path: /tmp/.buildx-cache
```

**Effect:** Reduces build time by ~50% on subsequent runs

### Parallel Execution

`release.yml` uses matrix strategy:

```yaml
strategy:
  matrix:
    build-type: [Debug, Release]
```

**Effect:** Runs Debug and Release tests in parallel

### Timeout Limits

All workflows have timeouts to prevent hanging:

```yaml
timeout-minutes: 30  # Adjust as needed
```

---

## Maintenance

### Update Actions Versions

Periodically update action versions:

```bash
# Find outdated actions
grep -r "uses:" .github/workflows/ | grep -v "@v4"

# Update to latest
# uses: actions/checkout@v3 → uses: actions/checkout@v4
```

### Monitor Workflow Duration

Track workflow duration in **Insights → Actions**:

- Identify slow jobs
- Optimize Docker builds
- Reduce test execution time

### Clean Up Old Artifacts

GitHub automatically deletes artifacts after retention period. To manually clean:

1. Go to **Actions** tab
2. Click workflow run
3. Delete artifacts manually (if needed)

---

## Troubleshooting

### "Error: Neither Podman nor Docker found"

**Cause:** Container runtime not available (shouldn't happen in GitHub Actions)

**Solution:** Workflow uses Docker by default. Check Docker setup step.

### "Coverage report not found"

**Cause:** Tests failed before coverage generation

**Solution:**
1. Check test job logs
2. Ensure tests pass first
3. Verify coverage service in docker-compose.test.yml

### "Codecov upload failed"

**Cause:** Missing `CODECOV_TOKEN` secret

**Solution:** Add token in repository settings (optional, workflow continues)

### "Container killed (OOM)"

**Cause:** Not enough memory

**Solution:** Reduce parallel jobs or increase runner size (requires paid plan)

---

## CI/CD Best Practices

1. **Keep workflows fast** - Use caching, parallel execution
2. **Fail fast** - Run quick checks (lint, syntax) before expensive tests
3. **Artifact everything** - Save logs, reports for debugging
4. **Monitor coverage** - Enforce thresholds to maintain quality
5. **Test locally first** - Validate changes before pushing

---

## Related Documentation

- [CONTAINERIZED_TESTING.md](../../CONTAINERIZED_TESTING.md) - Container setup
- [QUICKSTART_CONTAINERS.md](../../QUICKSTART_CONTAINERS.md) - Quick start guide
- [CLAUDE.md](../../CLAUDE.md) - Project architecture
- [TEST_IMPLEMENTATION.md](../../TEST_IMPLEMENTATION.md) - Test strategy

---

## Support

For issues with workflows:

1. Check workflow logs in **Actions** tab
2. Review [GitHub Actions documentation](https://docs.github.com/en/actions)
3. Test locally with containerized tests
4. Open issue in repository with workflow run link
