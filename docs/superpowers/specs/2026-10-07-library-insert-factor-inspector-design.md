# Library Insert Factor Inspector

## Goal

Provide a small read-only LibreCAD plugin that lets the user compare the value currently shown in the Library Insert options toolbar with the value persisted in LibreCAD settings. The immediate case is the user's `D:\腰帶.dxf`, where the Insert toolbar shows coefficient `10` even though the registry value previously checked was `1`.

The diagnostic must make it clear whether the `10` comes from the active UI control (runtime state) or from persisted settings. It must not silently change either value.

## User workflow

1. Open the drawing and activate the Library Insert command so its angle and factor controls are present.
2. Open **Plugins → Library Insert Factor Inspector**.
3. Read the live factor, persisted factor, and whether the live control was found and visible. The user can then compare the two values while the relevant Insert action is active.
4. Dismiss the dialog; the diagnostic has made no changes.

If the Library Insert controls are not currently instantiated or the factor control cannot be found, the dialog reports that explicitly and still displays the persisted setting. It must not represent a missing live value as `1`.

## Values and setting location

- The live value is read from the Library Insert factor widget identified in the current source as `QLineEdit` object name `leFactor`. The implementation must verify the widget belongs to the active LibreCAD window and report unavailable if that source-level assumption does not hold at runtime.
- The persisted value is read using the same Qt settings namespace and key as LibreCAD: organization `LibreCAD`, application `LibreCAD`, group `LibraryInsert`, key `LibraryInsertFactor` (the Windows registry path is `HKEY_CURRENT_USER\Software\LibreCAD\LibreCAD\LibraryInsert`, value `LibraryInsertFactor`).
- Show the raw stored value, whether the key exists, and the resolved settings backend/path when the platform API exposes it. Do not substitute the default `1.0` without labeling it as a default.
- The dialog should also report the angle value when its corresponding live control is available, as contextual information only; the factor is the primary diagnostic.

## Safety and scope

- Read-only: do not write QSettings, change toolbar widgets, trigger insertion, alter entities, or save/modify any DXF.
- Do not add a global override or force the factor to `1`; the purpose is to identify which runtime or persisted source supplies `10`.
- Do not modify the user's existing plugins or their plugin settings.
- Use a unique plugin name and DLL filename (`LibraryInsertFactorInspector`) to avoid replacing or colliding with existing plugins.

## Plugin integration

- Implement the existing `QC_PluginInterface` contract and expose one descriptive action under LibreCAD's Plugins menu.
- Follow the repository's sample plugin conventions and target the installed LibreCAD 2.2.0.2 plugin ABI.
- The installed application loads plugins during startup. A restart is expected after installation; the diagnostic must not claim hot reload unless source/runtime verification establishes that capability.
- Preserve the current document and active command state. Closing the dialog returns the user to the same drawing without having saved or altered it.

## Build, compatibility, and deployment gates

The installed LibreCAD reports Qt 5.12.11 and GCC 13.1.0. The available local Qt kit is Qt 5.12.11 with MinGW 7.3, and the installed Qt Core/runtime compiler DLLs do not hash-match the local kit. Therefore version labels alone do not establish binary compatibility.

1. Identify a toolchain and headers/libraries compatible with the installed LibreCAD plugin ABI. Build the unique plugin without touching existing plugin files.
2. First load-test it in an isolated/test copy or otherwise reversible environment. Confirm the host loads the plugin and opens the diagnostic without a crash or ABI warning.
3. Only after that gate passes, install the unique DLL into the user's LibreCAD plugin directory. Do not overwrite existing files. If compatibility cannot be demonstrated, stop before deployment and report the exact limitation.
4. Restart the actual installed LibreCAD, open `D:\腰帶.dxf`, activate Library Insert with the factor control showing `10`, and run the inspector. Record the live factor and persisted registry value shown by the plugin.
5. Verify that the diagnostic did not change the drawing or settings: compare the relevant setting before/after and do not save the test drawing. Report the observed values and any limitation if the live control is inaccessible in this UI state.

## Rollback

The plugin is isolated in its own uniquely named DLL. If the host fails to load it or the user wants it removed, close LibreCAD and remove only that DLL from the plugin directory; do not alter any other plugin, settings, registry entry, or drawing.

## Acceptance criteria

- The plugin appears in the Plugins menu after a restart and opens a diagnostic dialog.
- With the Library Insert toolbar active, the dialog accurately reports the visible live coefficient and the persisted setting as distinct fields; for the current investigation, it can reveal the expected discrepancy `10` versus `1` if those are the values present at test time.
- If the live control cannot be queried, the dialog clearly says so rather than presenting a guessed value.
- The dialog is read-only; no DXF or settings change occurs.
- The DLL is deployed only after compatibility is demonstrated, and no existing plugin is overwritten.
