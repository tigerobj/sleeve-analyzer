#!/usr/bin/env python3
"""Standalone Python GUI for generating a long-sleeve DXF.

The original LibreCAD plugin remains unchanged.  This module provides the
same practical workflow without requiring LibreCAD to be running:

    python sleeve_gui.py input.dxf output.dxf --sleeve-length 627 \
        --cuff-width 225.17

The DXF reader and writer use only the standard library.  If the optional
tkinterdnd2 package is installed, the GUI also accepts Explorer drag-and-drop
for source DXF files.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
import posixpath
import re
import shutil
import tempfile
import tkinter as tk
import zipfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from tkinter import filedialog, messagebox, simpledialog, ttk
from typing import Any, Dict, Iterable, List, Mapping, Optional, Sequence, Tuple

try:
    import winreg
except ImportError:  # Keep the geometry and CLI usable outside Windows.
    winreg = None

try:
    from tkinterdnd2 import DND_FILES, TkinterDnD
    GUI_BASE = TkinterDnD.Tk
except ImportError:
    # The browse button still works when the optional drag-and-drop package
    # is not installed.
    DND_FILES = None
    GUI_BASE = tk.Tk


EPS = 1.0e-9
PI = math.pi
DEFAULT_SEAM_ALLOWANCE_MM = 7.9375
DEFAULT_CUFF_FOLD_DISTANCE_MM = 19.05

Point = Tuple[float, float]


SIZE_ORDER = ("12", "XS", "S", "M", "L", "XL", "2L", "3L", "4L",
              "5L", "6L")
SIZE_DATA_FILENAME = "sleeve_sizes.json"
SIZE_DATA_REGISTRY_SUBKEY = r"Software\LibreCAD\SleeveAnalyzer"
SIZE_DATA_REGISTRY_VALUE = "SizeDataPath"
SIZE_DATA_REGISTRY_DISPLAY = (
    r"HKEY_CURRENT_USER\Software\LibreCAD\SleeveAnalyzer\SizeDataPath")
ROOT_METADATA_KEYS = frozenset({"meta", "metadata", "version",
                                "schema_version"})


class SizeDataError(ValueError):
    """尺寸資料無法讀取、驗證或操作時使用的錯誤。"""


class SizeDataConflictError(SizeDataError):
    """尺寸 JSON 在本次載入後被其他程式修改。"""


@dataclass(frozen=True)
class SizeDetection:
    status: str
    size: Optional[str]
    candidates: Tuple[str, ...]
    reason: str

    @property
    def recognized(self) -> bool:
        return self.status == "recognized" and self.size is not None


@dataclass(frozen=True)
class EffectiveSizeValues:
    sleeve_length_mm: float
    cuff_width_mm: float
    cuff_source: str
    stored_cuff_width_mm: float


@dataclass(frozen=True)
class SleeveParameters:
    sleeve_length_mm: float
    cuff_width_mm: float
    seam_allowance_mm: float
    cuff_fold_distance_mm: float
    mirror_join: bool = False


@dataclass(frozen=True)
class ExcelSizeValues:
    sleeve_length_mm: float
    cuff_flat_width_mm: float
    cuff_width_mm: float


def default_size_data_path() -> Path:
    """Return the plugin JSON path relative to this script, not cwd."""
    return (Path(__file__).resolve().parent.parent / "plugins" /
            "sleeveanalyzer" / SIZE_DATA_FILENAME)


def registered_size_data_path() -> Optional[Path]:
    """Read the active size JSON path from the current user's registry."""
    if winreg is None:
        return None
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                            SIZE_DATA_REGISTRY_SUBKEY, 0,
                            winreg.KEY_READ) as key:
            value, value_type = winreg.QueryValueEx(
                key, SIZE_DATA_REGISTRY_VALUE)
    except FileNotFoundError:
        return None
    except OSError as error:
        raise SizeDataError(
            f"無法讀取 Windows 登錄檔：{SIZE_DATA_REGISTRY_DISPLAY}\n"
            f"原因：{error}") from error
    if value_type not in (winreg.REG_SZ, winreg.REG_EXPAND_SZ):
        raise SizeDataError(
            f"登錄值格式不正確：{SIZE_DATA_REGISTRY_DISPLAY}")
    if not isinstance(value, str) or not value.strip():
        raise SizeDataError(
            f"登錄值沒有有效的尺寸資料路徑：{SIZE_DATA_REGISTRY_DISPLAY}")
    return Path(os.path.expandvars(value.strip())).expanduser()


def register_size_data_path(path: Path) -> None:
    """Store the selected size JSON path in the current user's registry."""
    if winreg is None:
        raise SizeDataError("此功能需要 Windows 登錄檔。")
    selected_path = str(Path(path).expanduser().resolve())
    try:
        with winreg.CreateKeyEx(
                winreg.HKEY_CURRENT_USER, SIZE_DATA_REGISTRY_SUBKEY, 0,
                winreg.KEY_SET_VALUE) as key:
            winreg.SetValueEx(key, SIZE_DATA_REGISTRY_VALUE, 0,
                              winreg.REG_SZ, selected_path)
    except OSError as error:
        raise SizeDataError(
            f"無法寫入 Windows 登錄檔：{SIZE_DATA_REGISTRY_DISPLAY}\n"
            f"原因：{error}") from error


def load_active_size_table() -> SizeTable:
    """Load the registered JSON file, or the bundled default if unregistered."""
    path = registered_size_data_path() or default_size_data_path()
    return load_size_table(path)


def _canonical_size_name(name: str) -> str:
    normalized = name.strip().upper()
    return re.sub(r"^([2-6])XL$", r"\1L", normalized)


def size_sort_key(name: str) -> Tuple[int, str]:
    canonical = _canonical_size_name(name)
    try:
        return (SIZE_ORDER.index(canonical), "")
    except ValueError:
        return (len(SIZE_ORDER), canonical.casefold())


def detect_size_from_filename(filename: str,
                              available_sizes: Optional[Sequence[str]] = None
                              ) -> SizeDetection:
    """Detect one independent size token from a DXF filename.

    Alphanumeric runs are tokens, so Chinese characters, hyphens, underscores
    and spaces all act as separators.  This prevents ``S`` matching ``XS``,
    ``L`` matching ``XL`` and digits inside a part number from becoming size
    ``12``.
    """
    available = tuple(available_sizes or SIZE_ORDER)
    lookup = {_canonical_size_name(size): size for size in available}
    tokens = re.findall(r"[A-Za-z0-9]+", Path(str(filename)).stem)
    matches = []
    for token in tokens:
        canonical = _canonical_size_name(token)
        if canonical in lookup:
            matches.append(lookup[canonical])
    unique_matches = tuple(dict.fromkeys(matches))
    if len(unique_matches) == 1:
        return SizeDetection("recognized", unique_matches[0], unique_matches,
                             f"已從檔名辨識尺寸：{unique_matches[0]}")
    if len(unique_matches) > 1:
        labels = "、".join(unique_matches)
        return SizeDetection("conflict", None, unique_matches,
                             f"檔名含有多個尺寸標記（{labels}），請手動選擇尺寸。")
    return SizeDetection("unknown", None, (),
                         "無法從檔名辨識獨立尺寸標記，請手動選擇尺寸。")


def _numeric_value(value: Any, label: str, allow_zero: bool = False) -> float:
    if isinstance(value, bool):
        raise SizeDataError(f"{label} 必須是有限數字。")
    try:
        number = float(value)
    except (TypeError, ValueError) as error:
        raise SizeDataError(f"{label} 必須是有限數字。") from error
    minimum = 0.0 if allow_zero else EPS
    if not math.isfinite(number) or number < minimum:
        qualifier = "不可小於 0" if allow_zero else "必須大於 0"
        raise SizeDataError(f"{label} 必須是有限數字，且{qualifier}。")
    return number


def validate_size_data(data: Mapping[str, Any],
                       allow_legacy_zero: bool = True) -> None:
    """Validate the complete size schema without normalizing or mutating it."""
    if not isinstance(data, Mapping) or not data:
        raise SizeDataError("尺寸資料必須包含至少一個尺寸。")
    folded_size_names = set()
    size_count = 0
    for size_name, size_record in data.items():
        if not isinstance(size_name, str) or not size_name.strip():
            raise SizeDataError("尺寸名稱不可空白。")
        if size_name.startswith("_") or size_name.casefold() in ROOT_METADATA_KEYS:
            continue
        folded_size_name = size_name.casefold()
        if folded_size_name in folded_size_names:
            raise SizeDataError(f"有重複尺寸名稱：{size_name}。")
        folded_size_names.add(folded_size_name)
        size_count += 1
        if not isinstance(size_record, Mapping):
            raise SizeDataError(f"尺寸 {size_name} 的資料必須是物件。")
        default = size_record.get("default")
        variants = size_record.get("variants")
        if not isinstance(default, str) or not default.strip():
            raise SizeDataError(f"尺寸 {size_name} 的 default 不可空白。")
        if not isinstance(variants, Mapping) or not variants:
            raise SizeDataError(f"尺寸 {size_name} 至少需要一款袖型。")
        folded_names = set()
        for variant_name, values in variants.items():
            if not isinstance(variant_name, str) or not variant_name.strip():
                raise SizeDataError(f"尺寸 {size_name} 的款式名稱不可空白。")
            folded_name = variant_name.casefold()
            if folded_name in folded_names:
                raise SizeDataError(f"尺寸 {size_name} 有重複款式名稱：{variant_name}。")
            folded_names.add(folded_name)
            if not isinstance(values, Mapping):
                raise SizeDataError(f"{size_name}/{variant_name} 的資料必須是物件。")
            if "sleeve_length_mm" not in values:
                raise SizeDataError(f"{size_name}/{variant_name} 缺少袖長。")
            if "cuff_width_mm" not in values:
                raise SizeDataError(f"{size_name}/{variant_name} 缺少袖口寬。")
            _numeric_value(values["sleeve_length_mm"],
                           f"{size_name}/{variant_name} 袖長")
            _numeric_value(values["cuff_width_mm"],
                           f"{size_name}/{variant_name} 袖口寬",
                           allow_zero=allow_legacy_zero)
        if default not in variants:
            raise SizeDataError(f"尺寸 {size_name} 的 default 款式不存在：{default}。")
    if size_count == 0:
        raise SizeDataError("尺寸資料必須包含至少一個尺寸。")


def _file_signature(path: Path) -> Optional[Tuple[int, int, str]]:
    try:
        raw = path.read_bytes()
        stat = path.stat()
    except FileNotFoundError:
        return None
    return stat.st_mtime_ns, stat.st_size, hashlib.sha256(raw).hexdigest()


class SizeTable:
    """Centralized size JSON loading, validation, CRUD and safe persistence."""

    def __init__(self, path: Optional[Path] = None) -> None:
        self.path = Path(path) if path is not None else default_size_data_path()
        self._data: Dict[str, Any] = {}
        self._loaded_signature: Optional[Tuple[int, int, str]] = None
        self._dirty = False
        self.reload()

    @classmethod
    def from_dict(cls, path: Path, data: Mapping[str, Any]) -> "SizeTable":
        validate_size_data(data)
        table = cls.__new__(cls)
        table.path = Path(path)
        table._data = copy.deepcopy(dict(data))
        table._loaded_signature = _file_signature(table.path)
        table._dirty = False
        return table

    def _read_disk(self) -> Tuple[Dict[str, Any], Optional[Tuple[int, int, str]]]:
        try:
            raw = self.path.read_text(encoding="utf-8-sig")
        except OSError as error:
            raise SizeDataError(
                f"尺寸資料無法讀取：{self.path}\n原因：{error}") from error
        try:
            data = json.loads(raw)
        except json.JSONDecodeError as error:
            raise SizeDataError(
                f"尺寸資料 JSON 損壞：{self.path}\n原因：{error}") from error
        try:
            validate_size_data(data)
        except SizeDataError as error:
            raise SizeDataError(
                f"尺寸資料驗證失敗：{self.path}\n原因：{error}") from error
        return copy.deepcopy(data), _file_signature(self.path)

    def reload(self) -> None:
        data, signature = self._read_disk()
        self._data = data
        self._loaded_signature = signature
        self._dirty = False

    @property
    def dirty(self) -> bool:
        return self._dirty

    def clone(self) -> "SizeTable":
        clone = self.__class__.__new__(self.__class__)
        clone.path = self.path
        clone._data = copy.deepcopy(self._data)
        clone._loaded_signature = self._loaded_signature
        clone._dirty = self._dirty
        return clone

    def to_dict(self) -> Dict[str, Any]:
        return copy.deepcopy(self._data)

    def sizes(self) -> Tuple[str, ...]:
        return tuple(sorted(
            (name for name, record in self._data.items()
             if not name.startswith("_")
             and name.casefold() not in ROOT_METADATA_KEYS
             and isinstance(record, Mapping)
             and "default" in record and "variants" in record),
            key=size_sort_key))

    def _size(self, size: str) -> Dict[str, Any]:
        try:
            record = self._data[size]
        except KeyError as error:
            raise SizeDataError(f"找不到尺寸：{size}。") from error
        return record

    def variants(self, size: str) -> Tuple[str, ...]:
        return tuple(self._size(size)["variants"].keys())

    def default_variant(self, size: str) -> str:
        return str(self._size(size)["default"])

    def _variant(self, size: str, variant: str) -> Dict[str, Any]:
        variants = self._size(size)["variants"]
        try:
            return variants[variant]
        except KeyError as error:
            raise SizeDataError(f"找不到款式：{size}/{variant}。") from error

    @staticmethod
    def _ensure_new_name(name: str, label: str,
                         mapping: Mapping[str, Any],
                         exclude: Optional[str] = None) -> str:
        if not isinstance(name, str) or not name.strip():
            raise SizeDataError(f"{label}不可空白。")
        folded = name.casefold()
        for existing in mapping:
            if existing != exclude and existing.casefold() == folded:
                raise SizeDataError(f"{label}已存在：{name}。")
        return name.strip()

    def effective_values(self, size: str, variant: str) -> EffectiveSizeValues:
        values = self._variant(size, variant)
        sleeve_length = _numeric_value(values["sleeve_length_mm"], "袖長")
        stored_cuff = _numeric_value(values["cuff_width_mm"], "袖口寬",
                                     allow_zero=True)
        if stored_cuff > EPS:
            return EffectiveSizeValues(sleeve_length, stored_cuff, "款式",
                                       stored_cuff)
        standard = self._size(size)["variants"].get("standard")
        if isinstance(standard, Mapping):
            standard_cuff = _numeric_value(
                standard.get("cuff_width_mm"), "standard 袖口寬",
                allow_zero=True)
            if standard_cuff > EPS:
                return EffectiveSizeValues(sleeve_length, standard_cuff,
                                           "同尺寸 standard", stored_cuff)
        raise SizeDataError(
            f"{size}/{variant} 袖口寬為 0，且同尺寸 standard 沒有有效正值。")

    def _set_values(self, size: str, variant: str,
                    sleeve_length_mm: Any, cuff_width_mm: Any,
                    allow_legacy_zero: bool = False) -> None:
        length = _numeric_value(sleeve_length_mm, "袖長")
        values = self._variant(size, variant)
        if (allow_legacy_zero and values.get("cuff_width_mm") == 0
                and cuff_width_mm == 0):
            cuff = 0.0
        else:
            cuff = _numeric_value(cuff_width_mm, "袖口寬")
        values["sleeve_length_mm"] = length
        values["cuff_width_mm"] = cuff
        self._dirty = True

    def add_size(self, size: str, sleeve_length_mm: Any,
                 cuff_width_mm: Any, default_variant: str = "standard") -> None:
        name = self._ensure_new_name(size, "尺寸名稱", self._data)
        variant = self._ensure_new_name(default_variant, "款式名稱", {})
        length = _numeric_value(sleeve_length_mm, "袖長")
        cuff = _numeric_value(cuff_width_mm, "袖口寬")
        self._data[name] = {
            "default": variant,
            "variants": {variant: {
                "sleeve_length_mm": length,
                "cuff_width_mm": cuff,
            }},
        }
        self._dirty = True

    def copy_size(self, source: str, new_size: str) -> None:
        name = self._ensure_new_name(new_size, "尺寸名稱", self._data)
        self._data[name] = copy.deepcopy(self._size(source))
        self._dirty = True

    def rename_size(self, old_size: str, new_size: str) -> None:
        name = self._ensure_new_name(new_size, "尺寸名稱", self._data,
                                     exclude=old_size)
        if old_size not in self._data:
            raise SizeDataError(f"找不到尺寸：{old_size}。")
        record = self._data.pop(old_size)
        self._data[name] = record
        self._dirty = True

    def delete_size(self, size: str) -> None:
        if len(self.sizes()) <= 1:
            raise SizeDataError("至少需要保留一個尺寸。")
        self._size(size)
        del self._data[size]
        self._dirty = True

    def add_variant(self, size: str, variant: str,
                    sleeve_length_mm: Any, cuff_width_mm: Any) -> None:
        variants = self._size(size)["variants"]
        name = self._ensure_new_name(variant, "款式名稱", variants)
        length = _numeric_value(sleeve_length_mm, "袖長")
        cuff = _numeric_value(cuff_width_mm, "袖口寬")
        variants[name] = {"sleeve_length_mm": length,
                          "cuff_width_mm": cuff}
        self._dirty = True

    def copy_variant(self, size: str, source: str, new_variant: str,
                     sleeve_length_mm: Optional[Any] = None,
                     cuff_width_mm: Optional[Any] = None) -> None:
        variants = self._size(size)["variants"]
        name = self._ensure_new_name(new_variant, "款式名稱", variants)
        source_values = self._variant(size, source)
        effective = self.effective_values(size, source)
        copied = copy.deepcopy(source_values)
        copied["sleeve_length_mm"] = (effective.sleeve_length_mm
                                       if sleeve_length_mm is None
                                       else _numeric_value(sleeve_length_mm,
                                                           "袖長"))
        copied["cuff_width_mm"] = (effective.cuff_width_mm
                                    if cuff_width_mm is None
                                    else _numeric_value(cuff_width_mm,
                                                        "袖口寬"))
        variants[name] = copied
        self._dirty = True

    def rename_variant(self, size: str, old_variant: str,
                       new_variant: str) -> None:
        variants = self._size(size)["variants"]
        name = self._ensure_new_name(new_variant, "款式名稱", variants,
                                     exclude=old_variant)
        if old_variant not in variants:
            raise SizeDataError(f"找不到款式：{size}/{old_variant}。")
        values = variants.pop(old_variant)
        variants[name] = values
        if self._size(size)["default"] == old_variant:
            self._size(size)["default"] = name
        self._dirty = True

    def update_variant(self, size: str, variant: str,
                       sleeve_length_mm: Any, cuff_width_mm: Any,
                       allow_legacy_zero: bool = False) -> None:
        self._set_values(size, variant, sleeve_length_mm, cuff_width_mm,
                         allow_legacy_zero)

    def set_default(self, size: str, variant: str) -> None:
        self._variant(size, variant)
        self._size(size)["default"] = variant
        self._dirty = True

    def delete_variant(self, size: str, variant: str,
                       replacement: Optional[str] = None) -> None:
        record = self._size(size)
        variants = record["variants"]
        self._variant(size, variant)
        if len(variants) <= 1:
            raise SizeDataError("每個尺寸至少需要保留一款袖型。")
        if record["default"] == variant:
            if not replacement or replacement == variant:
                raise SizeDataError("刪除預設款前必須指定替代款式。")
            if replacement not in variants:
                raise SizeDataError(f"替代款式不存在：{replacement}。")
            record["default"] = replacement
        del variants[variant]
        self._dirty = True

    def save(self) -> None:
        try:
            validate_size_data(self._data)
        except SizeDataError:
            raise
        current_signature = _file_signature(self.path)
        if current_signature != self._loaded_signature:
            raise SizeDataConflictError(
                f"尺寸資料已被外部修改，為避免覆蓋更新而停止儲存：{self.path}")
        backup = Path(str(self.path) + ".bak")
        temporary_path: Optional[Path] = None
        try:
            shutil.copy2(self.path, backup)
            descriptor, temporary_name = tempfile.mkstemp(
                prefix=f".{self.path.name}.", suffix=".tmp",
                dir=str(self.path.parent), text=True)
            temporary_path = Path(temporary_name)
            with os.fdopen(descriptor, "w", encoding="utf-8",
                            newline="\n") as stream:
                json.dump(self._data, stream, ensure_ascii=False, indent=2)
                stream.write("\n")
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary_path, self.path)
            temporary_path = None
        except SizeDataError:
            raise
        except OSError as error:
            raise SizeDataError(f"無法安全儲存尺寸資料：{self.path}\n原因：{error}") from error
        finally:
            if temporary_path is not None:
                try:
                    temporary_path.unlink()
                except OSError:
                    pass
        self._loaded_signature = _file_signature(self.path)
        self._dirty = False


def load_size_table(path: Optional[Path] = None) -> SizeTable:
    return SizeTable(path)


def _xlsx_shared_strings(archive: zipfile.ZipFile) -> List[str]:
    namespace = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"
    try:
        root = ET.fromstring(archive.read("xl/sharedStrings.xml"))
    except KeyError:
        return []
    return ["".join(node.text or "" for node in item.iter(f"{namespace}t"))
            for item in root.findall(f"{namespace}si")]


def _xlsx_cell_column(reference: str) -> int:
    letters = re.match(r"[A-Za-z]+", reference)
    if letters is None:
        raise SizeDataError(f"Excel 儲存格位置格式錯誤：{reference}")
    column = 0
    for character in letters.group(0).upper():
        column = column * 26 + ord(character) - ord("A") + 1
    return column - 1


def _read_xlsx_sheets(path: Path
                      ) -> List[Tuple[str, List[Tuple[int, Dict[int, str]]]]]:
    main_ns = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"
    office_rel_ns = "{http://schemas.openxmlformats.org/officeDocument/2006/relationships}"
    package_rel_ns = "{http://schemas.openxmlformats.org/package/2006/relationships}"
    try:
        with zipfile.ZipFile(path) as archive:
            workbook = ET.fromstring(archive.read("xl/workbook.xml"))
            relationships = ET.fromstring(
                archive.read("xl/_rels/workbook.xml.rels"))
            targets = {
                relation.get("Id", ""): relation.get("Target", "")
                for relation in relationships.findall(f"{package_rel_ns}Relationship")
            }
            shared_strings = _xlsx_shared_strings(archive)
            sheets = []
            for sheet in workbook.findall(
                    f"{main_ns}sheets/{main_ns}sheet"):
                name = sheet.get("name", "工作表")
                relation_id = sheet.get(f"{office_rel_ns}id", "")
                target = targets.get(relation_id)
                if not target:
                    continue
                worksheet_path = (target.lstrip("/") if target.startswith("/")
                                  else posixpath.normpath(
                                      posixpath.join("xl", target)))
                worksheet = ET.fromstring(archive.read(worksheet_path))
                rows = []
                sheet_data = worksheet.find(f"{main_ns}sheetData")
                if sheet_data is None:
                    continue
                for row_index, row in enumerate(
                        sheet_data.findall(f"{main_ns}row"), start=1):
                    values: Dict[int, str] = {}
                    formulas: Dict[int, str] = {}
                    for cell in row.findall(f"{main_ns}c"):
                        column = _xlsx_cell_column(cell.get("r", ""))
                        cell_type = cell.get("t", "")
                        if cell_type == "inlineStr":
                            value = "".join(
                                node.text or ""
                                for node in cell.iter(f"{main_ns}t"))
                        else:
                            value_node = cell.find(f"{main_ns}v")
                            value = value_node.text if value_node is not None else ""
                            if cell_type == "s" and value:
                                try:
                                    value = shared_strings[int(value)]
                                except (ValueError, IndexError) as error:
                                    raise SizeDataError(
                                        f"Excel 共用字串索引錯誤：{value}") from error
                        values[column] = value or ""
                        formula_node = cell.find(f"{main_ns}f")
                        if formula_node is not None and formula_node.text:
                            formulas[column] = formula_node.text
                    for column, formula in formulas.items():
                        if values.get(column):
                            continue
                        match = re.fullmatch(
                            r"\s*\$?([A-Za-z]+)\$?\d+\s*\*\s*"
                            r"([+-]?(?:\d+(?:\.\d*)?|\.\d+))\s*",
                            formula)
                        if match is None:
                            continue
                        reference_column = _xlsx_cell_column(match.group(1))
                        try:
                            values[column] = str(
                                float(values.get(reference_column, ""))
                                * float(match.group(2)))
                        except (TypeError, ValueError):
                            continue
                    row_number = int(row.get("r", row_index))
                    rows.append((row_number, values))
                sheets.append((name, rows))
            if not sheets:
                raise SizeDataError("Excel 檔案中沒有可讀取的工作表。")
            return sheets
    except SizeDataError:
        raise
    except (OSError, zipfile.BadZipFile, KeyError, ET.ParseError) as error:
        raise SizeDataError(
            f"無法讀取 Excel 檔案：{path}\n"
            "請選擇有效的 .xlsx 或 .xlsm 檔案。\n"
            f"原因：{error}") from error


def _normalize_excel_header(value: str) -> str:
    normalized = str(value).strip().casefold().replace("㎜", "mm")
    return re.sub(r"[\s_()（）\-－]+", "", normalized)


def _excel_size_name(value: str) -> str:
    name = str(value).strip()
    if not name:
        return ""
    if re.fullmatch(r"\d+\.0+", name):
        name = name.split(".", 1)[0]
    return name.upper()


def validate_batch_name(value: str) -> str:
    name = str(value).strip()
    invalid_characters = re.search(r'[<>:"/\\|?*]', name) is not None
    reserved_names = {"CON", "PRN", "AUX", "NUL"} | {
        f"COM{number}" for number in range(1, 10)} | {
        f"LPT{number}" for number in range(1, 10)}
    if (not name or invalid_characters or name.endswith((".", " "))
            or name.upper() in reserved_names):
        raise SizeDataError(
            "名稱不可空白、包含 Windows 禁用字元，或以句點／空白結尾。")
    return name


def _excel_mm_value(cells: Mapping[int, str], mm_column: Optional[int],
                    cm_column: Optional[int], label: str,
                    row_number: int) -> float:
    if mm_column is not None:
        value = cells.get(mm_column, "").strip()
        if value:
            return _numeric_value(value, f"第 {row_number} 列 {label}（mm）")
    if cm_column is not None:
        value = cells.get(cm_column, "").strip()
        if value:
            return _numeric_value(value, f"第 {row_number} 列 {label}（cm）") * 10.0
    raise SizeDataError(f"第 {row_number} 列缺少 {label}（mm 或 cm）數值。")


def read_size_values_from_excel(path: Path
                                ) -> Dict[str, ExcelSizeValues]:
    """Read size, sleeve length and flat cuff width from XLSX/XLSM."""
    size_headers = {"新尺寸", "尺寸", "尺寸名稱", "尺碼", "尺碼名稱",
                    "尺码", "尺码名称", "size", "sizename"}
    sleeve_mm_headers = {"袖長mm", "袖长mm", "sleevelengthmm"}
    sleeve_cm_headers = {"袖長cm", "袖长cm", "sleevelengthcm"}
    cuff_mm_headers = {"袖口寬平量mm", "袖口宽平量mm", "cuffwidthflatmm"}
    cuff_cm_headers = {"袖口寬平量cm", "袖口宽平量cm", "cuffwidthflatcm"}
    normalize = lambda names: {_normalize_excel_header(name) for name in names}
    size_headers = normalize(size_headers)
    sleeve_mm_headers = normalize(sleeve_mm_headers)
    sleeve_cm_headers = normalize(sleeve_cm_headers)
    cuff_mm_headers = normalize(cuff_mm_headers)
    cuff_cm_headers = normalize(cuff_cm_headers)

    for sheet_name, rows in _read_xlsx_sheets(path):
        header_row_index = None
        size_column = None
        sleeve_mm_column = sleeve_cm_column = None
        cuff_mm_column = cuff_cm_column = None
        for row_index, (_row_number, cells) in enumerate(rows[:50]):
            headers = {column: _normalize_excel_header(value)
                       for column, value in cells.items()}
            found_size = next((column for column, name in headers.items()
                               if name in size_headers), None)
            found_sleeve_mm = next((column for column, name in headers.items()
                                    if name in sleeve_mm_headers), None)
            found_sleeve_cm = next((column for column, name in headers.items()
                                    if name in sleeve_cm_headers), None)
            found_cuff_mm = next((column for column, name in headers.items()
                                  if name in cuff_mm_headers), None)
            found_cuff_cm = next((column for column, name in headers.items()
                                  if name in cuff_cm_headers), None)
            if (found_size is not None
                    and (found_sleeve_mm is not None
                         or found_sleeve_cm is not None)
                    and (found_cuff_mm is not None
                         or found_cuff_cm is not None)):
                header_row_index = row_index
                size_column = found_size
                sleeve_mm_column = found_sleeve_mm
                sleeve_cm_column = found_sleeve_cm
                cuff_mm_column = found_cuff_mm
                cuff_cm_column = found_cuff_cm
                break
        if header_row_index is None or size_column is None:
            continue

        sizes: Dict[str, ExcelSizeValues] = {}
        seen_canonical = set()
        for row_number, cells in rows[header_row_index + 1:]:
            name = _excel_size_name(cells.get(size_column, ""))
            has_measurement = any(
                cells.get(column, "").strip()
                for column in (sleeve_mm_column, sleeve_cm_column,
                               cuff_mm_column, cuff_cm_column)
                if column is not None)
            if not has_measurement:
                continue
            if not name:
                raise SizeDataError(
                    f"工作表「{sheet_name}」第 {row_number} 列有尺寸數值，但尺寸名稱為空。")
            canonical = _canonical_size_name(name)
            if canonical in seen_canonical:
                raise SizeDataError(f"Excel 尺寸重複：{name}。")
            sleeve_length = _excel_mm_value(
                cells, sleeve_mm_column, sleeve_cm_column, "袖長", row_number)
            flat_width = _excel_mm_value(
                cells, cuff_mm_column, cuff_cm_column,
                "袖口寬平量", row_number)
            sizes[name] = ExcelSizeValues(
                sleeve_length, flat_width, flat_width * 2.0)
            seen_canonical.add(canonical)
        if not sizes:
            raise SizeDataError(
                f"工作表「{sheet_name}」找到尺寸與尺寸欄位，但沒有有效資料列。")
        return dict(sorted(sizes.items(), key=lambda item: size_sort_key(item[0])))
    raise SizeDataError(
        "找不到包含新尺寸、袖長(mm 或 cm)、袖口寬平量(mm 或 cm)的工作表。")


def merge_excel_size_values(table: SizeTable,
                            excel_sizes: Mapping[str, ExcelSizeValues],
                            batch_name: str
                            ) -> Tuple[SizeTable, int, int]:
    """Merge workbook values into one named variant per size."""
    variant_name = validate_batch_name(batch_name)
    merged = table.clone()
    existing = {_canonical_size_name(size): size for size in merged.sizes()}
    added = updated = 0
    for name, values in excel_sizes.items():
        canonical = _canonical_size_name(name)
        target = existing.get(canonical)
        if target is None:
            merged.add_size(name, values.sleeve_length_mm,
                            values.cuff_width_mm,
                            default_variant=variant_name)
            existing[canonical] = name
            added += 1
            continue
        variants = merged.variants(target)
        matching_variant = next(
            (variant for variant in variants
             if variant.casefold() == variant_name.casefold()), None)
        if matching_variant is None:
            merged.add_variant(target, variant_name, values.sleeve_length_mm,
                               values.cuff_width_mm)
            added += 1
        else:
            merged.update_variant(target, matching_variant,
                                  values.sleeve_length_mm,
                                  values.cuff_width_mm)
            updated += 1
    return merged, added, updated


def delete_named_variant(table: SizeTable, batch_name: str
                         ) -> Tuple[SizeTable, int, int]:
    """Remove a named batch variant while preserving other size variants."""
    variant_name = validate_batch_name(batch_name)
    updated_table = table.clone()
    matches = []
    for size in updated_table.sizes():
        match = next((variant for variant in updated_table.variants(size)
                      if variant.casefold() == variant_name.casefold()), None)
        if match is not None:
            matches.append((size, match))
    if not matches:
        raise SizeDataError(f"尺寸 JSON 中沒有「{variant_name}」款式。")

    sizes_to_delete = [size for size, variant in matches
                       if len(updated_table.variants(size)) == 1]
    if len(updated_table.sizes()) <= len(sizes_to_delete):
        raise SizeDataError("刪除後至少要保留一個尺寸。")

    for size, variant in matches:
        variants = updated_table.variants(size)
        if len(variants) == 1:
            updated_table.delete_size(size)
            continue
        replacement = None
        if updated_table.default_variant(size) == variant:
            replacement = next(
                (candidate for candidate in variants
                 if candidate != variant and candidate.casefold() == "standard"),
                None)
            if replacement is None:
                replacement = next(candidate for candidate in variants
                                   if candidate != variant)
        updated_table.delete_variant(size, variant, replacement)
    return updated_table, len(matches), len(sizes_to_delete)


def validate_generation_parameters(sleeve_length_mm: Any,
                                   cuff_width_mm: Any,
                                   seam_allowance_mm: Any,
                                   cuff_fold_distance_mm: Any,
                                   mirror_join: bool = False
                                   ) -> SleeveParameters:
    """Validate the exact numeric inputs shared by preview and DXF output."""
    try:
        sleeve_length = _numeric_value(sleeve_length_mm, "袖長")
        cuff_width = _numeric_value(cuff_width_mm, "袖口寬")
        seam_allowance = _numeric_value(seam_allowance_mm, "縫份",
                                        allow_zero=True)
        cuff_fold_distance = _numeric_value(cuff_fold_distance_mm,
                                             "袖口折口距離")
    except SizeDataError as error:
        raise ValueError(str(error)) from error
    return SleeveParameters(sleeve_length, cuff_width, seam_allowance,
                            cuff_fold_distance, bool(mirror_join))


@dataclass
class Vertex:
    point: Point
    bulge: float = 0.0


@dataclass
class Polyline:
    vertices: List[Vertex]
    closed: bool
    layer: str = "0"
    source_kind: str = "POLYLINE"


@dataclass
class DxfDocument:
    polylines: List[Polyline]
    unit_code: int
    unit_scale_mm: float


@dataclass
class SleeveAnalysis:
    source: Polyline
    point1: Point
    point2: Point
    point3: Point
    cuff_center: Point
    cuff_upper: Point
    cuff_lower: Point
    axis: Point
    transverse: Point
    cap_vertices: List[Vertex]
    cap_minimum_axial: float
    sleeve_length_mm: float
    cuff_width_mm: float
    cap_length_mm: float
    seam_allowance: Optional[Polyline] = None


@dataclass
class GenerationResult:
    analysis: SleeveAnalysis
    inner: Polyline
    outer: Polyline
    requested_length_mm: float
    requested_cuff_width_mm: float
    seam_allowance_mm: float
    input_unit_name: str
    cap_overhang_mm: float
    cuff_fold_distance_mm: float
    mirror_join: bool = False


@dataclass
class PreviewGeometry:
    outline: List[Point]
    cut_outline: List[Point]
    cap: List[Point]
    cuff: List[Point]
    axis_start: Point
    axis_end: Point
    point1: Point
    point2: Point
    point3: Point
    cuff_center: Point
    target_cuff_center: Optional[Point]
    target_cuff_upper: Optional[Point]
    target_cuff_lower: Optional[Point]
    upper_side: List[Point]
    lower_side: List[Point]


@dataclass
class PreviewTransform:
    scale_factor: float
    minimum_x: float
    maximum_y: float
    left: float
    top: float


def preview_points(preview: PreviewGeometry) -> List[Point]:
    points = (list(preview.outline) + list(preview.cut_outline)
              + list(preview.cap) + list(preview.cuff))
    points.extend([preview.axis_start, preview.axis_end, preview.point1,
                   preview.point2, preview.point3, preview.cuff_center])
    for point in (preview.target_cuff_center, preview.target_cuff_upper,
                  preview.target_cuff_lower):
        if point is not None:
            points.append(point)
    points.extend(preview.upper_side)
    points.extend(preview.lower_side)
    return points


def add(a: Point, b: Point) -> Point:
    return a[0] + b[0], a[1] + b[1]


def sub(a: Point, b: Point) -> Point:
    return a[0] - b[0], a[1] - b[1]


def scale(a: Point, value: float) -> Point:
    return a[0] * value, a[1] * value


def dot(a: Point, b: Point) -> float:
    return a[0] * b[0] + a[1] * b[1]


def cross(a: Point, b: Point) -> float:
    return a[0] * b[1] - a[1] * b[0]


def length(a: Point) -> float:
    return math.hypot(a[0], a[1])


def distance(a: Point, b: Point) -> float:
    return length(sub(b, a))


def normalized(a: Point) -> Point:
    magnitude = length(a)
    return scale(a, 1.0 / magnitude) if magnitude > EPS else (0.0, 0.0)


def perpendicular_left(a: Point) -> Point:
    return -a[1], a[0]


def almost_same(a: Point, b: Point, tolerance: float = 1.0e-7) -> bool:
    return distance(a, b) <= tolerance


def positive_angle(angle: float) -> float:
    angle = math.fmod(angle, 2.0 * PI)
    return angle + 2.0 * PI if angle < 0.0 else angle


def angle_on_directed_arc(angle: float, start_angle: float,
                          sweep: float) -> bool:
    travelled = (positive_angle(angle - start_angle)
                 if sweep >= 0.0
                 else positive_angle(start_angle - angle))
    return travelled <= abs(sweep) + 1.0e-10


def sample_segment(start: Vertex, end: Vertex) -> List[Point]:
    result = [start.point]
    if abs(start.bulge) <= EPS:
        result.append(end.point)
        return result

    chord = sub(end.point, start.point)
    chord_length = length(chord)
    sweep = 4.0 * math.atan(start.bulge)
    sine = math.sin(abs(sweep) * 0.5)
    if chord_length <= EPS or sine <= EPS:
        result.append(end.point)
        return result

    radius = chord_length / (2.0 * sine)
    midpoint = scale(add(start.point, end.point), 0.5)
    normal = normalized(perpendicular_left(chord))
    center = add(midpoint,
                 scale(normal, chord_length / (2.0 * math.tan(sweep * 0.5))))
    start_angle = math.atan2(start.point[1] - center[1],
                             start.point[0] - center[0])
    divisions = max(4, min(180, math.ceil(abs(sweep) / math.radians(3.0))))
    for index in range(1, divisions):
        angle = start_angle + sweep * index / divisions
        result.append(add(center, scale((math.cos(angle), math.sin(angle)),
                                         radius)))
    result.append(end.point)
    return result


def sampled_closed(vertices: Sequence[Vertex]) -> List[Point]:
    result: List[Point] = []
    if len(vertices) < 2:
        return result
    for index, start in enumerate(vertices):
        end = vertices[(index + 1) % len(vertices)]
        samples = sample_segment(start, end)
        if not result:
            result.append(samples[0])
        result.extend(samples[1:])
    return result


def sampled_path(vertices: Sequence[Vertex]) -> List[Point]:
    """Sample a vertex path without adding a closing segment."""
    result: List[Point] = []
    if len(vertices) < 2:
        return result
    for index, start in enumerate(vertices[:-1]):
        samples = sample_segment(start, vertices[index + 1])
        if not result:
            result.append(samples[0])
        result.extend(samples[1:])
    return result


def segment_length(start: Vertex, end: Vertex) -> float:
    chord = distance(start.point, end.point)
    if abs(start.bulge) <= EPS:
        return chord
    sweep = 4.0 * math.atan(start.bulge)
    sine = math.sin(abs(sweep) * 0.5)
    return chord * abs(sweep) / (2.0 * sine) if sine > EPS else chord


def minimum_axial_projection(vertices: Sequence[Vertex], origin: Point,
                             axis: Point) -> float:
    direction = normalized(axis)
    if not vertices or length(direction) <= EPS:
        return 0.0

    minimum = math.inf

    def add_point(point: Point) -> None:
        nonlocal minimum
        minimum = min(minimum, dot(sub(point, origin), direction))

    for index in range(len(vertices) - 1):
        start = vertices[index]
        end = vertices[index + 1]
        add_point(start.point)
        add_point(end.point)
        if abs(start.bulge) <= EPS:
            continue

        chord = sub(end.point, start.point)
        chord_length = length(chord)
        sweep = 4.0 * math.atan(start.bulge)
        sine = math.sin(abs(sweep) * 0.5)
        if chord_length <= EPS or sine <= EPS:
            continue
        radius = chord_length / (2.0 * sine)
        midpoint = scale(add(start.point, end.point), 0.5)
        normal = normalized(perpendicular_left(chord))
        center = add(midpoint,
                     scale(normal, chord_length / (2.0 * math.tan(sweep * 0.5))))
        start_angle = math.atan2(start.point[1] - center[1],
                                 start.point[0] - center[0])
        axis_angle = math.atan2(direction[1], direction[0])
        for angle in (axis_angle, axis_angle + PI):
            if angle_on_directed_arc(angle, start_angle, sweep):
                add_point(add(center,
                              scale((math.cos(angle), math.sin(angle)), radius)))
    return minimum if math.isfinite(minimum) else 0.0


def point_line_distance(point: Point, line_start: Point, line_end: Point) -> float:
    direction = sub(line_end, line_start)
    denominator = length(direction)
    return (abs(cross(direction, sub(point, line_start))) / denominator
            if denominator > EPS else distance(point, line_start))


def point_path_distance(point: Point, path: Sequence[Point]) -> float:
    if len(path) < 2:
        return math.inf
    best = math.inf
    for index in range(1, len(path)):
        start = path[index - 1]
        direction = sub(path[index], start)
        denominator = dot(direction, direction)
        if denominator <= EPS:
            best = min(best, distance(point, start))
            continue
        parameter = max(0.0, min(1.0, dot(sub(point, start), direction)
                                 / denominator))
        best = min(best, distance(point, add(start,
                                             scale(direction, parameter))))
    return best


def median(values: Sequence[float]) -> float:
    if not values:
        return math.inf
    ordered = sorted(values)
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[middle]
    return (ordered[middle - 1] + ordered[middle]) * 0.5


def polygon_area(points: Sequence[Point]) -> float:
    if len(points) < 3:
        return 0.0
    return 0.5 * sum(cross(points[index], points[(index + 1) % len(points)])
                     for index in range(len(points)))


def point_at_distance(points: Sequence[Point], wanted: float,
                      perimeter: float) -> Point:
    if len(points) < 2 or perimeter <= EPS:
        return points[0] if points else (0.0, 0.0)
    wanted = math.fmod(wanted, perimeter)
    if wanted < 0.0:
        wanted += perimeter
    accumulated = 0.0
    for index in range(1, len(points)):
        part = distance(points[index - 1], points[index])
        if accumulated + part >= wanted and part > EPS:
            ratio = (wanted - accumulated) / part
            return add(points[index - 1], scale(sub(points[index],
                                                    points[index - 1]), ratio))
        accumulated += part
    return points[0]


def detect_major_corners(vertices: Sequence[Vertex],
                         outline: Sequence[Point]) -> List[int]:
    perimeter = sum(distance(outline[index - 1], outline[index])
                     for index in range(1, len(outline)))
    if perimeter <= EPS:
        return []

    source_distances: List[float] = []
    distance_along = 0.0
    for index, vertex in enumerate(vertices):
        source_distances.append(distance_along)
        distance_along += segment_length(vertex,
                                         vertices[(index + 1) % len(vertices)])

    window = perimeter * 0.045
    candidates = []
    for index, vertex in enumerate(vertices):
        before = point_at_distance(outline, source_distances[index] - window,
                                   perimeter)
        after = point_at_distance(outline, source_distances[index] + window,
                                  perimeter)
        turn = abs(PI - math.acos(max(-1.0, min(1.0, dot(
            normalized(sub(before, vertex.point)),
            normalized(sub(after, vertex.point)))))))
        candidates.append((turn, index, source_distances[index]))
    candidates.sort(reverse=True)

    selected = []
    # The cap endpoints and cuff corners can be close together on narrow
    # sleeve patterns.  An 8% spacing can discard a real cuff corner and pick
    # a notch vertex along the cap instead.
    minimum_spacing = perimeter * 0.06
    for turn, index, at_distance in candidates:
        if turn < math.radians(22.0):
            break
        if all(abs(at_distance - old_distance) >= minimum_spacing
               and perimeter - abs(at_distance - old_distance)
               >= minimum_spacing
               for _, old_distance in selected):
            selected.append((index, at_distance))
        if len(selected) == 4:
            break
    return sorted(index for index, _ in selected)


@dataclass
class BoundaryPath:
    start: int
    end: int
    points: List[Point]
    exact_length: float
    chord: float
    maximum_deviation: float
    curvature_score: float


def make_boundary_path(vertices: Sequence[Vertex], start: int,
                       end: int) -> BoundaryPath:
    points = [vertices[start].point]
    current = start
    exact = 0.0
    segment_count = 0
    while current != end:
        next_index = (current + 1) % len(vertices)
        points.extend(sample_segment(vertices[current], vertices[next_index])[1:])
        exact += segment_length(vertices[current], vertices[next_index])
        current = next_index
        segment_count += 1
        if segment_count > len(vertices):
            break
    chord = distance(vertices[start].point, vertices[end].point)
    deviation = max((point_line_distance(point, vertices[start].point,
                                         vertices[end].point)
                     for point in points), default=0.0)
    if chord > EPS:
        curvature = max(0.0, exact / chord - 1.0) + deviation / chord
    else:
        curvature = math.inf
    return BoundaryPath(start, end, points, exact, chord, deviation, curvature)


def line_segment_intersection(line_point: Point, line_direction: Point,
                              segment_start: Point, segment_end: Point
                              ) -> Optional[Tuple[Point, float]]:
    segment_direction = sub(segment_end, segment_start)
    denominator = cross(line_direction, segment_direction)
    if abs(denominator) <= EPS:
        return None
    delta = sub(segment_start, line_point)
    line_parameter = cross(delta, segment_direction) / denominator
    segment_parameter = cross(delta, line_direction) / denominator
    if segment_parameter < -EPS or segment_parameter > 1.0 + EPS:
        return None
    return add(line_point, scale(line_direction, line_parameter)), line_parameter


def analyze_polyline(polyline: Polyline, unit_scale_mm: float) -> Optional[SleeveAnalysis]:
    vertices = list(polyline.vertices)
    if len(vertices) > 1 and almost_same(vertices[0].point, vertices[-1].point):
        vertices.pop()
    if not polyline.closed:
        return None
    if len(vertices) < 8:
        return None

    outline = sampled_closed(vertices)
    if len(outline) < 4:
        return None
    corners = detect_major_corners(vertices, outline)
    if len(corners) != 4:
        return None
    paths = [make_boundary_path(vertices, corners[index],
                               corners[(index + 1) % 4])
             for index in range(4)]
    cap_index = max(range(4), key=lambda index: paths[index].curvature_score)
    cuff_index = (cap_index + 2) % 4
    cap = paths[cap_index]
    cuff = paths[cuff_index]
    second_highest = max((paths[index].curvature_score for index in range(4)
                          if index != cap_index), default=0.0)
    if (not math.isfinite(cap.curvature_score)
            or cap.curvature_score < 0.035
            or cap.curvature_score < second_highest * 1.35):
        return None
    if (cuff.chord <= EPS or cuff.exact_length / cuff.chord > 1.12
            or cuff.maximum_deviation / cuff.chord > 0.10):
        return None

    cap_start = vertices[cap.start].point
    cap_end = vertices[cap.end].point
    cuff_start = vertices[cuff.start].point
    cuff_end = vertices[cuff.end].point
    cap_baseline_center = scale(add(cap_start, cap_end), 0.5)
    cuff_center = scale(add(cuff_start, cuff_end), 0.5)
    toward_cap = normalized(sub(cap_baseline_center, cuff_center))
    if length(toward_cap) <= EPS:
        return None

    best_parameter = -math.inf
    point3 = None
    for index in range(1, len(cap.points)):
        intersection = line_segment_intersection(
            cuff_center, toward_cap, cap.points[index - 1], cap.points[index])
        if intersection is not None and intersection[1] > best_parameter:
            point3, best_parameter = intersection
    if point3 is None or best_parameter <= 0.0:
        return None

    axis = normalized(sub(cuff_center, point3))
    transverse = perpendicular_left(axis)
    # Match the stable orientation used by the LibreCAD analyser.
    if transverse[1] < -EPS or (abs(transverse[1]) <= EPS
                                and transverse[0] < 0.0):
        transverse = scale(transverse, -1.0)
    if dot(sub(cap_start, cap_baseline_center), transverse) \
            >= dot(sub(cap_end, cap_baseline_center), transverse):
        point1, point2 = cap_start, cap_end
    else:
        point1, point2 = cap_end, cap_start
    if dot(sub(cuff_start, cuff_center), transverse) \
            >= dot(sub(cuff_end, cuff_center), transverse):
        cuff_upper, cuff_lower = cuff_start, cuff_end
    else:
        cuff_upper, cuff_lower = cuff_end, cuff_start

    cap_vertices: List[Vertex] = []
    current = cap.start
    while True:
        cap_vertices.append(vertices[current])
        if current == cap.end:
            break
        current = (current + 1) % len(vertices)
    cap_minimum_axial = minimum_axial_projection(cap_vertices, point3, axis)
    cap_length = sum(segment_length(cap_vertices[index], cap_vertices[index + 1])
                     for index in range(len(cap_vertices) - 1))
    return SleeveAnalysis(
        source=Polyline(vertices, True, polyline.layer, polyline.source_kind),
        point1=point1,
        point2=point2,
        point3=point3,
        cuff_center=cuff_center,
        cuff_upper=cuff_upper,
        cuff_lower=cuff_lower,
        axis=axis,
        transverse=transverse,
        cap_vertices=cap_vertices,
        cap_minimum_axial=cap_minimum_axial,
        sleeve_length_mm=dot(sub(cuff_center, point3), axis) * unit_scale_mm,
        cuff_width_mm=distance(cuff_start, cuff_end) * unit_scale_mm,
        cap_length_mm=cap_length * unit_scale_mm,
    )


def find_sleeve(document: DxfDocument) -> SleeveAnalysis:
    candidates = []
    for polyline in document.polylines:
        analysis = analyze_polyline(polyline, document.unit_scale_mm)
        if analysis is not None:
            # The actual sleeve is the inner member when a cutting outline is
            # also present.  Choosing the smallest valid closed area mirrors
            # the source plugin's inner-outline preference.
            area = abs(polygon_area(sampled_closed(analysis.source.vertices)))
            candidates.append((area, analysis))
    if not candidates:
        raise ValueError(
            "找不到可辨識的閉合袖片。請確認 DXF 含有至少 8 個頂點的袖片外框，"
            "且 POLYLINE/LWPOLYLINE 已閉合。")
    analysis = min(candidates, key=lambda item: item[0])[1]
    analysis.seam_allowance = find_seam_allowance(document, analysis)
    return analysis


def line_intersection(first_point: Point, first_direction: Point,
                      second_point: Point, second_direction: Point
                      ) -> Optional[Point]:
    denominator = cross(first_direction, second_direction)
    if abs(denominator) <= EPS:
        return None
    parameter = cross(sub(second_point, first_point), second_direction) \
        / denominator
    return add(first_point, scale(first_direction, parameter))


def find_seam_allowance(document: DxfDocument,
                        analysis: SleeveAnalysis) -> Optional[Polyline]:
    """Find the original outer/cutting contour paired with the sleeve seam."""
    source_points = sampled_closed(analysis.source.vertices)
    source_area = abs(polygon_area(source_points))
    expected = DEFAULT_SEAM_ALLOWANCE_MM / document.unit_scale_mm
    candidates = []
    for candidate in document.polylines:
        if not candidate.closed or len(candidate.vertices) < 3:
            continue
        candidate_points = sampled_closed(candidate.vertices)
        candidate_area = abs(polygon_area(candidate_points))
        if candidate_area <= source_area * 1.001:
            continue
        distances = [point_path_distance(point, candidate_points)
                     for point in source_points]
        distances.extend(point_path_distance(point, source_points)
                         for point in candidate_points)
        measured = median(distances)
        if not math.isfinite(measured) or measured > max(expected * 2.0,
                                                         1.0e-5):
            continue
        score = abs(measured - expected) + max(
            0.0, measured - expected) * 0.25
        candidates.append((score, candidate))
    if not candidates:
        return None
    return min(candidates, key=lambda item: item[0])[1]


def offset_closed_polygon(vertices: Sequence[Vertex], offset: float) -> List[Point]:
    points = sampled_closed(vertices)
    if len(points) > 1 and almost_same(points[0], points[-1]):
        points.pop()
    if len(points) < 3 or offset <= EPS:
        return list(points)
    signed_area = polygon_area(points)
    if abs(signed_area) <= EPS:
        return list(points)

    offset_lines = []
    for index, start in enumerate(points):
        end = points[(index + 1) % len(points)]
        direction = sub(end, start)
        unit = normalized(direction)
        outward = (unit[1], -unit[0]) if signed_area > 0.0 \
            else (-unit[1], unit[0])
        offset_lines.append((add(start, scale(outward, offset)), direction))

    result = []
    for index in range(len(points)):
        previous = offset_lines[(index - 1) % len(points)]
        current = offset_lines[index]
        corner = line_intersection(previous[0], previous[1],
                                   current[0], current[1])
        if corner is None:
            corner = current[0]
        result.append(corner)
    return result


def contour_outward_normal(contour: Sequence[Vertex], line_start: Point,
                           line_end: Point) -> Point:
    direction = normalized(sub(line_end, line_start))
    if length(direction) <= EPS:
        return (0.0, 0.0)
    area = polygon_area(sampled_closed(contour))
    if area >= 0.0:
        return normalized((direction[1], -direction[0]))
    return normalized((-direction[1], direction[0]))


def route_between(points: Sequence[Point], start: int, end: int,
                  forward: bool) -> List[Point]:
    if len(points) < 2 or start < 0 or end < 0 \
            or start >= len(points) or end >= len(points):
        return []
    result = [points[start]]
    current = start
    while current != end:
        current = ((current + 1) % len(points) if forward
                   else (current - 1) % len(points))
        result.append(points[current])
        if len(result) > len(points) + 1:
            return []
    return result


def expanded_cap_route(expanded: Polyline, analysis: SleeveAnalysis,
                       cap_start: Point, cap_end: Point) -> List[Point]:
    points = sampled_closed(expanded.vertices)
    if len(points) > 1 and almost_same(points[0], points[-1]):
        points.pop()
    cap_reference = sampled_path(analysis.cap_vertices)
    if len(points) < 3 or len(cap_reference) < 2:
        return []
    start_index = min(range(len(points)),
                      key=lambda index: distance(points[index], cap_start))
    end_index = min(range(len(points)),
                    key=lambda index: distance(points[index], cap_end))
    if start_index == end_index:
        return []
    forward = route_between(points, start_index, end_index, True)
    backward = route_between(points, start_index, end_index, False)
    if len(forward) < 2 or len(backward) < 2:
        return []
    forward_score = sum(point_path_distance(point, cap_reference)
                        for point in forward) / len(forward)
    backward_score = sum(point_path_distance(point, cap_reference)
                         for point in backward) / len(backward)
    return forward if forward_score <= backward_score else backward


def line_polyline_intersection(polyline: Sequence[Point],
                               line_point: Point, line_direction: Point,
                               expected: Point
                               ) -> Optional[Tuple[Point, int, float]]:
    best = None
    best_distance = math.inf
    for index in range(1, len(polyline)):
        segment_start = polyline[index - 1]
        segment_direction = sub(polyline[index], segment_start)
        denominator = cross(line_direction, segment_direction)
        if abs(denominator) <= EPS:
            continue
        delta = sub(segment_start, line_point)
        line_parameter = cross(delta, segment_direction) / denominator
        segment_parameter = cross(delta, line_direction) / denominator
        if segment_parameter < -1.0e-6 or segment_parameter > 1.0 + 1.0e-6:
            continue
        candidate = add(line_point, scale(line_direction, line_parameter))
        candidate_distance = distance(candidate, expected)
        if candidate_distance < best_distance:
            best_distance = candidate_distance
            best = (candidate, index - 1,
                    max(0.0, min(1.0, segment_parameter)))
    return best


def clip_cap_route(route: Sequence[Point], start: Point, start_segment: int,
                   end: Point, end_segment: int) -> List[Point]:
    if (len(route) < 2 or start_segment < 0 or end_segment < start_segment
            or end_segment >= len(route) - 1):
        return []
    result = [start]
    for index in range(start_segment + 1, end_segment + 1):
        if not almost_same(result[-1], route[index]):
            result.append(route[index])
    if not almost_same(result[-1], end):
        result.append(end)
    return result


def build_expanded_outer(inner: Sequence[Vertex], analysis: SleeveAnalysis,
                         target_upper: Point, target_lower: Point,
                         expanded_source: Polyline, side_offset: float,
                         cuff_cut_axial: float) -> List[Point]:
    """Build one cut loop: expanded sleeve cap, offset sides, and cuff edge."""
    cap_start = analysis.cap_vertices[0].point
    cap_end = analysis.cap_vertices[-1].point
    cap_starts_at_point1 = almost_same(cap_start, analysis.point1)
    cap_route = expanded_cap_route(expanded_source, analysis,
                                   cap_start, cap_end)
    if len(cap_route) < 2:
        return []

    first_cuff = (target_lower if cap_starts_at_point1 else target_upper)
    second_cuff = (target_upper if cap_starts_at_point1 else target_lower)
    first_normal = contour_outward_normal(inner, cap_end, first_cuff)
    second_normal = contour_outward_normal(inner, second_cuff, cap_start)
    if length(first_normal) <= EPS or length(second_normal) <= EPS:
        return []

    first_side_start = add(cap_end, scale(first_normal, side_offset))
    first_side_end = add(first_cuff, scale(first_normal, side_offset))
    second_side_start = add(second_cuff, scale(second_normal, side_offset))
    second_side_end = add(cap_start, scale(second_normal, side_offset))
    first_side_direction = sub(first_side_end, first_side_start)
    second_side_direction = sub(second_side_end, second_side_start)
    if length(first_side_direction) <= EPS \
            or length(second_side_direction) <= EPS:
        return []

    cuff_axis = normalized(sub(analysis.cuff_center, analysis.point3))
    outer_cuff_first = add(first_cuff, scale(cuff_axis, cuff_cut_axial))
    outer_cuff_second = add(second_cuff, scale(cuff_axis, cuff_cut_axial))
    outer_cuff_direction = sub(outer_cuff_second, outer_cuff_first)
    if length(outer_cuff_direction) <= EPS:
        return []

    end_intersection = line_polyline_intersection(
        cap_route, first_side_start, first_side_direction, cap_end)
    start_intersection = line_polyline_intersection(
        cap_route, second_side_end, second_side_direction, cap_start)
    if end_intersection is not None and start_intersection is not None:
        cap_at_end, end_segment, _ = end_intersection
        cap_at_start, start_segment, _ = start_intersection
        if start_segment > end_segment:
            return []
        clipped_cap = clip_cap_route(cap_route, cap_at_start, start_segment,
                                     cap_at_end, end_segment)
    else:
        # The source cutting contour often stops just short of the offset
        # side-line joins.  Extend its endpoint tangents, matching the
        # LibreCAD plugin, while retaining every original cap notch between
        # the two joins.
        if len(cap_route) < 3:
            return []
        cap_at_start = line_intersection(
            second_side_end, second_side_direction,
            cap_route[0], sub(cap_route[1], cap_route[0]))
        cap_at_end = line_intersection(
            first_side_start, first_side_direction,
            cap_route[-1], sub(cap_route[-1], cap_route[-2]))
        maximum_extension = max(side_offset * 4.0, 1.0e-5)
        if (cap_at_start is None or cap_at_end is None
                or distance(cap_at_start, cap_start) > maximum_extension
                or distance(cap_at_end, cap_end) > maximum_extension):
            return []
        clipped_cap = [cap_at_start]
        clipped_cap.extend(cap_route[1:])
        if not almost_same(clipped_cap[-1], cap_at_end):
            clipped_cap.append(cap_at_end)
    if len(clipped_cap) < 2:
        return []

    cuff_at_first = line_intersection(
        first_side_start, first_side_direction,
        outer_cuff_first, outer_cuff_direction)
    cuff_at_second = line_intersection(
        second_side_start, second_side_direction,
        outer_cuff_first, outer_cuff_direction)
    if cuff_at_first is None or cuff_at_second is None:
        return []
    result: List[Point] = []
    for point in clipped_cap:
        if not result or not almost_same(result[-1], point):
            result.append(point)
    for point in (cuff_at_first, cuff_at_second):
        if not almost_same(result[-1], point):
            result.append(point)
    return result if len(result) >= 3 else []


def reflect_point_across_axis(point: Point, axis_origin: Point,
                              axis_direction: Point) -> Point:
    direction = normalized(axis_direction)
    if length(direction) <= EPS:
        return point
    relative = sub(point, axis_origin)
    foot = add(axis_origin, scale(direction, dot(relative, direction)))
    return sub(scale(foot, 2.0), point)


def mirror_outer_sides_and_join(outer_points: Sequence[Point],
                                centerline_start: Point,
                                centerline_end: Point) -> List[Point]:
    """Mirror only the two cuff-side edges and join them at the centerline."""
    if len(outer_points) < 5:
        return []
    cap_points = list(outer_points[:-2])
    cuff_first, cuff_second = outer_points[-2:]
    first_side_start, first_side_end = cap_points[-1], cuff_first
    second_side_start, second_side_end = cuff_second, cap_points[0]
    centerline_direction = sub(centerline_end, centerline_start)
    cuff_direction = sub(cuff_second, cuff_first)
    if length(centerline_direction) <= EPS or length(cuff_direction) <= EPS:
        return []

    def centerline_join(start: Point, end: Point) -> Optional[Point]:
        intersection = line_intersection(
            start, sub(end, start), centerline_start, centerline_direction)
        if intersection is None:
            return None
        point = intersection
        direction = sub(end, start)
        parameter = dot(sub(point, start), direction) / dot(direction,
                                                              direction)
        if parameter < -1.0e-6 or parameter > 1.0 + 1.0e-6:
            return None
        return point

    def mirrored_cuff_join(start: Point, end: Point) -> Optional[Point]:
        reflected_start = reflect_point_across_axis(
            start, centerline_start, centerline_direction)
        reflected_end = reflect_point_across_axis(
            end, centerline_start, centerline_direction)
        return line_intersection(
            reflected_start, sub(reflected_end, reflected_start),
            cuff_first, cuff_direction)

    first_join = centerline_join(first_side_start, first_side_end)
    second_join = centerline_join(second_side_start, second_side_end)
    first_cuff_join = mirrored_cuff_join(first_side_start, first_side_end)
    second_cuff_join = mirrored_cuff_join(second_side_start, second_side_end)
    if (first_join is None or second_join is None
            or first_cuff_join is None or second_cuff_join is None):
        return []

    if (dot(sub(second_cuff_join, first_cuff_join), cuff_direction) <= EPS):
        return []

    result: List[Point] = []
    for point in (*cap_points, first_join, first_cuff_join,
                  second_cuff_join, second_join):
        if not result or not almost_same(result[-1], point):
            result.append(point)
    return result if len(result) >= 3 else []


def build_long_sleeve(analysis: SleeveAnalysis, unit_scale_mm: float,
                      sleeve_length_mm: float, cuff_width_mm: float,
                      seam_allowance_mm: float,
                      cuff_fold_distance_mm: float =
                      DEFAULT_CUFF_FOLD_DISTANCE_MM,
                      mirror_join: bool = False) -> GenerationResult:
    parameters = validate_generation_parameters(
        sleeve_length_mm, cuff_width_mm, seam_allowance_mm,
        cuff_fold_distance_mm, mirror_join)
    sleeve_length_mm = parameters.sleeve_length_mm
    cuff_width_mm = parameters.cuff_width_mm
    seam_allowance_mm = parameters.seam_allowance_mm
    cuff_fold_distance_mm = parameters.cuff_fold_distance_mm
    mirror_join = parameters.mirror_join
    if sleeve_length_mm <= analysis.sleeve_length_mm:
        raise ValueError(
            f"目標袖長必須大於原袖長（目前約 {analysis.sleeve_length_mm:.3f} mm）。")

    length_drawing = sleeve_length_mm / unit_scale_mm
    cuff_width_drawing = cuff_width_mm / unit_scale_mm
    # The cap may extend beyond P3.  Move the new cuff toward P3 by that
    # measured overhang so the actual-size contour has the requested span.
    cuff_axial_distance = length_drawing + analysis.cap_minimum_axial
    center = add(analysis.point3, scale(analysis.axis, cuff_axial_distance))
    upper = add(center, scale(analysis.transverse, cuff_width_drawing * 0.5))
    lower = add(center, scale(analysis.transverse, -cuff_width_drawing * 0.5))

    inner_vertices = list(analysis.cap_vertices)
    if almost_same(inner_vertices[-1].point, analysis.point2):
        inner_vertices.extend([Vertex(lower), Vertex(upper)])
    else:
        inner_vertices.extend([Vertex(upper), Vertex(lower)])
    inner = Polyline(inner_vertices, True, "LONG_SLEEVE_SEAM", "POLYLINE")
    side_offset = seam_allowance_mm / unit_scale_mm
    expanded_source = analysis.seam_allowance
    if expanded_source is None:
        expanded_points = offset_closed_polygon(inner_vertices, side_offset)
        expanded_source = Polyline([Vertex(point) for point in expanded_points],
                                   True, "GEOMETRIC_OFFSET", "POLYLINE")
    outer_points = build_expanded_outer(
        inner_vertices, analysis, upper, lower, expanded_source, side_offset,
        cuff_fold_distance_mm / unit_scale_mm)
    if len(outer_points) < 3:
        raise ValueError("無法沿原始袖山牙口與袖口折口建立縫份外框。")
    if mirror_join:
        outer_points = mirror_outer_sides_and_join(outer_points, lower, upper)
        if len(outer_points) < 3:
            raise ValueError("無法依袖口中心線鏡射上下外框並完成兩處接合。")
    outer = Polyline([Vertex(point) for point in outer_points], True,
                     "LONG_SLEEVE_CUT", "POLYLINE")
    unit_name = "inch" if abs(unit_scale_mm - 25.4) < EPS else "mm"
    return GenerationResult(analysis, inner, outer, sleeve_length_mm,
                            cuff_width_mm, seam_allowance_mm, unit_name,
                            analysis.cap_minimum_axial * unit_scale_mm,
                            cuff_fold_distance_mm, mirror_join)


def build_preview(analysis: SleeveAnalysis, unit_scale_mm: float,
                  sleeve_length_mm: float, cuff_width_mm: float,
                  seam_allowance_mm: float = DEFAULT_SEAM_ALLOWANCE_MM,
                  cuff_fold_distance_mm: float =
                  DEFAULT_CUFF_FOLD_DISTANCE_MM,
                  mirror_join: bool = False) -> PreviewGeometry:
    """Build the points drawn by the LibreCAD Sleeve Analyzer preview."""
    preview = PreviewGeometry(
        outline=sampled_closed(analysis.source.vertices),
        cut_outline=[],
        cap=sampled_path(analysis.cap_vertices),
        cuff=[analysis.cuff_upper, analysis.cuff_lower],
        axis_start=analysis.point3,
        axis_end=analysis.cuff_center,
        point1=analysis.point1,
        point2=analysis.point2,
        point3=analysis.point3,
        cuff_center=analysis.cuff_center,
        target_cuff_center=None,
        target_cuff_upper=None,
        target_cuff_lower=None,
        upper_side=[],
        lower_side=[])
    try:
        result = build_long_sleeve(
            analysis, unit_scale_mm, sleeve_length_mm, cuff_width_mm,
            seam_allowance_mm, cuff_fold_distance_mm, mirror_join)
        preview.cut_outline = sampled_closed(result.outer.vertices)
        cap_count = len(analysis.cap_vertices)
        first_target = result.inner.vertices[cap_count].point
        second_target = result.inner.vertices[cap_count + 1].point
        if almost_same(analysis.cap_vertices[-1].point, analysis.point2):
            target_lower, target_upper = first_target, second_target
        else:
            target_upper, target_lower = first_target, second_target
        target_center = scale(add(target_upper, target_lower), 0.5)
        preview.target_cuff_center = target_center
        preview.target_cuff_upper = target_upper
        preview.target_cuff_lower = target_lower
        preview.upper_side = [analysis.point1, target_upper]
        preview.lower_side = [analysis.point2, target_lower]
    except ValueError:
        # Match the plugin's behavior when the target values are not usable:
        # keep the analyzed source visible and omit the target overlay.
        pass
    return preview


def make_preview_transform(preview: PreviewGeometry, width: float,
                           height: float, margin: float = 24.0
                           ) -> PreviewTransform:
    points = preview_points(preview)
    if not points:
        return PreviewTransform(1.0, 0.0, 0.0, margin, margin)
    minimum_x = min(point[0] for point in points)
    maximum_x = max(point[0] for point in points)
    minimum_y = min(point[1] for point in points)
    maximum_y = max(point[1] for point in points)
    drawing_width = max(maximum_x - minimum_x, EPS)
    drawing_height = max(maximum_y - minimum_y, EPS)
    available_width = max(1.0, width - 2.0 * margin)
    available_height = max(1.0, height - 2.0 * margin)
    scale_factor = min(available_width / drawing_width,
                       available_height / drawing_height)
    left = (width - drawing_width * scale_factor) * 0.5
    top = (height - drawing_height * scale_factor) * 0.5
    return PreviewTransform(scale_factor, minimum_x, maximum_y, left, top)


def preview_to_screen(transform: PreviewTransform, point: Point
                      ) -> Point:
    return (transform.left + (point[0] - transform.minimum_x)
            * transform.scale_factor,
            transform.top + (transform.maximum_y - point[1])
            * transform.scale_factor)


def _pair_lines(lines: Sequence[str]) -> List[Tuple[int, str]]:
    pairs = []
    for index in range(0, len(lines) - 1, 2):
        try:
            code = int(lines[index].strip())
        except ValueError:
            continue
        pairs.append((code, lines[index + 1].strip()))
    return pairs


def _finish_vertex(vertices: List[Vertex], current: Optional[dict]) -> None:
    if current is not None and current.get("x") is not None \
            and current.get("y") is not None:
        vertices.append(Vertex((float(current["x"]), float(current["y"])),
                               float(current.get("bulge", 0.0))))


def read_dxf(path: Path) -> DxfDocument:
    try:
        text = path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError as error:
        raise ValueError(f"無法讀取 DXF：{error}") from error
    pairs = _pair_lines(text.splitlines())
    unit_code = 0
    for index, (code, value) in enumerate(pairs[:-1]):
        if code == 9 and value.upper() == "$INSUNITS":
            if pairs[index + 1][0] == 70:
                try:
                    unit_code = int(float(pairs[index + 1][1]))
                except ValueError:
                    unit_code = 0
            break
    unit_scale = {1: 25.4, 4: 1.0}.get(unit_code, 1.0)
    polylines: List[Polyline] = []
    index = 0
    while index < len(pairs):
        code, value = pairs[index]
        if code != 0 or value.upper() not in {"POLYLINE", "LWPOLYLINE"}:
            index += 1
            continue
        kind = value.upper()
        if kind == "POLYLINE":
            vertices: List[Vertex] = []
            layer = "0"
            closed = False
            current = None
            cursor = index + 1
            while cursor < len(pairs):
                entity_code, entity_value = pairs[cursor]
                if entity_code == 0:
                    if entity_value.upper() == "VERTEX":
                        _finish_vertex(vertices, current)
                        current = {"x": None, "y": None, "bulge": 0.0}
                    elif entity_value.upper() == "SEQEND":
                        _finish_vertex(vertices, current)
                        index = cursor
                        break
                    else:
                        index = cursor - 1
                        break
                elif current is None:
                    if entity_code == 8:
                        layer = entity_value
                    elif entity_code == 70:
                        try:
                            closed = bool(int(float(entity_value)) & 1)
                        except ValueError:
                            pass
                else:
                    if entity_code == 10:
                        current["x"] = entity_value
                    elif entity_code == 20:
                        current["y"] = entity_value
                    elif entity_code == 42:
                        current["bulge"] = entity_value
                cursor += 1
            if len(vertices) > 1 and almost_same(vertices[0].point,
                                                  vertices[-1].point):
                closed = True
            if vertices:
                polylines.append(Polyline(vertices, closed, layer, kind))
        else:
            vertices = []
            layer = "0"
            closed = False
            current = None
            cursor = index + 1
            while cursor < len(pairs) and pairs[cursor][0] != 0:
                entity_code, entity_value = pairs[cursor]
                if entity_code == 8:
                    layer = entity_value
                elif entity_code == 70:
                    try:
                        closed = bool(int(float(entity_value)) & 1)
                    except ValueError:
                        pass
                elif entity_code == 10:
                    _finish_vertex(vertices, current)
                    current = {"x": entity_value, "y": None, "bulge": 0.0}
                elif entity_code == 20 and current is not None:
                    current["y"] = entity_value
                elif entity_code == 42 and current is not None:
                    current["bulge"] = entity_value
                cursor += 1
            _finish_vertex(vertices, current)
            if len(vertices) > 1 and almost_same(vertices[0].point,
                                                  vertices[-1].point):
                closed = True
            if vertices:
                polylines.append(Polyline(vertices, closed, layer, kind))
            index = cursor - 1
        index += 1
    return DxfDocument(polylines, unit_code, unit_scale)


def _format_number(value: float) -> str:
    return f"{value:.10f}".rstrip("0").rstrip(".") or "0"


def _write_pair(lines: List[str], code: int, value: object) -> None:
    lines.extend([str(code), str(value)])


def write_dxf(path: Path, result: GenerationResult,
              include_source: bool = False) -> None:
    lines: List[str] = []
    _write_pair(lines, 999, "Python Sleeve Generator")
    _write_pair(lines, 0, "SECTION")
    _write_pair(lines, 2, "HEADER")
    _write_pair(lines, 9, "$ACADVER")
    _write_pair(lines, 1, "AC1009")
    _write_pair(lines, 9, "$INSUNITS")
    _write_pair(lines, 70, 1 if result.input_unit_name == "inch" else 4)
    _write_pair(lines, 0, "ENDSEC")
    _write_pair(lines, 0, "SECTION")
    _write_pair(lines, 2, "ENTITIES")

    polylines = []
    if include_source:
        source = result.analysis.source
        polylines.append(Polyline(source.vertices, True, "SOURCE_SLEEVE",
                                  source.source_kind))
    polylines.extend([result.outer, result.inner])
    for polyline in polylines:
        _write_pair(lines, 0, "POLYLINE")
        _write_pair(lines, 8, polyline.layer)
        _write_pair(lines, 66, 1)
        _write_pair(lines, 70, 1)
        for vertex in polyline.vertices:
            _write_pair(lines, 0, "VERTEX")
            _write_pair(lines, 8, polyline.layer)
            _write_pair(lines, 10, _format_number(vertex.point[0]))
            _write_pair(lines, 20, _format_number(vertex.point[1]))
            if abs(vertex.bulge) > EPS:
                _write_pair(lines, 42, _format_number(vertex.bulge))
        _write_pair(lines, 0, "SEQEND")
        _write_pair(lines, 8, polyline.layer)
    _write_pair(lines, 0, "ENDSEC")
    _write_pair(lines, 0, "EOF")
    try:
        path.write_text("\n".join(lines) + "\n", encoding="ascii")
    except OSError as error:
        raise ValueError(f"無法寫入輸出 DXF：{error}") from error


def generate_file(input_path: Path, output_path: Path, sleeve_length_mm: float,
                  cuff_width_mm: float,
                  seam_allowance_mm: float = DEFAULT_SEAM_ALLOWANCE_MM,
                  include_source: bool = False,
                  cuff_fold_distance_mm: float =
                  DEFAULT_CUFF_FOLD_DISTANCE_MM,
                  mirror_join: bool = False) -> GenerationResult:
    if input_path.resolve() == output_path.resolve():
        raise ValueError("輸出 DXF 必須是新檔案，不可覆蓋輸入袖片。")
    document = read_dxf(input_path)
    analysis = find_sleeve(document)
    result = build_long_sleeve(analysis, document.unit_scale_mm,
                               sleeve_length_mm, cuff_width_mm,
                               seam_allowance_mm, cuff_fold_distance_mm,
                               mirror_join)
    write_dxf(output_path, result, include_source)
    return result


def result_summary(result: GenerationResult, output_path: Path) -> str:
    return (
        f"完成：{output_path}\n"
        f"原始袖長（P3→袖口中心）：{result.analysis.sleeve_length_mm:.3f} mm\n"
        f"袖山外伸校正：{result.cap_overhang_mm:.3f} mm\n"
        f"生成內框袖長：{result.requested_length_mm:.3f} mm\n"
        f"生成袖口寬：{result.requested_cuff_width_mm:.3f} mm\n"
        f"縫份外框：{result.seam_allowance_mm:.3f} mm\n"
        f"袖口折口距離：{result.cuff_fold_distance_mm:.3f} mm\n"
        f"鏡射接合選項：{'已勾選' if result.mirror_join else '未勾選'}\n"
        "輸出圖層：LONG_SLEEVE_CUT、LONG_SLEEVE_SEAM"
    )


class SleeveGui(GUI_BASE):
    def __init__(self) -> None:
        super().__init__()
        self.title("Python 長袖 DXF 產生器")
        self.geometry("1180x760")
        self.minsize(980, 620)
        try:
            ttk.Style(self).theme_use("vista")
        except tk.TclError:
            pass
        self._registry_error = ""
        registered_path: Optional[Path] = None
        try:
            registered_path = registered_size_data_path()
            self.size_table: Optional[SizeTable] = load_size_table(
                registered_path or default_size_data_path())
            self._size_load_error = ""
            if registered_path is None and winreg is not None:
                try:
                    register_size_data_path(self.size_table.path)
                except SizeDataError as error:
                    self._registry_error = str(error)
        except SizeDataError as error:
            self.size_table = None
            self._size_load_error = str(error)
        self.input_var = tk.StringVar()
        self.output_var = tk.StringVar()
        self.recognition_var = tk.StringVar(value="尚未載入來源 DXF")
        self.size_var = tk.StringVar()
        self.variant_var = tk.StringVar()
        self.value_source_var = tk.StringVar()
        self.validation_var = tk.StringVar()
        self.length_var = tk.StringVar()
        self.cuff_var = tk.StringVar()
        self.seam_var = tk.StringVar(value=str(DEFAULT_SEAM_ALLOWANCE_MM))
        self.cuff_fold_var = tk.StringVar(
            value=str(DEFAULT_CUFF_FOLD_DISTANCE_MM))
        self.mirror_join_var = tk.BooleanVar(value=False)
        supplied_excel = Path(
            r"H:\比酷\1151005\杰優女版長袖尺寸表XS-6XL.xlsx")
        self.batch_excel_var = tk.StringVar(
            value=str(supplied_excel) if supplied_excel.is_file() else "")
        self.batch_folder_var = tk.StringVar()
        self.batch_name_var = tk.StringVar(value="長袖批次")
        self.extension_name_var = tk.StringVar(value="長袖")
        self.batch_json_path_var = tk.StringVar(
            value=str(self.size_table.path) if self.size_table is not None else "")
        self.batch_status_var = tk.StringVar(value="請選擇 Excel 尺寸表。")
        self._batch_sizes: Dict[str, ExcelSizeValues] = {}
        self._current_preview: Optional[PreviewGeometry] = None
        self._preview_message = "請選擇或拖入來源 DXF"
        self._preview_after_id = None
        self._detection = SizeDetection("unknown", None, (),
                                        "尚未載入來源 DXF")
        self._input_error = ""
        self._selection_updating = False
        self._selection_is_manual = False
        self.input_var.trace_add("write", self._on_input_var_changed)
        self.output_var.trace_add("write", self._on_value_changed)
        for variable in (self.length_var, self.cuff_var, self.seam_var,
                         self.cuff_fold_var, self.mirror_join_var):
            variable.trace_add("write", self._on_value_changed)
        self._build_widgets()
        self._populate_size_choices()
        if self.batch_excel_var.get():
            self.after_idle(self._load_batch_excel)
        if self._size_load_error:
            self.recognition_var.set(self._size_load_error)
        if self._registry_error:
            self.after_idle(lambda: self._set_status(
                "尺寸資料已載入，但無法登錄目前路徑：\n"
                f"{self._registry_error}"))
        elif self.size_table is not None:
            source = ("已從登錄檔載入尺寸資料" if registered_path is not None
                      else "已載入預設尺寸資料")
            self.after_idle(lambda: self._set_status(
                f"{source}：\n{self.size_table.path}\n"
                f"登錄值：{SIZE_DATA_REGISTRY_DISPLAY}"))
        self.after_idle(self._enable_file_drop)
        self.after_idle(self._refresh_preview)

    def _build_widgets(self) -> None:
        main = ttk.Frame(self, padding=(8, 6))
        main.grid(row=0, column=0, sticky="nsew")
        self.rowconfigure(0, weight=1)
        self.columnconfigure(0, weight=1)
        main.rowconfigure(0, weight=1)
        main.columnconfigure(0, weight=1)

        self.notebook = ttk.Notebook(main)
        self.notebook.grid(row=0, column=0, sticky="nsew")
        single_page = ttk.Frame(self.notebook, padding=10)
        batch_page = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(single_page, text="單件轉換")
        self.notebook.add(batch_page, text="資料夾批次轉換")
        single_page.rowconfigure(0, weight=1)
        single_page.columnconfigure(0, weight=3)
        single_page.columnconfigure(1, weight=2)

        self.preview_canvas = tk.Canvas(
            single_page, width=720, height=680, background="#f7f9fc",
            highlightthickness=0)
        self.preview_canvas.grid(row=0, column=0, sticky="nsew",
                                 padx=(0, 10))
        self.preview_canvas.bind("<Configure>",
                                 lambda _event: self._draw_current_preview())

        panel = ttk.Frame(single_page, padding=(0, 2, 0, 0))
        panel.grid(row=0, column=1, sticky="nsew")
        panel.rowconfigure(15, weight=1)
        panel.columnconfigure(1, weight=1)
        self._path_row(panel, 0, "袖子 DXF：", self.input_var,
                       self._browse_input)
        self._path_row(panel, 1, "輸出 DXF：", self.output_var,
                       self._browse_output)
        self.drop_hint = ttk.Label(
            panel, text="也可以把 .dxf 檔案拖到這個視窗",
            relief="groove", anchor="center")
        self.drop_hint.grid(
                      row=2, column=0, columnspan=3, sticky="ew", pady=(4, 8))
        ttk.Label(panel, text="辨識狀態：").grid(row=3, column=0,
                                               sticky="nw", pady=4)
        ttk.Label(panel, textvariable=self.recognition_var,
                  wraplength=360, justify="left").grid(
                      row=3, column=1, columnspan=2, sticky="ew", pady=4)
        ttk.Label(panel, text="尺寸：").grid(row=4, column=0, sticky="w",
                                             padx=(0, 8), pady=4)
        self.size_combo = ttk.Combobox(
            panel, textvariable=self.size_var, state="readonly", width=18)
        self.size_combo.grid(row=4, column=1, columnspan=2, sticky="ew",
                             pady=4)
        self.size_combo.bind("<<ComboboxSelected>>", self._on_size_selected)
        ttk.Label(panel, text="款式：").grid(row=5, column=0, sticky="w",
                                             padx=(0, 8), pady=4)
        self.variant_combo = ttk.Combobox(
            panel, textvariable=self.variant_var, state="readonly", width=18)
        self.variant_combo.grid(row=5, column=1, columnspan=2, sticky="ew",
                                pady=4)
        self.variant_combo.bind("<<ComboboxSelected>>",
                                self._on_variant_selected)
        self._number_row(panel, 6, "袖長（mm）：", self.length_var)
        self._number_row(panel, 7, "袖口寬（mm）：", self.cuff_var)
        ttk.Label(panel, textvariable=self.value_source_var,
                  wraplength=360, justify="left").grid(
                      row=8, column=1, columnspan=2, sticky="ew", pady=(0, 4))
        self._number_row(panel, 9, "縫份（mm）：", self.seam_var)
        self._number_row(panel, 10, "袖口折口距離（mm）：",
                         self.cuff_fold_var)
        ttk.Checkbutton(panel, text="鏡射接合",
                        variable=self.mirror_join_var).grid(
                            row=10, column=2, sticky="w", padx=(8, 0),
                            pady=4)
        actions = ttk.Frame(panel)
        actions.grid(row=11, column=0, columnspan=3, sticky="ew", pady=(6, 4))
        actions.columnconfigure((0, 1, 2), weight=1)
        ttk.Button(actions, text="管理尺寸", command=self._open_size_manager
                   ).grid(row=0, column=0, sticky="ew", padx=(0, 2))
        ttk.Button(actions, text="選擇尺寸檔",
                   command=self._choose_size_data_file).grid(
                       row=0, column=1, sticky="ew", padx=2)
        ttk.Button(actions, text="重新載入", command=self._reload_size_table
                   ).grid(row=0, column=2, sticky="ew", padx=(2, 0))
        actions2 = ttk.Frame(panel)
        actions2.grid(row=12, column=0, columnspan=3, sticky="ew", pady=4)
        actions2.columnconfigure((0, 1), weight=1)
        ttk.Button(actions2, text="還原款式值", command=self._restore_variant
                   ).grid(row=0, column=0, sticky="ew", padx=(0, 3))
        ttk.Button(actions2, text="儲存至目前款式",
                   command=self._save_current_variant).grid(
                       row=0, column=1, sticky="ew", padx=(3, 0))
        ttk.Button(panel, text="另存新款式", command=self._save_new_variant
                   ).grid(row=13, column=0, columnspan=3, sticky="ew", pady=4)
        self.generate_button = ttk.Button(
            panel, text="產生長袖 DXF", command=self._generate, state="disabled")
        self.generate_button.grid(row=14, column=0, columnspan=3,
                                  sticky="ew", pady=(6, 4))
        ttk.Label(panel, textvariable=self.validation_var, foreground="#a33",
                  wraplength=360, justify="left").grid(
                      row=15, column=0, columnspan=3, sticky="new", pady=4)
        ttk.Label(panel, text="結果：").grid(row=16, column=0, sticky="nw",
                                            pady=(8, 2))
        self.status = tk.Text(panel, width=42, height=8, state="disabled",
                              wrap="word")
        self.status.grid(row=17, column=0, columnspan=3, sticky="nsew")
        panel.rowconfigure(17, weight=1)
        self._build_batch_widgets(batch_page)

    def _build_batch_widgets(self, parent: ttk.Frame) -> None:
        parent.columnconfigure(1, weight=1)
        parent.rowconfigure(6, weight=1)
        self._path_row(parent, 0, "尺寸 Excel：", self.batch_excel_var,
                       self._browse_batch_excel)
        self._path_row(parent, 1, "來源資料夾：", self.batch_folder_var,
                       self._browse_batch_folder)
        ttk.Label(parent, text="批次名稱：").grid(
            row=2, column=0, sticky="w", padx=(0, 8), pady=4)
        ttk.Entry(parent, textvariable=self.batch_name_var).grid(
            row=2, column=1, sticky="ew", pady=4)
        ttk.Label(parent, text="JSON 款式／輸出子資料夾").grid(
            row=2, column=2, sticky="w", padx=(8, 0), pady=4)
        ttk.Label(parent, text="延伸名稱：").grid(
            row=3, column=0, sticky="w", padx=(0, 8), pady=4)
        ttk.Entry(parent, textvariable=self.extension_name_var).grid(
            row=3, column=1, sticky="ew", pady=4)
        ttk.Label(parent, text="輸出檔名：原檔名_,延伸名稱.原附檔名").grid(
            row=3, column=2, sticky="w", padx=(8, 0), pady=4)
        ttk.Checkbutton(parent, text="鏡射接合",
                        variable=self.mirror_join_var).grid(
                            row=4, column=0, columnspan=3, sticky="w",
                            pady=4)
        ttk.Label(parent, text="合併目標 JSON：").grid(
            row=5, column=0, sticky="nw", padx=(0, 8), pady=4)
        ttk.Label(parent, textvariable=self.batch_json_path_var,
                  wraplength=760, justify="left").grid(
                      row=5, column=1, columnspan=2, sticky="ew", pady=4)

        columns = ("size", "sleeve", "flat", "cuff")
        self.batch_tree = ttk.Treeview(
            parent, columns=columns, show="headings", height=12)
        headings = (("size", "尺寸"), ("sleeve", "袖長（mm）"),
                    ("flat", "袖口平量（mm）"),
                    ("cuff", "寫入 JSON 袖口寬（mm）"))
        for column, title in headings:
            self.batch_tree.heading(column, text=title)
        self.batch_tree.column("size", width=120, anchor="center")
        for column in columns[1:]:
            self.batch_tree.column(column, width=190, anchor="e")
        self.batch_tree.grid(row=6, column=0, columnspan=3, sticky="nsew",
                             pady=(8, 4))
        actions = ttk.Frame(parent)
        actions.grid(row=7, column=0, columnspan=3, sticky="ew", pady=4)
        actions.columnconfigure((0, 1, 2, 3), weight=1)
        ttk.Button(actions, text="讀取 Excel",
                   command=self._load_batch_excel).grid(
                       row=0, column=0, sticky="ew", padx=(0, 4))
        ttk.Button(actions, text="合併寫入單件尺寸 JSON",
                   command=self._merge_batch_excel).grid(
                       row=0, column=1, sticky="ew", padx=4)
        ttk.Button(actions, text="刪除合併款式",
                   command=self._delete_merged_json_variant).grid(
                       row=0, column=2, sticky="ew", padx=4)
        ttk.Button(actions, text="批次產生長袖 DXF",
                   command=self._batch_convert).grid(
                       row=0, column=3, sticky="ew", padx=(4, 0))
        ttk.Label(parent, textvariable=self.batch_status_var,
                  wraplength=900, justify="left").grid(
                      row=8, column=0, columnspan=3, sticky="ew", pady=4)
        self.batch_log = tk.Text(parent, height=8, state="disabled",
                                 wrap="word")
        self.batch_log.grid(row=9, column=0, columnspan=3, sticky="nsew",
                            pady=(4, 0))
        parent.rowconfigure(9, weight=1)

    def _browse_batch_excel(self) -> None:
        filename = filedialog.askopenfilename(
            title="選擇尺寸 Excel",
            filetypes=[("Excel 活頁簿", "*.xlsx *.xlsm"),
                       ("所有檔案", "*.*")])
        if filename:
            self.batch_excel_var.set(filename)
            self._load_batch_excel()

    def _browse_batch_folder(self) -> None:
        folder = filedialog.askdirectory(title="選擇來源 DXF 資料夾")
        if folder:
            self.batch_folder_var.set(folder)

    def _load_batch_excel(self) -> None:
        path_text = self.batch_excel_var.get().strip()
        if not path_text:
            messagebox.showwarning("讀取尺寸 Excel", "請先選擇 Excel 檔案。")
            return
        path = Path(path_text).expanduser()
        try:
            sizes = read_size_values_from_excel(path)
        except (OSError, SizeDataError, ValueError) as error:
            self._batch_sizes = {}
            for item in self.batch_tree.get_children():
                self.batch_tree.delete(item)
            self.batch_status_var.set(f"Excel 讀取失敗：{error}")
            self._append_batch_log(f"讀取失敗：{error}")
            return

        self._batch_sizes = sizes
        for item in self.batch_tree.get_children():
            self.batch_tree.delete(item)
        for name, values in sizes.items():
            self.batch_tree.insert(
                "", "end", values=(
                    name, _format_number(values.sleeve_length_mm),
                    _format_number(values.cuff_flat_width_mm),
                    _format_number(values.cuff_width_mm)))
        self.batch_status_var.set(
            f"已讀取 {len(sizes)} 個尺寸：{path}\n"
            "袖口平量會乘 2，合併時以批次名稱新增或更新款式。")
        self._append_batch_log(f"已讀取 Excel：{path}（{len(sizes)} 個尺寸）")

    def _merge_batch_excel(self) -> None:
        try:
            batch_name = validate_batch_name(self.batch_name_var.get())
        except SizeDataError as error:
            messagebox.showwarning("批次名稱無效", str(error))
            return
        if not self._batch_sizes:
            self._load_batch_excel()
            if not self._batch_sizes:
                return
        if self.size_table is None:
            messagebox.showerror("合併尺寸失敗", self._size_load_error)
            return
        try:
            merged, added, updated = merge_excel_size_values(
                self.size_table, self._batch_sizes, batch_name)
            merged.save()
        except (OSError, SizeDataError, ValueError) as error:
            self.batch_status_var.set(f"合併失敗：{error}")
            self._append_batch_log(f"合併失敗：{error}")
            messagebox.showerror("合併尺寸失敗", str(error))
            return

        self.size_table = merged
        self._populate_size_choices()
        self.batch_json_path_var.set(str(merged.path))
        if self.size_var.get() in self.size_table.sizes():
            self._select_size(self.size_var.get(), manual=False,
                              variant=self.variant_var.get())
        summary = (f"已合併到單件尺寸 JSON 的「{batch_name}」款式："
                   f"新增 {added} 個、更新 {updated} 個。\n"
                   f"{merged.path}\n備份：{merged.path}.bak")
        self.batch_status_var.set(summary)
        self._append_batch_log(summary)
        self._update_validation()

    def _delete_merged_json_variant(self) -> None:
        try:
            batch_name = validate_batch_name(self.batch_name_var.get())
        except SizeDataError as error:
            messagebox.showwarning("批次名稱無效", str(error))
            return
        if self.size_table is None:
            messagebox.showerror("刪除合併款式失敗", self._size_load_error)
            return
        try:
            updated, variant_count, size_count = delete_named_variant(
                self.size_table, batch_name)
        except SizeDataError as error:
            messagebox.showinfo("刪除合併款式", str(error))
            return

        detail = (f"將從 {variant_count} 個尺寸移除「{batch_name}」款式。"
                  f"其中 {size_count} 個尺寸只剩此款，會一併刪除尺寸記錄。\n"
                  "此操作不會刪除已產生的 DXF 檔。\n\n"
                  f"尺寸 JSON：{self.size_table.path}")
        if not messagebox.askyesno("刪除合併款式", detail):
            return
        try:
            updated.save()
        except (OSError, SizeDataError, ValueError) as error:
            self.batch_status_var.set(f"刪除失敗：{error}")
            self._append_batch_log(f"刪除失敗：{error}")
            messagebox.showerror("刪除合併款式失敗", str(error))
            return

        previous_size = self.size_var.get()
        previous_variant = self.variant_var.get()
        self.size_table = updated
        self.batch_json_path_var.set(str(updated.path))
        self._populate_size_choices()
        if previous_size in updated.sizes():
            variant = (previous_variant if previous_variant in updated.variants(previous_size)
                       else updated.default_variant(previous_size))
            self._select_size(previous_size, manual=False, variant=variant)
        elif self.input_var.get().strip():
            self._on_input_var_changed()
        else:
            self._clear_selection()
        summary = (f"已刪除「{batch_name}」款式：{variant_count} 筆，"
                   f"刪除僅含此款的尺寸 {size_count} 個。\n"
                   f"備份：{updated.path}.bak")
        self.batch_status_var.set(summary)
        self._append_batch_log(summary)
        self._update_validation()

    def _batch_convert(self) -> None:
        if not self._batch_sizes:
            self._load_batch_excel()
            if not self._batch_sizes:
                return
        folder_text = self.batch_folder_var.get().strip()
        source_folder = Path(folder_text).expanduser() if folder_text else None
        if source_folder is None or not source_folder.is_dir():
            messagebox.showwarning("批次轉換", "請先選擇有效的來源資料夾。")
            return
        try:
            batch_name = validate_batch_name(self.batch_name_var.get())
        except SizeDataError as error:
            messagebox.showwarning(
                "批次名稱無效", str(error))
            return
        try:
            extension_name = validate_batch_name(self.extension_name_var.get())
        except SizeDataError as error:
            messagebox.showwarning("延伸名稱無效", str(error))
            return
        try:
            seam_allowance = _numeric_value(self.seam_var.get(), "縫份",
                                            allow_zero=True)
            cuff_fold = _numeric_value(self.cuff_fold_var.get(),
                                       "袖口折口距離")
        except SizeDataError as error:
            messagebox.showwarning("批次轉換設定錯誤", str(error))
            return

        try:
            source_files = sorted(
                (path for path in source_folder.iterdir()
                 if path.is_file() and path.suffix.lower() == ".dxf"),
                key=lambda path: path.name.casefold())
        except OSError as error:
            messagebox.showerror(
                "讀取來源資料夾失敗", f"無法讀取 {source_folder}\n原因：{error}")
            return
        if not source_files:
            messagebox.showinfo("批次轉換", "來源資料夾沒有 DXF 檔案。")
            return

        output_folder = source_folder / batch_name
        records = []
        for input_path in source_files:
            detection = detect_size_from_filename(
                input_path.name, tuple(self._batch_sizes))
            if not detection.recognized:
                records.append(f"略過 {input_path.name}：{detection.reason}")
                continue
            values = self._batch_sizes[detection.size or ""]
            output_path = output_folder / (
                f"{input_path.stem}_,{extension_name}{input_path.suffix}")
            if output_path.exists():
                records.append(f"略過 {input_path.name}：輸出已存在 {output_path.name}")
                continue
            try:
                output_folder.mkdir(parents=True, exist_ok=True)
                generate_file(
                    input_path, output_path, values.sleeve_length_mm,
                    values.cuff_width_mm, seam_allowance, False, cuff_fold,
                    self.mirror_join_var.get())
            except (OSError, SizeDataError, ValueError) as error:
                records.append(f"失敗 {input_path.name}：{error}")
                continue
            records.append(
                f"完成 {input_path.name} → {output_path.name}"
                f"（尺寸 {detection.size}）")

        successes = sum(record.startswith("完成 ") for record in records)
        failures = sum(record.startswith("失敗 ") for record in records)
        skipped = len(records) - successes - failures
        summary = (f"批次「{batch_name}」完成：成功 {successes}、"
                   f"略過 {skipped}、失敗 {failures}。\n"
                   f"輸出資料夾：{output_folder}")
        self.batch_status_var.set(summary)
        self._append_batch_log(summary)
        for record in records:
            self._append_batch_log(record)
        messagebox.showinfo("批次轉換完成", summary)

    def _append_batch_log(self, text: str) -> None:
        self.batch_log.configure(state="normal")
        self.batch_log.insert("end", text.rstrip() + "\n")
        self.batch_log.see("end")
        self.batch_log.configure(state="disabled")

    @staticmethod
    def _path_row(parent: ttk.Frame, row: int, label: str,
                  variable: tk.StringVar, command) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w",
                                           padx=(0, 8), pady=4)
        ttk.Entry(parent, textvariable=variable, width=42).grid(
            row=row, column=1, sticky="ew", pady=4)
        ttk.Button(parent, text="瀏覽…", command=command).grid(
            row=row, column=2, padx=(8, 0), pady=4)

    @staticmethod
    def _number_row(parent: ttk.Frame, row: int, label: str,
                    variable: tk.StringVar) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w",
                                           padx=(0, 8), pady=4)
        ttk.Entry(parent, textvariable=variable, width=18).grid(
            row=row, column=1, sticky="w", pady=4)

    def _populate_size_choices(self) -> None:
        sizes = self.size_table.sizes() if self.size_table is not None else ()
        self.size_combo["values"] = sizes
        if self.size_var.get() not in sizes:
            self.size_var.set("")
            self.variant_var.set("")
            self.variant_combo["values"] = ()

    def _on_input_var_changed(self, *_args) -> None:
        self._input_error = ""
        input_text = self.input_var.get().strip()
        if not input_text:
            self._detection = SizeDetection("unknown", None, (),
                                            "尚未載入來源 DXF")
            self.recognition_var.set(self._detection.reason)
            self._clear_selection()
            self._schedule_preview()
            return
        if self.size_table is None:
            self._detection = SizeDetection("unknown", None, (),
                                            self._size_load_error)
            self.recognition_var.set(self._size_load_error)
            self._clear_selection()
            self._schedule_preview()
            return
        self._detection = detect_size_from_filename(
            input_text, self.size_table.sizes())
        self.recognition_var.set(self._detection.reason)
        if self._detection.recognized:
            self._select_size(self._detection.size or "", manual=False)
        else:
            self._clear_selection()
        self._schedule_preview()

    def _clear_selection(self) -> None:
        self._selection_updating = True
        try:
            self.size_var.set("")
            self.variant_var.set("")
            self.variant_combo["values"] = ()
            self.length_var.set("")
            self.cuff_var.set("")
            self.value_source_var.set("")
        finally:
            self._selection_updating = False
        self._selection_is_manual = False
        self._update_validation()

    def _select_size(self, size: str, manual: bool = True,
                     variant: Optional[str] = None) -> None:
        if self.size_table is None or size not in self.size_table.sizes():
            return
        variants = self.size_table.variants(size)
        selected_variant = variant or self.size_table.default_variant(size)
        if selected_variant not in variants:
            selected_variant = self.size_table.default_variant(size)
        self._selection_updating = True
        try:
            self.size_var.set(size)
            self.variant_combo["values"] = variants
            self.variant_var.set(selected_variant)
        finally:
            self._selection_updating = False
        self._selection_is_manual = manual
        if manual:
            self.recognition_var.set(f"手動選取尺寸：{size}")
        self._load_selected_variant_values()

    def _on_size_selected(self, _event=None) -> None:
        if not self._selection_updating:
            self._select_size(self.size_var.get(), manual=True)

    def _on_variant_selected(self, _event=None) -> None:
        if self._selection_updating:
            return
        if self.size_table is None or not self.size_var.get():
            return
        try:
            self._load_selected_variant_values()
            self.recognition_var.set(
                f"手動選取尺寸：{self.size_var.get()}，款式：{self.variant_var.get()}")
        except SizeDataError as error:
            self.value_source_var.set(str(error))
            self._update_validation()

    def _load_selected_variant_values(self) -> None:
        if self.size_table is None or not self.size_var.get() \
                or not self.variant_var.get():
            self._update_validation()
            return
        try:
            values = self.size_table.effective_values(
                self.size_var.get(), self.variant_var.get())
        except SizeDataError as error:
            self._selection_updating = True
            try:
                self.length_var.set("")
                self.cuff_var.set("")
            finally:
                self._selection_updating = False
            self.value_source_var.set(str(error))
            self._update_validation()
            return
        self._selection_updating = True
        try:
            self.length_var.set(f"{values.sleeve_length_mm:g}")
            self.cuff_var.set(f"{values.cuff_width_mm:g}")
        finally:
            self._selection_updating = False
        if values.cuff_source == "同尺寸 standard":
            self.value_source_var.set(
                "袖口寬來源：同尺寸 standard（原款式為 0 的相容回退；"
                "儲存時會寫入目前款式的正值）")
        else:
            self.value_source_var.set("數值來源：目前款式")
        self._update_value_state()

    def _on_value_changed(self, *_args) -> None:
        self._update_value_state()
        self._schedule_preview()

    def _current_parameters(self) -> SleeveParameters:
        if not self.size_var.get() or not self.variant_var.get():
            raise ValueError("尚未選取有效尺寸與款式；請手動選擇。")
        return validate_generation_parameters(
            self.length_var.get(), self.cuff_var.get(), self.seam_var.get(),
            self.cuff_fold_var.get(), self.mirror_join_var.get())

    def _update_value_state(self) -> None:
        if not hasattr(self, "generate_button"):
            return
        error = ""
        temporary = ""
        try:
            if not self.input_var.get().strip():
                raise ValueError("請先選擇或拖入輸入 DXF。")
            if self._input_error:
                raise ValueError(self._input_error)
            if not self.output_var.get().strip():
                raise ValueError("請指定輸出 DXF。")
            if (Path(self.input_var.get()).resolve()
                    == Path(self.output_var.get()).resolve()):
                raise ValueError("輸出 DXF 必須是新檔案，不可覆蓋輸入袖片。")
            parameters = self._current_parameters()
            if self.size_table is not None:
                stored = self.size_table.effective_values(
                    self.size_var.get(), self.variant_var.get())
                if (abs(parameters.sleeve_length_mm - stored.sleeve_length_mm)
                        > EPS
                        or abs(parameters.cuff_width_mm - stored.cuff_width_mm)
                        > EPS):
                    temporary = "未存入尺寸表（目前值僅用於本次預覽與輸出）"
        except (SizeDataError, ValueError) as exc:
            error = str(exc)
        self.validation_var.set(error or temporary)
        self.generate_button.configure(state="disabled" if error
                                       else "normal")

    def _update_value_state_after_save(self) -> None:
        self._load_selected_variant_values()
        self._update_validation()

    def _update_validation(self) -> None:
        self._update_value_state()

    def _browse_input(self) -> None:
        path = filedialog.askopenfilename(
            title="選擇袖子 DXF", filetypes=[("DXF files", "*.dxf"),
                                            ("All files", "*.*")])
        if path:
            self._set_input_path(Path(path))

    def _set_input_path(self, path: Path) -> None:
        self.input_var.set(str(path))
        if not self.output_var.get():
            self.output_var.set(str(path.with_name(
                path.stem + "_long_sleeve.dxf")))

    def _enable_file_drop(self) -> None:
        """Enable safe Explorer drops through tkinterdnd2 when available."""
        if DND_FILES is None:
            return
        try:
            self.drop_target_register(DND_FILES)
            self.dnd_bind("<<Drop>>", self._on_dnd_drop)
        except tk.TclError:
            # The browse button remains available on unsupported Tk builds.
            pass

    def _on_dnd_drop(self, event) -> None:
        paths = self.tk.splitlist(event.data)
        self._handle_dropped_files(paths)

    def _handle_dropped_files(self, paths: Sequence[str]) -> None:
        if len(paths) != 1:
            messagebox.showwarning("拖放來源檔案", "一次請拖放一個 DXF 檔案。")
            return
        path = Path(paths[0])
        if path.suffix.lower() != ".dxf":
            messagebox.showwarning("拖放來源檔案", "來源檔案必須是 .dxf。")
            return
        self._set_input_path(path)
        self._set_status(f"已加入來源 DXF：{path}")

    def _schedule_preview(self, *_args) -> None:
        if not hasattr(self, "preview_canvas"):
            return
        if self._preview_after_id is not None:
            try:
                self.after_cancel(self._preview_after_id)
            except tk.TclError:
                pass
        self._preview_after_id = self.after(180, self._refresh_preview)

    def _refresh_preview(self) -> None:
        self._preview_after_id = None
        input_text = self.input_var.get().strip()
        self._input_error = ""
        if not input_text:
            self._current_preview = None
            self._preview_message = "請選擇或拖入來源 DXF"
            self._update_validation()
            self._draw_current_preview()
            return
        try:
            document = read_dxf(Path(input_text))
            analysis = find_sleeve(document)
            parameters = self._current_parameters()
            build_long_sleeve(
                analysis, document.unit_scale_mm,
                parameters.sleeve_length_mm, parameters.cuff_width_mm,
                parameters.seam_allowance_mm,
                parameters.cuff_fold_distance_mm,
                parameters.mirror_join)
            preview = build_preview(
                analysis, document.unit_scale_mm,
                parameters.sleeve_length_mm, parameters.cuff_width_mm,
                parameters.seam_allowance_mm,
                parameters.cuff_fold_distance_mm,
                parameters.mirror_join)
        except (OSError, SizeDataError, ValueError) as error:
            self._current_preview = None
            self._preview_message = f"預覽無法建立\n{error}"
            self._input_error = str(error)
            self._update_validation()
            self._draw_current_preview()
            return
        self._current_preview = preview
        self._preview_message = ""
        self._update_validation()
        self._draw_current_preview()

    def _open_size_manager(self) -> None:
        if self.size_table is None:
            messagebox.showerror("尺寸資料錯誤", self._size_load_error)
            return
        SizeManagerWindow(self, self.size_table, self._reload_size_table)

    def _choose_size_data_file(self) -> None:
        current_path = (self.size_table.path if self.size_table is not None
                        else default_size_data_path())
        initial_dir = current_path.parent
        if not initial_dir.is_dir():
            initial_dir = default_size_data_path().parent
        if not initial_dir.is_dir():
            initial_dir = Path.home()
        filename = filedialog.askopenfilename(
            title="選擇尺寸資料 JSON",
            initialdir=str(initial_dir),
            filetypes=[("JSON 尺寸資料", "*.json"), ("所有檔案", "*.*")])
        if not filename:
            return
        try:
            table = load_size_table(Path(filename))
        except (OSError, SizeDataError) as error:
            messagebox.showerror("載入尺寸資料失敗", str(error))
            self._set_status(f"載入尺寸資料失敗：{error}")
            return
        if self.size_table is not None and self.size_table.dirty:
            if not messagebox.askyesno(
                    "切換尺寸資料",
                    "目前有未儲存的尺寸表修改，切換資料檔會捨棄它。\n確定切換？"):
                return
        try:
            register_size_data_path(table.path)
        except SizeDataError as error:
            messagebox.showerror("登錄檔更新失敗", str(error))
            self._set_status(f"登錄檔更新失敗：{error}")
            return

        self.size_table = table
        self._size_load_error = ""
        self._registry_error = ""
        self.batch_json_path_var.set(str(table.path))
        self._populate_size_choices()
        if self.input_var.get().strip():
            self._on_input_var_changed()
        else:
            self._clear_selection()
            self.recognition_var.set("尺寸資料已載入並登錄")
        self._set_status(
            f"已載入尺寸資料並更新登錄檔：\n{table.path}\n"
            f"登錄值：{SIZE_DATA_REGISTRY_DISPLAY}")

    def _reload_size_table(self, *_args) -> None:
        if self.size_table is not None and self.size_table.dirty:
            if not messagebox.askyesno(
                    "重新載入尺寸", "目前有未儲存的尺寸表修改，重新載入會捨棄它。\n確定重新載入？"):
                return
        try:
            table = load_active_size_table()
        except SizeDataError as error:
            self._size_load_error = str(error)
            self.recognition_var.set(str(error))
            self._set_status(str(error))
            return
        self.size_table = table
        self._size_load_error = ""
        self._registry_error = ""
        self.batch_json_path_var.set(str(table.path))
        self._populate_size_choices()
        if self.input_var.get().strip():
            self._on_input_var_changed()
        else:
            self._clear_selection()
            self.recognition_var.set("尺寸資料已重新載入")
        self._set_status(f"已重新載入尺寸資料：{table.path}")

    def _restore_variant(self) -> None:
        if self.size_table is None or not self.size_var.get() \
                or not self.variant_var.get():
            self._set_status("請先選取尺寸與款式。")
            return
        self._load_selected_variant_values()
        self._set_status("已還原目前款式的尺寸表值。")

    def _size_values_from_entries(self) -> Tuple[float, float]:
        try:
            parameters = validate_generation_parameters(
                self.length_var.get(), self.cuff_var.get(),
                self.seam_var.get(), self.cuff_fold_var.get())
        except ValueError as error:
            raise SizeDataError(str(error)) from error
        return parameters.sleeve_length_mm, parameters.cuff_width_mm

    def _save_current_variant(self) -> None:
        if self.size_table is None or not self.size_var.get() \
                or not self.variant_var.get():
            messagebox.showwarning("儲存尺寸", "請先選取尺寸與款式。")
            return
        try:
            length_mm, cuff_mm = self._size_values_from_entries()
            self.size_table.update_variant(self.size_var.get(),
                                           self.variant_var.get(),
                                           length_mm, cuff_mm)
            self.size_table.save()
            self._load_selected_variant_values()
            self._set_status(
                f"已儲存至 {self.size_var.get()}/{self.variant_var.get()}。\n"
                f"備份：{self.size_table.path}.bak")
        except (OSError, SizeDataError, ValueError) as error:
            self._set_status(f"尺寸表儲存失敗：{error}")
            messagebox.showerror("尺寸表儲存失敗", str(error))

    def _save_new_variant(self) -> None:
        if self.size_table is None or not self.size_var.get():
            messagebox.showwarning("另存新款式", "請先選取尺寸。")
            return
        name = simpledialog.askstring("另存新款式", "新款式名稱：", parent=self)
        if name is None:
            return
        try:
            length_mm, cuff_mm = self._size_values_from_entries()
            self.size_table.add_variant(self.size_var.get(), name,
                                        length_mm, cuff_mm)
            self.size_table.save()
            self._select_size(self.size_var.get(), manual=True,
                              variant=name.strip())
            self._set_status(
                f"已建立並儲存款式：{self.size_var.get()}/{name.strip()}。")
        except (OSError, SizeDataError, ValueError) as error:
            self._set_status(f"新款式儲存失敗：{error}")
            messagebox.showerror("新款式儲存失敗", str(error))

    def _draw_current_preview(self) -> None:
        if not hasattr(self, "preview_canvas"):
            return
        canvas = self.preview_canvas
        canvas.delete("all")
        width = max(float(canvas.winfo_width()), 1.0)
        height = max(float(canvas.winfo_height()), 1.0)
        if self._current_preview is None:
            canvas.create_text(
                width * 0.5, height * 0.5,
                text=self._preview_message or "請選擇來源 DXF",
                fill="#4b5563", justify="center", width=width - 48,
                font=("Segoe UI", 11))
            return

        preview = self._current_preview
        transform = make_preview_transform(preview, width, height)

        def draw_path(points: Sequence[Point], color: str, line_width: float,
                      dash=None) -> None:
            if len(points) < 2:
                return
            coordinates = []
            for point in points:
                screen = preview_to_screen(transform, point)
                coordinates.extend(screen)
            options = {"fill": color, "width": line_width,
                       "smooth": False}
            if dash is not None:
                options["dash"] = dash
            canvas.create_line(*coordinates, **options)

        def draw_marker(point: Point, label: str, color: str) -> None:
            x, y = preview_to_screen(transform, point)
            radius = 4.5
            canvas.create_oval(x - radius, y - radius, x + radius, y + radius,
                               fill=color, outline=color)
            canvas.create_text(x + 8, y - 7, text=label, fill=color,
                               anchor="sw", font=("Segoe UI", 9))

        draw_path(preview.outline, "#9aa0aa", 1.2)
        draw_path(preview.cut_outline, "#00d5dd", 1.8)
        draw_path(preview.cap, "#ff5ad2", 3.0)
        draw_path(preview.cuff, "#3cbefb", 3.0)
        draw_path([preview.axis_start, preview.axis_end], "#4be682", 2.0,
                  dash=(7, 4))
        draw_path(preview.upper_side, "#b478ff", 2.5)
        draw_path(preview.lower_side, "#ffdc50", 2.5)

        draw_marker(preview.point1, "P1", "#ffcd46")
        draw_marker(preview.point2, "P2", "#ffcd46")
        draw_marker(preview.point3, "P3", "#ff734b")
        draw_marker(preview.cuff_center, "袖口中心", "#3cbefb")
        if (preview.target_cuff_center is not None
                and preview.target_cuff_upper is not None
                and preview.target_cuff_lower is not None):
            draw_path([preview.target_cuff_upper,
                       preview.target_cuff_lower], "#ff9b37", 3.0)
            draw_marker(preview.target_cuff_center, "目標袖口中心",
                        "#ff9b37")
            draw_marker(preview.target_cuff_upper, "目標袖口上",
                        "#ff9b37")
            draw_marker(preview.target_cuff_lower, "目標袖口下",
                        "#ff9b37")

        canvas.create_text(
            14, 18, text="袖片幾何預覽（袖山／縫份／折口）",
            fill="#374151", anchor="nw", font=("Segoe UI", 10))

    def _browse_output(self) -> None:
        path = filedialog.asksaveasfilename(
            title="儲存長袖 DXF", defaultextension=".dxf",
            filetypes=[("DXF files", "*.dxf"), ("All files", "*.*")])
        if path:
            self.output_var.set(path)

    def _set_status(self, text: str) -> None:
        self.status.configure(state="normal")
        self.status.delete("1.0", "end")
        self.status.insert("1.0", text)
        self.status.configure(state="disabled")

    def _generate(self) -> None:
        if not self.input_var.get().strip() or not self.output_var.get().strip():
            messagebox.showwarning("資料不足", "請先選擇輸入與輸出 DXF。")
            return
        try:
            parameters = self._current_parameters()
            result = generate_file(
                Path(self.input_var.get()), Path(self.output_var.get()),
                parameters.sleeve_length_mm, parameters.cuff_width_mm,
                parameters.seam_allowance_mm, False,
                parameters.cuff_fold_distance_mm,
                parameters.mirror_join)
            self._set_status(result_summary(result, Path(self.output_var.get())))
            self._refresh_preview()
            messagebox.showinfo("完成", "長袖 DXF 已產生。")
        except (OSError, SizeDataError, ValueError) as error:
            self._set_status(f"失敗：{error}")
            messagebox.showerror("產生失敗", str(error))


class SizeManagerWindow(tk.Toplevel):
    """Transactional editor: work on a clone and write only on Save."""

    def __init__(self, parent: SleeveGui, table: SizeTable,
                 on_saved) -> None:
        super().__init__(parent)
        self.parent_gui = parent
        self.working = table.clone()
        self.on_saved = on_saved
        self.title("管理尺寸與款式")
        self.geometry("820x600")
        self.minsize(720, 500)
        self.transient(parent)
        self.protocol("WM_DELETE_WINDOW", self._close)
        self._build_widgets()
        self._populate_sizes()

    def _build_widgets(self) -> None:
        self.rowconfigure(0, weight=1)
        self.columnconfigure(1, weight=1)
        left = ttk.LabelFrame(self, text="尺寸", padding=8)
        left.grid(row=0, column=0, sticky="nsew", padx=(8, 4), pady=8)
        left.rowconfigure(0, weight=1)
        left.columnconfigure(0, weight=1)
        self.size_list = tk.Listbox(left, exportselection=False,
                                    activestyle="dotbox")
        self.size_list.grid(row=0, column=0, columnspan=2, sticky="nsew")
        self.size_list.bind("<<ListboxSelect>>", self._on_size_selected)
        for column, (label, command) in enumerate((
                ("新增", self._add_size), ("複製", self._copy_size),
                ("改名", self._rename_size), ("刪除", self._delete_size))):
            ttk.Button(left, text=label, command=command).grid(
                row=1 + column // 2, column=column % 2, sticky="ew",
                padx=(0 if column % 2 == 0 else 3, 3 if column % 2 == 0 else 0),
                pady=(8 if column < 2 else 3, 0))

        right = ttk.Frame(self, padding=8)
        right.grid(row=0, column=1, sticky="nsew", padx=(4, 8), pady=8)
        right.rowconfigure(0, weight=1)
        right.columnconfigure(0, weight=1)
        self.variant_tree = ttk.Treeview(
            right, columns=("name", "length", "cuff", "default"),
            show="headings", selectmode="browse")
        for column, heading, width in (("name", "名稱", 160),
                                       ("length", "袖長（mm）", 120),
                                       ("cuff", "袖口寬（mm）", 120),
                                       ("default", "預設", 70)):
            self.variant_tree.heading(column, text=heading)
            self.variant_tree.column(column, width=width, anchor="w")
        self.variant_tree.grid(row=0, column=0, sticky="nsew")
        self.variant_tree.bind("<<TreeviewSelect>>", self._on_variant_selected)

        variant_actions = ttk.Frame(right)
        variant_actions.grid(row=1, column=0, sticky="ew", pady=(8, 4))
        for column, (label, command) in enumerate((
                ("新增款式", self._add_variant), ("複製款式", self._copy_variant),
                ("改名", self._rename_variant), ("刪除", self._delete_variant),
                ("設為預設", self._set_default))):
            ttk.Button(variant_actions, text=label, command=command).grid(
                row=0, column=column, sticky="ew", padx=2)
            variant_actions.columnconfigure(column, weight=1)

        form = ttk.LabelFrame(right, text="款式編輯（mm）", padding=8)
        form.grid(row=2, column=0, sticky="ew", pady=(4, 0))
        form.columnconfigure(1, weight=1)
        ttk.Label(form, text="名稱：").grid(row=0, column=0, sticky="w",
                                            padx=(0, 8), pady=3)
        self.form_variant_var = tk.StringVar()
        ttk.Entry(form, textvariable=self.form_variant_var).grid(
            row=0, column=1, sticky="ew", pady=3)
        ttk.Label(form, text="袖長：").grid(row=1, column=0, sticky="w",
                                            padx=(0, 8), pady=3)
        self.form_length_var = tk.StringVar()
        ttk.Entry(form, textvariable=self.form_length_var).grid(
            row=1, column=1, sticky="ew", pady=3)
        ttk.Label(form, text="袖口寬：").grid(row=2, column=0, sticky="w",
                                              padx=(0, 8), pady=3)
        self.form_cuff_var = tk.StringVar()
        ttk.Entry(form, textvariable=self.form_cuff_var).grid(
            row=2, column=1, sticky="ew", pady=3)
        self.form_default_var = tk.BooleanVar()
        ttk.Checkbutton(form, text="設為此尺寸預設款",
                        variable=self.form_default_var).grid(
                            row=3, column=0, columnspan=2, sticky="w", pady=3)
        ttk.Button(form, text="套用編輯", command=self._apply_form).grid(
            row=4, column=0, columnspan=2, sticky="ew", pady=(6, 0))

        bottom = ttk.Frame(self, padding=(8, 0, 8, 8))
        bottom.grid(row=1, column=0, columnspan=2, sticky="ew")
        bottom.columnconfigure(0, weight=1)
        bottom.columnconfigure(1, weight=1)
        ttk.Button(bottom, text="儲存", command=self._save).grid(
            row=0, column=0, sticky="ew", padx=(0, 4))
        ttk.Button(bottom, text="取消", command=self._close).grid(
            row=0, column=1, sticky="ew", padx=(4, 0))

    def _selected_size(self) -> Optional[str]:
        selection = self.size_list.curselection()
        return self.size_list.get(selection[0]) if selection else None

    def _selected_variant(self) -> Optional[str]:
        selection = self.variant_tree.selection()
        if not selection:
            return None
        return str(self.variant_tree.item(selection[0], "values")[0])

    def _populate_sizes(self, selected: Optional[str] = None) -> None:
        names = self.working.sizes()
        self.size_list.delete(0, "end")
        for name in names:
            self.size_list.insert("end", name)
        if not names:
            self._populate_variants()
            return
        chosen = selected if selected in names else names[0]
        index = names.index(chosen)
        self.size_list.selection_set(index)
        self.size_list.activate(index)
        self._populate_variants()

    def _populate_variants(self, selected: Optional[str] = None) -> None:
        for item in self.variant_tree.get_children():
            self.variant_tree.delete(item)
        size = self._selected_size()
        if not size:
            self._clear_form()
            return
        raw = self.working.to_dict()[size]
        default = self.working.default_variant(size)
        names = self.working.variants(size)
        chosen_item = None
        for name in names:
            values = raw["variants"][name]
            item = self.variant_tree.insert(
                "", "end", values=(name, values["sleeve_length_mm"],
                                    values["cuff_width_mm"],
                                    "是" if name == default else ""))
            if name == selected:
                chosen_item = item
        if chosen_item is None and names:
            chosen_item = self.variant_tree.get_children()[0]
        if chosen_item:
            self.variant_tree.selection_set(chosen_item)
            self.variant_tree.focus(chosen_item)
            self._load_form()
        else:
            self._clear_form()

    def _clear_form(self) -> None:
        self.form_variant_var.set("")
        self.form_length_var.set("")
        self.form_cuff_var.set("")
        self.form_default_var.set(False)

    def _load_form(self) -> None:
        size = self._selected_size()
        variant = self._selected_variant()
        if not size or not variant:
            self._clear_form()
            return
        values = self.working.to_dict()[size]["variants"][variant]
        self.form_variant_var.set(variant)
        self.form_length_var.set(str(values["sleeve_length_mm"]))
        self.form_cuff_var.set(str(values["cuff_width_mm"]))
        self.form_default_var.set(self.working.default_variant(size) == variant)

    def _on_size_selected(self, _event=None) -> None:
        self._populate_variants()

    def _on_variant_selected(self, _event=None) -> None:
        self._load_form()

    @staticmethod
    def _ask_measure(title: str, prompt: str, initial: str) -> Optional[float]:
        value = simpledialog.askstring(title, prompt, initialvalue=initial)
        if value is None:
            return None
        try:
            return _numeric_value(value, prompt)
        except SizeDataError as error:
            messagebox.showerror("輸入錯誤", str(error))
            return None

    def _add_size(self) -> None:
        name = simpledialog.askstring("新增尺寸", "尺寸名稱：", parent=self)
        if name is None:
            return
        length = self._ask_measure("新增尺寸", "袖長（mm）：", "627")
        if length is None:
            return
        cuff = self._ask_measure("新增尺寸", "袖口寬（mm）：", "225.17")
        if cuff is None:
            return
        try:
            self.working.add_size(name, length, cuff)
            self._populate_sizes(name.strip())
        except SizeDataError as error:
            messagebox.showerror("新增尺寸失敗", str(error))

    def _copy_size(self) -> None:
        source = self._selected_size()
        if not source:
            return
        name = simpledialog.askstring("複製尺寸", "新尺寸名稱：",
                                      initialvalue=f"{source}_copy",
                                      parent=self)
        if name is None:
            return
        try:
            self.working.copy_size(source, name)
            self._populate_sizes(name.strip())
        except SizeDataError as error:
            messagebox.showerror("複製尺寸失敗", str(error))

    def _rename_size(self) -> None:
        source = self._selected_size()
        if not source:
            return
        name = simpledialog.askstring("改名尺寸", "新尺寸名稱：",
                                      initialvalue=source, parent=self)
        if name is None:
            return
        try:
            self.working.rename_size(source, name)
            self._populate_sizes(name.strip())
        except SizeDataError as error:
            messagebox.showerror("改名尺寸失敗", str(error))

    def _delete_size(self) -> None:
        size = self._selected_size()
        if not size or not messagebox.askyesno(
                "刪除尺寸", f"確定刪除尺寸 {size} 及其所有款式？", parent=self):
            return
        try:
            self.working.delete_size(size)
            self._populate_sizes()
        except SizeDataError as error:
            messagebox.showerror("刪除尺寸失敗", str(error))

    def _add_variant(self) -> None:
        size = self._selected_size()
        if not size:
            return
        name = simpledialog.askstring("新增款式", "款式名稱：", parent=self)
        if name is None:
            return
        length = self._ask_measure("新增款式", "袖長（mm）：", "627")
        if length is None:
            return
        cuff = self._ask_measure("新增款式", "袖口寬（mm）：", "225.17")
        if cuff is None:
            return
        try:
            self.working.add_variant(size, name, length, cuff)
            self._populate_variants(name.strip())
        except SizeDataError as error:
            messagebox.showerror("新增款式失敗", str(error))

    def _copy_variant(self) -> None:
        size = self._selected_size()
        source = self._selected_variant()
        if not size or not source:
            return
        name = simpledialog.askstring("複製款式", "新款式名稱：",
                                      initialvalue=f"{source}_copy",
                                      parent=self)
        if name is None:
            return
        try:
            self.working.copy_variant(size, source, name)
            self._populate_variants(name.strip())
        except SizeDataError as error:
            messagebox.showerror("複製款式失敗", str(error))

    def _rename_variant(self) -> None:
        size = self._selected_size()
        source = self._selected_variant()
        if not size or not source:
            return
        name = simpledialog.askstring("改名款式", "新款式名稱：",
                                      initialvalue=source, parent=self)
        if name is None:
            return
        try:
            self.working.rename_variant(size, source, name)
            self._populate_variants(name.strip())
        except SizeDataError as error:
            messagebox.showerror("改名款式失敗", str(error))

    def _delete_variant(self) -> None:
        size = self._selected_size()
        variant = self._selected_variant()
        if not size or not variant:
            return
        replacement = None
        if self.working.default_variant(size) == variant:
            alternatives = [name for name in self.working.variants(size)
                            if name != variant]
            replacement = simpledialog.askstring(
                "刪除預設款", f"請指定替代款式（可選：{'、'.join(alternatives)}）：",
                parent=self)
            if replacement is None:
                return
        if not messagebox.askyesno("刪除款式", f"確定刪除 {size}/{variant}？",
                                   parent=self):
            return
        try:
            self.working.delete_variant(size, variant, replacement)
            self._populate_variants()
        except SizeDataError as error:
            messagebox.showerror("刪除款式失敗", str(error))

    def _set_default(self) -> None:
        size = self._selected_size()
        variant = self._selected_variant()
        if not size or not variant:
            return
        try:
            self.working.set_default(size, variant)
            self._populate_variants(variant)
        except SizeDataError as error:
            messagebox.showerror("設定預設失敗", str(error))

    def _apply_form(self) -> None:
        size = self._selected_size()
        old_variant = self._selected_variant()
        if not size or not old_variant:
            return
        new_variant = self.form_variant_var.get()
        try:
            if new_variant.strip() != old_variant:
                self.working.rename_variant(size, old_variant, new_variant)
                old_variant = new_variant.strip()
            old_cuff = self.working.to_dict()[size]["variants"][old_variant][
                "cuff_width_mm"]
            self.working.update_variant(
                size, old_variant, self.form_length_var.get(),
                self.form_cuff_var.get(),
                allow_legacy_zero=(old_cuff == 0
                                    and self.form_cuff_var.get().strip() == "0"))
            if self.form_default_var.get():
                self.working.set_default(size, old_variant)
            self._populate_variants(old_variant)
        except (SizeDataError, ValueError) as error:
            messagebox.showerror("套用款式失敗", str(error))

    def _save(self) -> None:
        try:
            self.working.save()
        except (OSError, SizeDataError, ValueError) as error:
            messagebox.showerror("儲存尺寸失敗", str(error))
            return
        self.on_saved()
        self.destroy()

    def _close(self) -> None:
        if self.working.dirty and not messagebox.askyesno(
                "取消管理尺寸", "有未儲存修改，確定放棄？", parent=self):
            return
        self.destroy()


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Python 長袖 DXF 產生器")
    parser.add_argument("input", nargs="?", type=Path, help="輸入袖子 DXF")
    parser.add_argument("output", nargs="?", type=Path, help="輸出長袖 DXF")
    parser.add_argument("--sleeve-length", type=float, default=627.0)
    parser.add_argument("--cuff-width", type=float, default=225.17)
    parser.add_argument("--seam-allowance", type=float,
                        default=DEFAULT_SEAM_ALLOWANCE_MM)
    parser.add_argument("--cuff-fold-distance", type=float,
                        default=DEFAULT_CUFF_FOLD_DISTANCE_MM,
                        help="袖口裁片線至折口的距離，預設 19.05 mm（0.75 in）")
    parser.add_argument("--include-source", action="store_true",
                        help="額外輸出原始袖片（GUI 預設不輸出）")
    # Keep the earlier switch accepted for scripts that already used it;
    # source output is now off by default.
    parser.add_argument("--no-source", action="store_true",
                        help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.input is not None or args.output is not None:
        if args.input is None or args.output is None:
            parser.error("CLI 模式需要同時指定 input 與 output")
        try:
            result = generate_file(args.input, args.output, args.sleeve_length,
                                   args.cuff_width, args.seam_allowance,
                                   args.include_source and not args.no_source,
                                   args.cuff_fold_distance)
            print(result_summary(result, args.output))
            return 0
        except (OSError, ValueError) as error:
            print(f"產生失敗：{error}")
            return 1
    SleeveGui().mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
