# DXF 尺寸解析分頁 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在長袖 DXF GUI 增加資料夾解析分頁，呈現袖長與袖口平量，並以批名將各尺寸合併寫入目前尺寸 JSON。

**Architecture:** 保留現有尺寸辨識、DXF 袖片分析與 `SizeTable` 持久化；新增資料夾解析資料模型與通用命名款式合併入口。GUI 新分頁獨立保存解析狀態，解析期間不寫 JSON，使用者按下寫入後才保存。

**Tech Stack:** Python 3.10、Tkinter、現有標準函式庫 DXF 讀取器。

**Spec:** `docs/superpowers/specs/2026-11-16-dxf-size-parser-design.md`

## Global Constraints

- 遞迴掃描選取資料夾及其所有子資料夾中的 `.dxf` 檔案。
- 袖口平量等於袖片分析所得袖口開口寬的一半；JSON `cuff_width_mm` 寫完整開口寬。
- 只分析檔名含「袖」且不含「袖口」、也不以 `#` 開頭的 DXF，尺寸由檔名判斷，不要求尺寸資料夾；未辨識尺寸或無法分析的 DXF 不得寫入 JSON；同尺寸多個標準袖片時，該尺寸不得寫入。
- 保留既有尺寸、款式、預設值、備份與驗證行為。
- 保留 `tools/sleeve_gui.py` 既有未提交的 DXF 單位修正。
- 不新增或執行測試；完成後只做語法解析與 `git diff --check`。

## Review Focus

- DXF 資料夾含有多種衣片與副本時，只分析符合標準檔名的袖片，再接受 `find_sleeve` 判定的袖片結果；尺寸只由檔名判斷。
- 檔名沒有尺寸標記或含多個尺寸標記時，不可猜測尺寸或寫入。
- 同尺寸多個 DXF 時，該尺寸不可因排序而任意選一件。
- 自訂 JSON 尺寸與標準尺寸別名（例如 `2L`／`2XL`）應沿用現有正規化。
- 來源單位代碼需由既有 `read_dxf` 換算成 mm；顯示表格和 JSON 數值均為 mm。

---

### Task 1: DXF measurements and named merge helper

**Files:**
- Modify: `tools/sleeve_gui.py`

**Interfaces:**
- Add `DxfSizeValues(sleeve_length_mm: float, cuff_flat_width_mm: float, cuff_width_mm: float)`.
- Add `read_dxf_size_values_from_folder(folder: Path, available_sizes: Sequence[str]) -> Tuple[Dict[str, DxfSizeValues], List[str]]`.
- Add `merge_named_size_values(table: SizeTable, values: Mapping[str, Tuple[float, float]], batch_name: str) -> Tuple[SizeTable, int, int]`; each tuple is `(sleeve_length_mm, cuff_width_mm)`.
- Keep `merge_excel_size_values` behavior by delegating to the shared merge helper.

- [x] Add the DXF result dataclass and folder parser. Recursively scan sorted `.dxf` files; retain names containing `袖` but not `袖口` and not starting with `#`, detect size from the filename without requiring a matching size folder, call `read_dxf`/`find_sleeve`, calculate flat width as `analysis.cuff_width_mm / 2`, and return full width for JSON. Record per-file relative paths/errors; if a size has multiple standard valid sleeves, remove it from mergeable results and record the duplicate.
- [x] Extract the existing add/update-by-batch-name logic into `merge_named_size_values`; preserve case-insensitive variant matching, existing default variants, and new-size behavior.
- [x] Adapt `merge_excel_size_values` to delegate through `(sleeve_length_mm, cuff_width_mm)` pairs without changing its visible behavior.

### Task 2: DXF parser GUI tab and JSON action

**Files:**
- Modify: `tools/sleeve_gui.py`

**Interfaces:**
- Add a third Notebook page named `DXF 尺寸解析`.
- Keep parser-specific folder, batch-name, results, and tree state separate from Excel batch state.

- [x] Add a folder field, batch-name field, current JSON path, four-column measurements table matching the reference image, and a results log; accept dropped folders and start recursive parsing.
- [x] Add `解析 DXF 資料夾` to populate the table and list skipped/failed/duplicate files without writing JSON.
- [x] Add `合併寫入 JSON` to validate the batch name, merge only unique valid sizes, save with the existing `SizeTable.save()` backup/validation flow, and refresh the displayed JSON path/status.

### Task 3: User documentation and non-test verification

**Files:**
- Modify: `tools/SLEEVE_GUI.md`

- [x] Document recursive folder parsing, sleeve-name filtering, filename-based size detection, flat-width conversion, duplicate/error behavior, and explicit JSON write action.
- [x] Run `python -c "import ast, pathlib; ast.parse(pathlib.Path('tools/sleeve_gui.py').read_text(encoding='utf-8'))"`.
- [x] Run `git diff --check` and inspect the final diff to confirm the unit-preservation changes remain present.
