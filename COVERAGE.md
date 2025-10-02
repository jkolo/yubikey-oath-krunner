# Code Coverage Guide

This document describes how to generate and analyze code coverage reports for the KRunner YubiKey OATH Plugin.

## Prerequisites

### Required Tools

- **lcov** - Coverage data processing tool
- **gcov** - GNU coverage tool (included with GCC)
- **genhtml** - HTML report generator (part of lcov)

### Installation

**Arch Linux:**
```bash
sudo pacman -S lcov
```

**Debian/Ubuntu:**
```bash
sudo apt install lcov
```

**Fedora:**
```bash
sudo dnf install lcov
```

## Quick Start

### 1. Build with Coverage Enabled

Create a separate build directory for coverage builds:

```bash
# Clean build with coverage enabled
cmake -B build-coverage \
      -DENABLE_COVERAGE=ON \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_INSTALL_PREFIX=$HOME/.local

# Build the project
cmake --build build-coverage -j$(nproc)
```

**Important:** Coverage must be enabled at configure time with `-DENABLE_COVERAGE=ON`.

### 2. Run the Plugin

To generate coverage data, you need to actually run the code:

```bash
# Install to local directory
cmake --install build-coverage

# Run KRunner with the plugin
krunner --replace

# Use the plugin:
# - Open KRunner (Alt+Space)
# - Type to search for credentials
# - Generate some TOTP codes
# - Try different actions (copy, type)
# - Test touch-required credentials
# - Quit KRunner when done
```

**Note:** The more functionality you exercise, the better the coverage report.

### 3. Generate Coverage Report

Use the provided script to generate an HTML coverage report:

```bash
./scripts/generate-coverage.sh build-coverage
```

This will:
1. Capture coverage data from `.gcda` files
2. Filter out system/external files
3. Generate HTML report in `build-coverage/coverage_html/`
4. Display coverage summary

### 4. View the Report

```bash
# Open in default browser
xdg-open build-coverage/coverage_html/index.html

# Or use Firefox directly
firefox build-coverage/coverage_html/index.html
```

## Manual Coverage Generation

If you prefer manual control, here's the step-by-step process:

### Capture Coverage Data

```bash
cd build-coverage

lcov --capture \
     --directory . \
     --output-file coverage.info \
     --rc lcov_branch_coverage=1
```

### Remove External Code

Filter out system headers, Qt, KDE, and generated files:

```bash
lcov --remove coverage.info \
     '/usr/*' \
     '*/build/*' \
     '*/Qt6/*' \
     '*/KF6/*' \
     '*_autogen/*' \
     '*/moc_*.cpp' \
     --output-file coverage.info \
     --rc lcov_branch_coverage=1
```

### Generate HTML Report

```bash
genhtml coverage.info \
        --output-directory coverage_html \
        --title "KRunner YubiKey Code Coverage" \
        --legend \
        --show-details \
        --branch-coverage \
        --rc lcov_branch_coverage=1
```

### View Summary

```bash
lcov --summary coverage.info --rc lcov_branch_coverage=1
```

## Understanding the Report

### Coverage Metrics

The report shows three types of coverage:

1. **Line Coverage** - Percentage of code lines executed
2. **Function Coverage** - Percentage of functions called
3. **Branch Coverage** - Percentage of conditional branches taken

### Target Coverage Goals

- **Core Logic** (`src/oath/`, `src/core/`): Aim for >50%
- **Coordinators** (`src/runner/*_coordinator.cpp`): Aim for >40%
- **Input Providers** (`src/input/`): Platform-dependent, >30%
- **Overall Project**: Aim for >40%

### Interpreting Colors

- 🟢 **Green** - High coverage (>75%)
- 🟡 **Orange** - Medium coverage (50-75%)
- 🔴 **Red** - Low coverage (<50%)

## CI/CD Integration (Future)

### GitHub Actions Example

```yaml
name: Code Coverage

on: [push, pull_request]

jobs:
  coverage:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Install Dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y lcov ...

      - name: Build with Coverage
        run: |
          cmake -B build -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
          cmake --build build

      - name: Run Tests
        run: |
          # Run test suite here
          cmake --build build --target test

      - name: Generate Coverage
        run: ./scripts/generate-coverage.sh build

      - name: Upload to Codecov
        uses: codecov/codecov-action@v3
        with:
          file: build/coverage.info
```

## Troubleshooting

### No Coverage Data Generated

**Problem:** `.gcda` files not created after running the plugin.

**Solution:**
1. Verify coverage was enabled: `grep ENABLE_COVERAGE build-coverage/CMakeCache.txt`
2. Ensure you actually ran the plugin (not just built it)
3. Check compiler is GCC or Clang: `cmake --system-information | grep CMAKE_CXX_COMPILER`

### Low Coverage Numbers

**Problem:** Coverage report shows unexpectedly low numbers.

**Solutions:**
- Run the plugin longer and exercise more features
- Test different code paths (errors, edge cases)
- Use multiple YubiKeys if testing multi-device support
- Trigger touch-required credentials

### Permission Errors

**Problem:** Cannot write `.gcda` files.

**Solution:**
- Ensure build directory is writable
- Run without `sudo` if possible
- Check file ownership: `ls -la build-coverage/`

### lcov Errors

**Problem:** `lcov` fails with "geninfo: ERROR: ...".

**Solutions:**
- Update lcov: `sudo pacman -S lcov` or equivalent
- Use `--ignore-errors` flag if needed
- Check GCC/Clang compatibility

## Cleaning Coverage Data

To start fresh with new coverage data:

```bash
# Remove all .gcda files
find build-coverage -name '*.gcda' -delete

# Or clean and rebuild
rm -rf build-coverage
cmake -B build-coverage -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-coverage
```

## Best Practices

1. **Separate Build Directory** - Use `build-coverage` to avoid mixing with regular builds
2. **Debug Build** - Always use `-DCMAKE_BUILD_TYPE=Debug` for coverage
3. **Exercise Code** - Run multiple scenarios to get meaningful coverage
4. **Review Uncovered Lines** - Focus on critical paths that lack coverage
5. **Incremental Testing** - Generate coverage after each major feature
6. **Document Gaps** - Note why certain code cannot be easily tested

## File Locations

- **CMake Option**: `CMakeLists.txt` (line 8: `ENABLE_COVERAGE`)
- **Coverage Script**: `scripts/generate-coverage.sh`
- **Coverage Data**: `build-coverage/*.gcda` (after running)
- **Coverage Report**: `build-coverage/coverage_html/index.html`
- **Coverage Info**: `build-coverage/coverage.info`

## Further Reading

- [lcov Documentation](http://ltp.sourceforge.net/coverage/lcov.php)
- [gcov Manual](https://gcc.gnu.org/onlinedocs/gcc/Gcov.html)
- [CMake Coverage Tutorial](https://cmake.org/cmake/help/latest/manual/cmake-commands.7.html)
