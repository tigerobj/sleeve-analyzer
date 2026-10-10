# Library Insert Factor Inspector Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and safely install a read-only LibreCAD plugin that reports the active Library Insert factor alongside its persisted setting.

**Architecture:** A small testable snapshot helper locates `leFactor` and `leAngle` only inside the visible `QG_LibraryInsertOptions` pane, then reads the `LibraryInsert/LibraryInsertFactor` setting without writing it. A modal dialog displays the two sources separately; a `QC_PluginInterface` adapter exposes it under the Plugins menu. Build and load-test the uniquely named DLL in an isolated LibreCAD copy before installing it into the real application.

**Tech Stack:** C++11, Qt Widgets/Qt Test and QSettings, LibreCAD `QC_PluginInterface`, qmake, MinGW.

**Spec:** `docs/superpowers/specs/2026-10-07-library-insert-factor-inspector-design.md`

## Global Constraints

- The installed LibreCAD reports Qt 5.12.11 and GCC 13.1.0. The available local Qt kit is Qt 5.12.11 with MinGW 7.3, and the installed Qt Core/runtime compiler DLLs do not hash-match the local kit. Therefore version labels alone do not establish binary compatibility.
- Read-only: do not write QSettings, change toolbar widgets, trigger insertion, alter entities, or save/modify any DXF.
- Use a unique plugin name and DLL filename (`LibraryInsertFactorInspector`) to avoid replacing or colliding with existing plugins.
- The installed application loads plugins during startup. A restart is expected after installation; the diagnostic must not claim hot reload unless source/runtime verification establishes that capability.
- Only install after the unique DLL successfully loads in an isolated/test environment; never overwrite an existing file.
- If compatibility cannot be demonstrated or the live control cannot be located reliably, report the limitation and do not install an uncertain DLL.

## Review Focus

- Standard Insert also has a `leFactor`; test that the reader only accepts the field under `QG_LibraryInsertOptions`.
- Library Insert may be inactive and its pane may not exist; test that live state is reported unavailable, never guessed as `1`.
- A hidden or multiply-matched Library Insert pane must be reported as hidden/ambiguous; test both conditions.
- An absent registry value differs from an explicitly stored `1`; test that existence and raw value remain distinct and no default is presented as stored data.
- Angle, factor, and settings backend/path must remain separate, read-only observations; test the `10` live versus `1` persisted display and verify snapshot collection does not mutate test settings.

---

### Task 1: Read-only factor snapshot and focused tests

**Files:**
- Create: `plugins/libraryinsertfactorinspector/factorinspectorsnapshot.h`
- Create: `plugins/libraryinsertfactorinspector/factorinspectorsnapshot.cpp`
- Create: `plugins/libraryinsertfactorinspector/tests/FactorInspectorSnapshotTests.cpp`
- Create: `plugins/libraryinsertfactorinspector/tests/tests.pro`

**Interfaces:**
- Produces `enum class LiveControlState { Found, NotFound, Hidden, Ambiguous };`, `LiveControlSnapshot { LiveControlState state; QString value; }`, and `InspectorSnapshot` containing live factor/angle, persisted-key existence/raw `QVariant`, settings format, and settings location.
- Produces `InspectorSnapshot inspectLibraryInsert(QWidget *hostWindow, QSettings &settings);`.
- Identifies the form by `metaObject()->className() == "QG_LibraryInsertOptions"`, then reads its `leFactor` and `leAngle` descendants. It must not select a same-named control from another options pane.
- Reads group `LibraryInsert`, key `LibraryInsertFactor`, `QSettings::contains`, raw value, `format()`, and `fileName()`; it never calls a settings write API or substitutes a default.

- [ ] **Step 1: Write failing Qt tests** named `readsLibraryInsertPaneNotStandardInsert`, `reportsMissingAndHiddenControls`, `reportsAmbiguousVisiblePanes`, `distinguishesMissingKeyFromStoredOne`, and `snapshotDoesNotWriteSettings`. Use fake QWidget subclasses with the source class names and a temporary INI settings file; include the case live `10` versus stored `1`.
- [ ] **Step 2: Run the tests and verify the expected failures** with the available Qt 5.12.11 kit and `QT_QPA_PLATFORM=offscreen`.
- [ ] **Step 3: Implement the snapshot types and `inspectLibraryInsert(...)`** in the two production files. For multiple visible matching forms, return `Ambiguous` rather than selecting one arbitrarily; for an existing hidden form, return `Hidden`.
- [ ] **Step 4: Run the focused Qt tests** and verify all five pass, including byte-for-byte unchanged temporary settings before/after collection.
- [ ] **Step 5: Commit only Task 1 files** with message `feat: add read-only library insert factor snapshot`.

### Task 2: Diagnostic dialog and LibreCAD plugin entry

**Files:**
- Create: `plugins/libraryinsertfactorinspector/factorinspectordialog.h`
- Create: `plugins/libraryinsertfactorinspector/factorinspectordialog.cpp`
- Create: `plugins/libraryinsertfactorinspector/libraryinsertfactorinspector.h`
- Create: `plugins/libraryinsertfactorinspector/libraryinsertfactorinspector.cpp`
- Create: `plugins/libraryinsertfactorinspector/libraryinsertfactorinspector.pro`
- Create: `plugins/libraryinsertfactorinspector/libraryinsertfactorinspector.json`
- Create: `plugins/libraryinsertfactorinspector/tests/FactorInspectorDialogTests.cpp`
- Create: `plugins/libraryinsertfactorinspector/tests/dialogtests.pro`
- Create: `plugins/libraryinsertfactorinspector/tests/PluginContractTests.cpp`
- Create: `plugins/libraryinsertfactorinspector/tests/plugintests.pro`
- Modify: `plugins/plugins.pro`

**Interfaces:**
- Consumes Task 1's `InspectorSnapshot` and `inspectLibraryInsert(QWidget *, QSettings &)`.
- Produces `FactorInspectorDialog(const InspectorSnapshot &, QWidget *parent = nullptr)`; labels expose distinct live angle, live factor, persisted raw value/key-exists state, settings format, and settings location.
- Produces `LC_LibraryInsertFactorInspector : QObject, QC_PluginInterface` with the exact interface methods `name() const`, `getCapabilities() const`, and `execComm(Document_Interface *, QWidget *, QString)`.
- The plugin creates `QSettings("LibreCAD", "LibreCAD")`, obtains the host snapshot, and opens the dialog. It does not use document mutation methods or persist dialog state.

- [ ] **Step 1: Write failing dialog tests** named `showsLiveAndPersistedValuesSeparately` and `labelsUnavailableLiveValue`; assert that a snapshot containing live `10` and stored `1` renders those values distinctly and that a missing live control is not rendered as `1`.
- [ ] **Step 2: Run `dialogtests.pro` and verify the dialog tests fail** before the dialog implementation exists.
- [ ] **Step 3: Implement the read-only dialog** with stable label object names for the values/status, format, and location; provide a close button only.
- [ ] **Step 4: Run snapshot and dialog tests** and verify all pass.
- [ ] **Step 5: Write the failing plugin contract test** `registersSinglePluginsMenuAction` and the declaration-only plugin header; assert plugin name, exactly one `plugins_menu` entry, and the action label “Library Insert Factor Inspector”.
- [ ] **Step 6: Run `plugintests.pro` and verify it fails to link** because the declared plugin methods have no implementation.
- [ ] **Step 7: Implement the plugin adapter and qmake metadata** using the repository sample conventions, one `plugins_menu` action named “Library Insert Factor Inspector”, and a unique target/version yielding a unique Windows DLL; make `plugintests.pro` compile the real adapter source.
- [ ] **Step 8: Add `libraryinsertfactorinspector` to `plugins/plugins.pro`** without editing or reformatting unrelated plugin entries.
- [ ] **Step 9: Run snapshot, dialog, and plugin-contract tests plus `git diff --check`**, then commit only the listed plugin files and `plugins/plugins.pro` with message `feat: add LibreCAD library insert factor inspector plugin`.

### Task 3: Build and isolated LibreCAD loader test

**Files:**
- Create build output only in a dedicated temporary staging directory; do not copy a DLL to the installed application yet.

**Interfaces:**
- Consumes Task 2's qmake plugin target and the installed LibreCAD 2.2.0.2 application/runtime.
- Produces a uniquely named plugin DLL whose load and menu action are verified in an isolated copy of the installed application.

- [x] **Step 1: Build the plugin and tests into a dedicated staging directory** using the available Qt 5.12.11/MinGW 7.3 toolchain; record the generated DLL filename.
- [x] **Step 2: Run all Qt tests** from staging and verify PASS before attempting any host load.
- [x] **Step 3: Create an isolated copy of the installed LibreCAD runtime** in a temporary test directory, place only the new DLL in that copy's plugin directory, and leave the real installation untouched.
- [x] **Step 4: Launch the isolated host and verify plugin discovery**: the menu action appears, the dialog opens, and the process remains stable.
- [x] **Step 5: Exercise the snapshot in the isolated host** with the Library Insert pane active; verify the dialog reports the visible value, persisted-key existence/raw value, and settings backend/location separately.
- [x] **Step 6: Compare the relevant setting before and immediately after invoking the plugin**; verify the plugin itself has not changed it and do not save the test drawing.
- [x] **Step 7: Compatibility passed in the installed runtime family; source and test changes remain uncommitted because no commit was requested.**

### Task 4: Install into the user's LibreCAD and verify the reported values

**Files:**
- Install only the new, uniquely named DLL into the verified plugin directory scanned by the user's LibreCAD.
- Do not replace, rename, or remove any existing plugin, settings value, or DXF.

**Interfaces:**
- Consumes the isolated-load-tested DLL from Task 3.
- Produces a live diagnostic result in the actual LibreCAD 2.2.0.2 session for `D:\腰帶.dxf`.

- [x] **Step 1: Verify the exact production plugin path and confirm the target DLL does not already exist.** Source inspection confirmed `%USERPROFILE%\Documents\LibreCAD\plugins` is scanned on Windows startup; the target was absent.
- [x] **Step 2: Keep any existing modified LibreCAD process open and untouched.** Use the verified user-writable plugin directory and launch a separate fresh process to load the plugin.
- [x] **Step 3: Copy only the unique DLL** after the compatibility gate passed; preserve and verify its deployed SHA-256 (`3BE180C45CBF68D724EC8A4D6D531A8982B2C4A6099B26160DFB6A39F35060BA`).
- [x] **Step 4: Launch the actual installed LibreCAD, open `D:\腰帶.dxf`, activate Library Insert, and verify the toolbar shows the current live coefficient.** Open **Plugins → Library Insert Factor Inspector** and record live factor, persisted value/existence, angle, backend, and location.
- [x] **Step 5: Compare the registry value immediately before and after invoking the diagnostic** and verify the plugin made no setting or drawing changes; do not save the DXF. Leave the diagnostic/app open for the user.
- [x] **Step 6: Report the measured values and DLL path.** The protected Program Files copy was denied by Windows ACL and not bypassed; the source-confirmed per-user plugin directory was used instead.
