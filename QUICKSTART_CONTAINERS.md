# Quick Start: Containerized Testing

## 🚀 Szybki Start (3 polecenia)

```bash
# 1. Zainstaluj Podman (zalecane)
sudo pacman -S podman podman-compose

# 2. Zbuduj obraz kontenera
./scripts/test-in-container.sh build

# 3. Uruchom wszystkie testy
./scripts/test-in-container.sh test
```

## 📖 Podstawowe Użycie

### Uruchamianie Testów

```bash
# Standardowe testy (Debug build)
./scripts/test-in-container.sh test

# Z czyszczeniem build directory
./scripts/test-in-container.sh test --clean

# Z raportem pokrycia kodu
./scripts/test-in-container.sh coverage

# Release build (optymalizacje)
./scripts/test-in-container.sh release
```

### Debugowanie

```bash
# Interaktywny shell w kontenerze
./scripts/test-in-container.sh shell

# W shellu można:
cd build-container-test
ctest -R test_result --verbose
ls -la /tmp/test-home/.local/share/krunner-yubikey/
```

### Czyszczenie

```bash
# Usuń kontenery i volumes
./scripts/test-in-container.sh clean
```

## 🛠️ Alternatywnie: Makefile

```bash
# To samo co powyżej, ale krócej:
make -f Makefile.container build
make -f Makefile.container test
make -f Makefile.container coverage
make -f Makefile.container shell
make -f Makefile.container clean

# Jeszcze krócej (aliasy):
make -f Makefile.container t    # test
make -f Makefile.container c    # coverage
make -f Makefile.container s    # shell
```

## 🐳 Bezpośrednio Docker Compose

```bash
# Dla zaawansowanych użytkowników
docker-compose -f docker-compose.test.yml run --rm tests
docker-compose -f docker-compose.test.yml run --rm tests-coverage
docker-compose -f docker-compose.test.yml run --rm shell
```

## 💡 Co Jest Izolowane?

Kontenery **NIE DOTYKAJĄ** systemu hosta:

- ✅ **D-Bus** - prywatna sesja w `/tmp/test-home/.runtime/dbus-session`
- ✅ **KWallet** - test mode, izolowany storage w `/tmp/test-home/.local/share/kwalletd`
- ✅ **SQLite** - baza w `/tmp/test-home/.local/share/krunner-yubikey/devices.db`
- ✅ **Konfiguracja** - XDG directories w `/tmp/test-home/.config`
- ✅ **Qt** - offscreen platform, bez X11

**Możesz bezpiecznie uruchomić na maszynie produkcyjnej!**

## 🔍 Sprawdzanie Statusu

```bash
# Pokaż użycie zasobów
make -f Makefile.container status

# Sprawdź czy Podman/Docker są dostępne
./scripts/test-in-container.sh 2>&1 | head -1
```

## 📊 Raport Pokrycia

```bash
# Wygeneruj raport
./scripts/test-in-container.sh coverage

# Raport zostanie skopiowany do:
# ./coverage-report/coverage_html/index.html

# Otwórz w przeglądarce
xdg-open ./coverage-report/coverage_html/index.html
```

## ⚙️ Zmienne Środowiskowe

```bash
# Czysty build
CLEAN_BUILD=true ./scripts/test-in-container.sh test

# Zachowaj dane testowe
PRESERVE_TEST_DATA=true ./scripts/test-in-container.sh test

# Niestandardowy katalog build
BUILD_DIR=my-build ./scripts/test-in-container.sh test
```

## 🔧 Troubleshooting

### Problem: Brak Podman/Docker

```bash
# Arch Linux
sudo pacman -S podman podman-compose

# Fedora/RHEL
sudo dnf install podman-compose
```

### Problem: Wolne budowanie

```bash
# Użyj cached volumes (domyślnie włączone)
# Aby wyczyścić cache:
./scripts/test-in-container.sh clean
./scripts/test-in-container.sh build
```

### Problem: Testy nie przechodzą w kontenerze

```bash
# Debug w interaktywnym shellu
./scripts/test-in-container.sh shell

# W kontenerze:
cd build-container-test
ctest -R failing_test --verbose --output-on-failure

# Sprawdź logi D-Bus
dbus-monitor --session &

# Sprawdź environment
env | grep -E "XDG|DBUS|QT"
```

## 📚 Więcej Informacji

- **Pełna dokumentacja:** [CONTAINERIZED_TESTING.md](CONTAINERIZED_TESTING.md)
- **Architektura projektu:** [CLAUDE.md](CLAUDE.md)
- **Strategia testowania:** [TEST_IMPLEMENTATION.md](TEST_IMPLEMENTATION.md)

## 🎯 Typowe Scenariusze

### Scenariusz 1: Szybki test przed commitem

```bash
./scripts/test-in-container.sh test
```

### Scenariusz 2: Pełna walidacja przed release

```bash
./scripts/test-in-container.sh test --clean
./scripts/test-in-container.sh coverage
./scripts/test-in-container.sh release
```

### Scenariusz 3: Debugowanie konkretnego testu

```bash
./scripts/test-in-container.sh shell
# W kontenerze:
cd build-container-test
ctest -R test_password_service --verbose
```

### Scenariusz 4: CI/CD Integration

```yaml
# .github/workflows/test.yml
- name: Run containerized tests
  run: |
    docker-compose -f docker-compose.test.yml build tests
    docker-compose -f docker-compose.test.yml run --rm tests
```

---

**Pytania?** Zobacz [CONTAINERIZED_TESTING.md](CONTAINERIZED_TESTING.md) dla szczegółów.
