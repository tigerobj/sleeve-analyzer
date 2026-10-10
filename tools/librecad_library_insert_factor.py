"""Read or update LibreCAD's persisted Library Insert factor on Windows.

Examples:
    python librecad_library_insert_factor.py
    python librecad_library_insert_factor.py get
    python librecad_library_insert_factor.py set 1
    python librecad_library_insert_factor.py set 10 --yes

LibreCAD is installed as a 32-bit application on this computer, so ``set``
targets the 32-bit registry view. ``get`` shows both views for comparison.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
import sys
from typing import Any


REGISTRY_SUBKEY = r"Software\LibreCAD\LibreCAD\LibraryInsert"
REGISTRY_DISPLAY_PATH = (
    r"HKEY_CURRENT_USER\Software\LibreCAD\LibreCAD\LibraryInsert"
)
VALUE_NAME = "LibraryInsertFactor"


@dataclass(frozen=True)
class RegistryEntry:
    value: Any
    value_type: int


def normalize_factor(raw_value: str) -> str:
    """Return a trimmed, positive, finite numeric factor or raise ValueError."""
    value = raw_value.strip()
    if not value:
        raise ValueError("倍率不可為空")
    try:
        number = Decimal(value)
    except InvalidOperation as exc:
        raise ValueError("倍率必須是數字，例如 1、0.5 或 10") from exc
    if not number.is_finite() or number <= 0:
        raise ValueError("倍率必須是大於 0 的有限數字")
    return value


def read_factor(registry: Any, view_flag: int) -> RegistryEntry | None:
    """Read the named value from one explicit Windows registry view."""
    access = registry.KEY_QUERY_VALUE | view_flag
    try:
        with registry.OpenKey(
            registry.HKEY_CURRENT_USER, REGISTRY_SUBKEY, 0, access
        ) as key:
            value, value_type = registry.QueryValueEx(key, VALUE_NAME)
    except OSError as exc:
        if isinstance(exc, FileNotFoundError) or getattr(exc, "winerror", None) in (
            2,
            3,
        ):
            return None
        raise
    return RegistryEntry(value, value_type)


def set_factor(registry: Any, raw_value: str) -> RegistryEntry:
    """Write a REG_SZ factor to LibreCAD's 32-bit view and verify the result."""
    value = normalize_factor(raw_value)
    access = (
        registry.KEY_SET_VALUE
        | registry.KEY_QUERY_VALUE
        | registry.KEY_WOW64_32KEY
    )
    with registry.CreateKeyEx(
        registry.HKEY_CURRENT_USER, REGISTRY_SUBKEY, 0, access
    ) as key:
        registry.SetValueEx(key, VALUE_NAME, 0, registry.REG_SZ, value)

    written = read_factor(registry, registry.KEY_WOW64_32KEY)
    expected = RegistryEntry(value, registry.REG_SZ)
    if written != expected:
        raise OSError(f"寫入後讀回不符：預期 {expected!r}，實際 {written!r}")
    return written


def _load_winreg() -> Any:
    try:
        import winreg
    except ImportError as exc:
        raise RuntimeError("此工具只能在 Windows 上使用。") from exc
    return winreg


def _type_name(registry: Any, value_type: int) -> str:
    for name in ("REG_SZ", "REG_EXPAND_SZ", "REG_DWORD", "REG_QWORD", "REG_BINARY"):
        if getattr(registry, name, object()) == value_type:
            return name
    return f"Registry type {value_type}"


def _entry_text(registry: Any, entry: RegistryEntry | None) -> str:
    if entry is None:
        return "未設定（key 或 value 不存在）"
    return f"{entry.value!r}（{_type_name(registry, entry.value_type)}）"


def _show_both_views(registry: Any) -> int:
    print(f"登錄檔位置：{REGISTRY_DISPLAY_PATH}\\{VALUE_NAME}")
    for label, view_flag in (
        ("32 位元檢視", registry.KEY_WOW64_32KEY),
        ("64 位元檢視", registry.KEY_WOW64_64KEY),
    ):
        try:
            entry = read_factor(registry, view_flag)
        except OSError as exc:
            print(f"{label}：讀取失敗：{exc}", file=sys.stderr)
            return 2
        print(f"{label}：{_entry_text(registry, entry)}")
    return 0


def _confirm_write(factor: str) -> bool:
    try:
        answer = input(
            f"確定將倍率 {factor} 寫入 LibreCAD 的 32 位元登錄檢視？ [y/N] "
        )
    except EOFError:
        return False
    return answer.strip().lower() in ("y", "yes", "是")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="查看或修改 LibreCAD Library Insert 的持久化倍率。"
    )
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("get", help="顯示 32/64 位元檢視目前儲存的值")
    set_parser = commands.add_parser("set", help="修改 32 位元 LibreCAD 檢視的值")
    set_parser.add_argument("factor", help="要儲存的正數倍率，例如 1 或 10")
    set_parser.add_argument(
        "--yes", action="store_true", help="略過寫入前確認提示"
    )
    return parser


def launch_gui(registry: Any | None = None) -> int:
    """Open a small Tkinter window for viewing and saving the factor."""
    try:
        import tkinter as tk
        from tkinter import messagebox, ttk

        registry = registry if registry is not None else _load_winreg()
    except (ImportError, RuntimeError) as exc:
        print(f"無法啟動 GUI：{exc}", file=sys.stderr)
        return 2

    try:
        root = tk.Tk()
    except tk.TclError as exc:
        print(f"無法建立 GUI 視窗：{exc}", file=sys.stderr)
        return 2
    root.title("LibreCAD Library Insert 倍率設定")
    root.geometry("590x285")
    root.resizable(False, False)

    frame = ttk.Frame(root, padding=16)
    frame.grid(row=0, column=0, sticky="nsew")
    root.columnconfigure(0, weight=1)
    frame.columnconfigure(1, weight=1)

    ttk.Label(
        frame,
        text="讀取／修改 LibreCAD Library Insert 的持久化倍率",
    ).grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 12))

    ttk.Label(frame, text="32 位元目前值（LibreCAD 使用）：").grid(
        row=1, column=0, sticky="w", padx=(0, 12), pady=4
    )
    current_32 = tk.StringVar()
    ttk.Label(frame, textvariable=current_32).grid(
        row=1, column=1, sticky="w", pady=4
    )

    ttk.Label(frame, text="64 位元對照值（唯讀）：").grid(
        row=2, column=0, sticky="w", padx=(0, 12), pady=4
    )
    current_64 = tk.StringVar()
    ttk.Label(frame, textvariable=current_64).grid(
        row=2, column=1, sticky="w", pady=4
    )

    ttk.Label(frame, text="要儲存的新倍率：").grid(
        row=3, column=0, sticky="w", padx=(0, 12), pady=8
    )
    factor = tk.StringVar(value="1.0")
    factor_entry = ttk.Entry(frame, textvariable=factor, width=24)
    factor_entry.grid(row=3, column=1, sticky="ew", pady=8)

    ttk.Label(frame, text="登錄檔位置：").grid(
        row=4, column=0, sticky="nw", padx=(0, 12), pady=4
    )
    ttk.Label(
        frame,
        text=f"{REGISTRY_DISPLAY_PATH}\\{VALUE_NAME}",
        wraplength=390,
    ).grid(row=4, column=1, sticky="w", pady=4)

    status = tk.StringVar(value="正在讀取設定…")
    ttk.Label(frame, textvariable=status).grid(
        row=5, column=0, columnspan=2, sticky="w", pady=(10, 6)
    )

    def refresh_values() -> None:
        try:
            value_32 = read_factor(registry, registry.KEY_WOW64_32KEY)
            value_64 = read_factor(registry, registry.KEY_WOW64_64KEY)
        except OSError as exc:
            status.set(f"讀取失敗：{exc}")
            messagebox.showerror("讀取失敗", str(exc), parent=root)
            return

        current_32.set(_entry_text(registry, value_32))
        current_64.set(_entry_text(registry, value_64))
        factor.set(str(value_32.value) if value_32 is not None else "1.0")
        status.set("已讀取；倍率欄位可編輯，按「儲存」才會寫入。")

    def save_value() -> None:
        try:
            new_factor = normalize_factor(factor.get())
        except ValueError as exc:
            messagebox.showerror("倍率無效", str(exc), parent=root)
            status.set(f"倍率無效：{exc}")
            return

        confirmed = messagebox.askyesno(
            "確認儲存",
            "請先關閉 LibreCAD，避免它關閉面板時將舊倍率寫回。\n\n"
            f"要將倍率 {new_factor} 寫入 32 位元登錄檢視嗎？",
            parent=root,
        )
        if not confirmed:
            status.set("已取消，登錄檔未修改。")
            return

        try:
            saved = set_factor(registry, new_factor)
        except (OSError, ValueError) as exc:
            messagebox.showerror("儲存失敗", str(exc), parent=root)
            status.set(f"儲存失敗：{exc}")
            return

        current_32.set(_entry_text(registry, saved))
        factor.set(str(saved.value))
        status.set(f"已寫入並讀回確認：{_entry_text(registry, saved)}")

    buttons = ttk.Frame(frame)
    buttons.grid(row=6, column=0, columnspan=2, sticky="e", pady=(10, 0))
    ttk.Button(buttons, text="重新讀取", command=refresh_values).grid(
        row=0, column=0, padx=(0, 8)
    )
    ttk.Button(buttons, text="儲存倍率", command=save_value).grid(
        row=0, column=1, padx=(0, 8)
    )
    ttk.Button(buttons, text="關閉", command=root.destroy).grid(row=0, column=2)

    refresh_values()
    factor_entry.focus_set()
    root.mainloop()
    return 0


def main(argv: list[str] | None = None, registry: Any | None = None) -> int:
    arguments = sys.argv[1:] if argv is None else argv
    if not arguments:
        return launch_gui(registry)

    args = build_parser().parse_args(arguments)
    if args.command == "set":
        try:
            factor = normalize_factor(args.factor)
        except ValueError as exc:
            print(f"倍率無效：{exc}", file=sys.stderr)
            return 2
    else:
        factor = None

    try:
        registry = registry if registry is not None else _load_winreg()
        if args.command == "get":
            return _show_both_views(registry)

        print(
            "注意：請先關閉 LibreCAD；插入選項面板關閉時可能會把舊倍率寫回。"
        )
        current = read_factor(registry, registry.KEY_WOW64_32KEY)
        print(f"登錄檔位置：{REGISTRY_DISPLAY_PATH}\\{VALUE_NAME}")
        print(f"目前 32 位元值：{_entry_text(registry, current)}")
        if not args.yes and not _confirm_write(factor):
            print("已取消，登錄檔未修改。")
            return 1

        written = set_factor(registry, factor)
        print(f"寫入並讀回確認成功：{_entry_text(registry, written)}")
        return 0
    except (OSError, RuntimeError) as exc:
        print(f"操作失敗：{exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
