# Daemon Refactor Notes

## Session Start: 2025-11-21

### Initial Exploration Results

**Architecture Review (Task 1):**
- ✅ Overall architecture is excellent (9/10)
- Clear layering, no circular dependencies
- Dependency Inversion pattern properly implemented (IOathSelector)
- Template Method pattern eliminates 550+ lines of duplication
- Minor concerns:
  - OathActionCoordinator has high coupling (5+ dependencies)
  - OathService is a "God Object" but acceptable for daemon use
  - UI components in daemon (justified for add-credential dialog)

**Security Review (Task 2):**
- ✅ Strong security posture (8/10)
- SecureMemory::SecureString properly used
- RAII patterns throughout
- Proper PC/SC resource management
- Minor issues:
  - APDU response bounds checking could be more explicit
  - Incomplete async workflows (CopyToClipboard, TypeCode - TODO comments)
  - Manual memory management for QMetaObject::Connection (line 233)
  - No D-Bus policy files

**TODOs Found:**
- `oath_credential_object.cpp:201` - Implement full async workflow with clipboard signals
- `oath_credential_object.cpp:215` - Implement full async workflow with type signals
- Various comments about Unicode encoding and serial number format (not actionable TODOs)

**Tests Status:**
- Build directory doesn't exist yet - need to build first

### Completed Reviews:
1. ✅ Architecture Review - 9/10, excellent layering
2. ✅ Security Review - 8/10, strong posture
3. ✅ SOLID/OOP Review - Grade B+, several issues found
4. ✅ Code Duplication - 143 lines found (6 patterns)
5. ✅ Translations - 60 missing i18n() calls (mostly tr() instead)
6. ✅ Unused Code - 11 qWarning/qDebug without categories
7. ✅ Performance - 12 issues found (3 critical, 5 high, 4 medium)

### Key Issues Summary:

**HIGH PRIORITY (Must Fix):**
1. Missing i18n() - 60 occurrences (yk_oath_session.cpp: 41, nitrokey_oath_session.cpp: 10, others: 9)
2. N+1 Device Lookup - getAllDevicesWithCredentials() issue
3. QString by Value - 55 occurrences causing heap allocations
4. TODOs in oath_credential_object.cpp - lines 201, 215 (async workflows)
5. Missing [[nodiscard]] - Result<T> methods lack compile-time checks
6. Code Duplication - 143 lines (CardTransaction validation, Password auth, D-Bus registration)

**MEDIUM PRIORITY:**
7. YkOathSession::getExtendedDeviceInfo() - 312 lines, needs Strategy Pattern
8. OathProtocol interface segregation - 76 static + 2 virtual methods
9. PortalTextInput - 579 lines, needs session extraction
10. OathDatabase - 881 lines, needs split into Repository + Cache
11. qWarning/qDebug without categories - 11 occurrences
12. Performance issues - double hash lookups, excessive logging, etc.

### Completed Work:

**Phase 1.1 - Translations (COMPLETE):**
- ✅ yk_oath_session.cpp: 41 tr() → i18n()
- ✅ nitrokey_oath_session.cpp: 10 tr() → i18n()
- ✅ oath_service.cpp: 6 tr() → i18n()
- ✅ oath_device_manager.cpp: 2 tr() → i18n(), fixed .arg() patterns
- ✅ oath_credential_object.cpp: 2 QStringLiteral → i18n()
- **Total: 61 fixes across 5 files**
- All .arg() patterns converted to i18n() format: i18n("text %1", value)
- Added #include <KLocalizedString> to 5 files

### Remaining Work (Prioritized):

**HIGH PRIORITY (Recommended for immediate implementation):**
1. Phase 1.2: Fix code duplication (143 lines, 4 patterns)
   - CardTransaction validation (6 instances, 30 lines)
   - Password authentication (3 instances, 24 lines)
   - D-Bus registration (3 classes, 82 lines)
   - Notification availability checks (7 instances, 15 lines)

2. Phase 1.3: Fix logging categories (11 occurrences)
   - Replace qWarning() → qCWarning(Category)
   - Replace qDebug() → qCDebug(Category)

3. Phase 1.4: Add [[nodiscard]] attributes (59+ methods)
   - All Result<T> returning methods
   - Compile-time error detection

**MEDIUM PRIORITY:**
4. Performance fixes (12 issues identified)
   - N+1 device lookup
   - QString by value (55 occurrences)
   - Double hash lookups

5. SOLID refactoring
   - Extract Strategy Pattern for device info (312 lines → multiple files)
   - Segregate OathProtocol interface
   - Extract PortalTextInput session management
   - Split OathDatabase

**DOCUMENTATION:**
6. Update CLAUDE.md
   - Fix 30+ incorrect class names (YubiKey* → Oath*)
   - Recalculate line counts (76% inaccurate)
   - Update file paths

### Summary for User:
- ✅ Comprehensive review complete (architecture, security, SOLID, translations, performance, duplication)
- ✅ Detailed improvement plan created (IMPROVEMENT_PLAN.md)
- ✅ Phase 1.1 complete: All 61 translation issues fixed
- 📋 Generated documentation:
  - IMPROVEMENT_PLAN.md (detailed implementation guide)
  - PERFORMANCE_REVIEW.md (12 performance issues)
  - QUICK_FIXES.md (implementation snippets)
  - PERFORMANCE_INDEX.md (navigation guide)
- **Recommended next step:** Implement Phase 1.2 (code duplication) or Phase 1.3 (logging categories)
