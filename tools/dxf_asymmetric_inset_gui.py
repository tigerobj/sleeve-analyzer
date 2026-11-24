#!/usr/bin/env python3
"""Tk GUI for previewing and saving an asymmetric DXF inset."""

from __future__ import annotations

import math
import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from typing import Dict, List, Optional, Sequence, Tuple

from dxf_asymmetric_inset import (
    DEFAULT_EXISTING_LINE_TOLERANCE,
    DEFAULT_NOTCH_LIMIT,
    Point,
    entities_section,
    inset_outline,
    line_coordinates,
    parse_pairs,
    parse_lwpolyline,
    parse_unit_code,
    read_lines,
    record_layer,
    sample_polyline,
    signed_area,
    translated_notch_marks,
    is_notch_mark_layer,
    process_file,
)


PREVIEW_SAMPLE_ANGLE_DEGREES = 1.0
PREVIEW_MARK_TOLERANCE = 0.15
DEFAULT_OFFSETS = {"上": "0.3125", "下": "1.0",
                   "左": "0.3125", "右": "0.3125"}


def read_preview(path: Path, offsets: Dict[str, float]
                 ) -> Tuple[List[Point], List[Point], List[Tuple[Point, Point]],
                            List[Tuple[Point, Point]], str, str]:
    """Return source/inset contours and original/copied notch marks."""
    lines, _encoding, _newline, _trailing_newline = read_lines(path)
    pairs = parse_pairs(lines)
    records, _endsec = entities_section(pairs)
    polylines = []
    for record_index, record in records:
        polyline = parse_lwpolyline(record_index, record)
        if polyline is not None and polyline.closed and len(polyline.vertices) >= 3:
            polylines.append(polyline)
    if not polylines:
        raise ValueError("找不到封閉的 LWPOLYLINE 外框。")

    max_angle = math.radians(PREVIEW_SAMPLE_ANGLE_DEGREES)
    source, outer = max(
        ((sample_polyline(polyline.vertices, max_angle), polyline)
         for polyline in polylines),
        key=lambda item: abs(signed_area(item[0])))
    inset, notch_count, _area = inset_outline(
        outer.vertices, offsets["上"], offsets["左"], offsets["右"],
        offsets["下"], max_angle, DEFAULT_NOTCH_LIMIT)
    orientation = 1.0 if signed_area(source) > 0 else -1.0
    bounds = (min(point[0] for point in source),
              min(point[1] for point in source),
              max(point[0] for point in source),
              max(point[1] for point in source))
    mark_clones, mark_count = translated_notch_marks(
        records, source, orientation, bounds,
        {"top": offsets["上"], "left": offsets["左"],
         "right": offsets["右"], "bottom": offsets["下"]},
        PREVIEW_MARK_TOLERANCE)

    original_marks: List[Tuple[Point, Point]] = []
    for _record_index, record in records:
        if record and record[0] == (0, "LINE") \
                and is_notch_mark_layer(record_layer(record)):
            coordinates = line_coordinates(record)
            if coordinates is not None:
                original_marks.append(coordinates)
    copied_marks = []
    for record in mark_clones:
        coordinates = line_coordinates(record)
        if coordinates is not None:
            copied_marks.append(coordinates)

    unit_code = parse_unit_code(pairs)
    unit_label = {0: "未指定", 1: "英吋", 2: "英呎", 4: "毫米",
                  5: "公分", 6: "公尺"}.get(
                      unit_code, f"圖面單位代碼 {unit_code}")
    summary = f"保留牙口 {notch_count} 個；複製牙口／針孔標記 {mark_count} 個（尺寸不變）"
    return source, inset, original_marks, copied_marks, unit_label, summary


class InsetPreviewApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("DXF 外框內縮與預覽")
        self.root.geometry("1120x790")
        self.root.minsize(860, 600)
        self.source_var = tk.StringVar()
        self.output_var = tk.StringVar()
        self.status_var = tk.StringVar(value="選擇 DXF 或將檔案拖到預覽區。")
        self.unit_var = tk.StringVar(value="DXF 圖面單位：—")
        self.offset_vars = {label: tk.StringVar(value=value)
                            for label, value in DEFAULT_OFFSETS.items()}
        self._preview_job: Optional[str] = None
        self._source_points: Optional[List[Point]] = None
        self._inset_points: Optional[List[Point]] = None
        self._original_marks: List[Tuple[Point, Point]] = []
        self._copied_marks: List[Tuple[Point, Point]] = []
        self._preview_offsets: Optional[Dict[str, float]] = None
        self._drop_hook = None
        self._make_widgets()
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def _make_widgets(self) -> None:
        file_frame = ttk.LabelFrame(self.root, text="DXF 檔案")
        file_frame.pack(fill="x", padx=12, pady=(12, 6))
        file_frame.columnconfigure(1, weight=1)
        ttk.Label(file_frame, text="來源 DXF：").grid(
            row=0, column=0, padx=(8, 4), pady=7, sticky="w")
        ttk.Entry(file_frame, textvariable=self.source_var).grid(
            row=0, column=1, padx=4, pady=7, sticky="ew")
        ttk.Button(file_frame, text="瀏覽…", command=self.browse_source).grid(
            row=0, column=2, padx=(4, 8), pady=7)

        settings = ttk.LabelFrame(self.root, text="內縮距離（依 DXF 圖面單位）")
        settings.pack(fill="x", padx=12, pady=6)
        for column, label in enumerate(("上", "下", "左", "右")):
            settings.columnconfigure(column, weight=1)
            ttk.Label(settings, text=f"{label}：").grid(
                row=0, column=column, padx=(10, 2), pady=(8, 2), sticky="w")
            spinbox = ttk.Spinbox(
                settings, textvariable=self.offset_vars[label], from_=0,
                to=100000, increment=0.0625, width=14)
            spinbox.grid(row=1, column=column, padx=(10, 10),
                         pady=(2, 8), sticky="ew")
            spinbox.bind("<KeyRelease>", self.schedule_preview)
            spinbox.bind("<FocusOut>", self.schedule_preview)
            spinbox.bind("<<Increment>>", self.schedule_preview)
            spinbox.bind("<<Decrement>>", self.schedule_preview)

        toolbar = ttk.Frame(self.root)
        toolbar.pack(fill="x", padx=12, pady=(2, 6))
        ttk.Label(toolbar, textvariable=self.unit_var).pack(side="left")
        ttk.Button(toolbar, text="更新預覽", command=self.update_preview).pack(
            side="right")

        preview_frame = ttk.LabelFrame(self.root, text="預覽（灰線：原外框；紅線：內縮結果）")
        preview_frame.pack(fill="both", expand=True, padx=12, pady=6)
        self.canvas = tk.Canvas(preview_frame, bg="white", highlightthickness=1,
                                highlightbackground="#b9c2cc")
        self.canvas.pack(fill="both", expand=True, padx=6, pady=6)
        self.canvas.bind("<Configure>", self.redraw)
        self.canvas.bind("<Button-1>", self._focus_canvas)
        self._enable_file_drop(self.canvas)

        output_frame = ttk.LabelFrame(self.root, text="輸出")
        output_frame.pack(fill="x", padx=12, pady=6)
        output_frame.columnconfigure(1, weight=1)
        ttk.Label(output_frame, text="輸出 DXF：").grid(
            row=0, column=0, padx=(8, 4), pady=7, sticky="w")
        ttk.Entry(output_frame, textvariable=self.output_var).grid(
            row=0, column=1, padx=4, pady=7, sticky="ew")
        ttk.Button(output_frame, text="另存為…", command=self.browse_output).grid(
            row=0, column=2, padx=(4, 8), pady=7)

        bottom = ttk.Frame(self.root)
        bottom.pack(fill="x", padx=12, pady=(2, 12))
        ttk.Label(bottom, textvariable=self.status_var).pack(
            side="left", fill="x", expand=True, padx=(2, 8))
        ttk.Button(bottom, text="產生 DXF", command=self.save_output).pack(
            side="right")

    def _enable_file_drop(self, widget: tk.Widget) -> None:
        """Use native Windows WM_DROPFILES on the large preview canvas."""
        if sys.platform != "win32":
            return
        try:
            import ctypes
            from ctypes import wintypes

            hwnd = widget.winfo_id()
            user32 = ctypes.windll.user32
            shell32 = ctypes.windll.shell32
            pointer_type = ctypes.c_ssize_t
            wndproc_type = ctypes.WINFUNCTYPE(
                pointer_type, wintypes.HWND, wintypes.UINT,
                wintypes.WPARAM, wintypes.LPARAM)
            set_proc = user32.SetWindowLongPtrW
            set_proc.argtypes = [wintypes.HWND, ctypes.c_int, pointer_type]
            set_proc.restype = pointer_type
            get_proc = user32.GetWindowLongPtrW
            get_proc.argtypes = [wintypes.HWND, ctypes.c_int]
            get_proc.restype = pointer_type
            call_proc = user32.CallWindowProcW
            call_proc.argtypes = [pointer_type, wintypes.HWND, wintypes.UINT,
                                  wintypes.WPARAM, wintypes.LPARAM]
            call_proc.restype = pointer_type
            drag_query = shell32.DragQueryFileW
            drag_query.argtypes = [wintypes.HANDLE, wintypes.UINT,
                                   wintypes.LPWSTR, wintypes.UINT]
            drag_query.restype = wintypes.UINT
            drag_finish = shell32.DragFinish
            drag_finish.argtypes = [wintypes.HANDLE]
            shell32.DragAcceptFiles.argtypes = [wintypes.HWND, wintypes.BOOL]

            previous_proc = get_proc(hwnd, -4)
            app = self

            @wndproc_type
            def wndproc(window, message, wparam, lparam):
                if message == 0x0233:  # WM_DROPFILES
                    drop_handle = wintypes.HANDLE(wparam)
                    count = drag_query(drop_handle, 0xFFFFFFFF, None, 0)
                    dropped = []
                    for index in range(count):
                        length = drag_query(drop_handle, index, None, 0)
                        buffer = ctypes.create_unicode_buffer(length + 1)
                        drag_query(drop_handle, index, buffer, length + 1)
                        dropped.append(Path(buffer.value))
                    drag_finish(drop_handle)
                    app.root.after(0, lambda items=dropped: app.accept_drop(items))
                    return 0
                return call_proc(previous_proc, window, message, wparam, lparam)

            set_proc(hwnd, -4, ctypes.cast(wndproc, ctypes.c_void_p).value)
            shell32.DragAcceptFiles(hwnd, True)
            self._drop_hook = (hwnd, previous_proc, wndproc, user32, shell32)
        except Exception as error:
            self.status_var.set(f"Windows 拖放功能無法啟用：{error}")

    def _focus_canvas(self, _event=None) -> None:
        self.canvas.focus_set()

    def accept_drop(self, paths: Sequence[Path]) -> None:
        dxf_paths = [path for path in paths if path.suffix.casefold() == ".dxf"]
        if not dxf_paths:
            messagebox.showwarning("檔案格式", "請拖入 .dxf 檔案。", parent=self.root)
            return
        self.set_source(dxf_paths[0])
        if len(dxf_paths) > 1:
            self.status_var.set(f"已載入 {dxf_paths[0].name}；拖入多個檔案時先使用第一個。")

    def browse_source(self) -> None:
        selected = filedialog.askopenfilename(
            title="選擇來源 DXF", filetypes=[("DXF 檔案", "*.dxf"),
                                              ("所有檔案", "*.*")])
        if selected:
            self.set_source(Path(selected))

    def set_source(self, path: Path) -> None:
        self.source_var.set(str(path))
        self.output_var.set(str(path.with_name(
            f"{path.stem}_內縮{path.suffix or '.dxf'}")))
        self.update_preview()

    def browse_output(self) -> None:
        source_text = self.source_var.get().strip()
        initial = Path(source_text).with_name(
            f"{Path(source_text).stem}_內縮.dxf") if source_text else Path("內縮.dxf")
        selected = filedialog.asksaveasfilename(
            title="設定輸出 DXF", initialdir=str(initial.parent),
            initialfile=initial.name, defaultextension=".dxf",
            filetypes=[("DXF 檔案", "*.dxf")])
        if selected:
            self.output_var.set(selected)

    def schedule_preview(self, _event=None) -> None:
        if self._preview_job is not None:
            self.root.after_cancel(self._preview_job)
        self._preview_job = self.root.after(350, self.update_preview)

    def get_offsets(self) -> Dict[str, float]:
        offsets = {}
        for label, variable in self.offset_vars.items():
            try:
                value = float(variable.get().strip())
            except ValueError as error:
                raise ValueError(f"{label}側距離必須是數字。") from error
            if not math.isfinite(value) or value < 0:
                raise ValueError(f"{label}側距離必須是大於或等於 0 的有限數值。")
            offsets[label] = value
        return offsets

    def update_preview(self) -> None:
        self._preview_job = None
        source_text = self.source_var.get().strip()
        if not source_text:
            self.status_var.set("請選擇 DXF，或將 .dxf 拖到預覽區。")
            return
        try:
            path = Path(source_text).expanduser()
            offsets = self.get_offsets()
            result = read_preview(path, offsets)
        except (OSError, ValueError) as error:
            self.status_var.set(f"預覽無法更新：{error}")
            return
        (self._source_points, self._inset_points, self._original_marks,
         self._copied_marks, unit_label, summary) = result
        self._preview_offsets = offsets
        self.unit_var.set(f"DXF 圖面單位：{unit_label}")
        self.status_var.set(summary)
        self.redraw()

    def redraw(self, _event=None) -> None:
        if not self._source_points or not self._inset_points:
            return
        width = max(1, self.canvas.winfo_width())
        height = max(1, self.canvas.winfo_height())
        all_points = list(self._source_points) + list(self._inset_points)
        for start, end in self._original_marks + self._copied_marks:
            all_points.extend((start, end))
        min_x = min(point[0] for point in all_points)
        max_x = max(point[0] for point in all_points)
        min_y = min(point[1] for point in all_points)
        max_y = max(point[1] for point in all_points)
        span_x = max(max_x - min_x, 1.0e-9)
        span_y = max(max_y - min_y, 1.0e-9)
        margin = 28
        scale = min((width - 2 * margin) / span_x,
                    (height - 2 * margin) / span_y)

        def screen(point: Point) -> Tuple[float, float]:
            return (margin + (point[0] - min_x) * scale,
                    height - margin - (point[1] - min_y) * scale)

        def draw_contour(points: Sequence[Point], color: str, line_width: int):
            coordinates = []
            for point in list(points) + [points[0]]:
                coordinates.extend(screen(point))
            self.canvas.create_line(*coordinates, fill=color, width=line_width,
                                    joinstyle=tk.ROUND, capstyle=tk.ROUND)

        self.canvas.delete("all")
        draw_contour(self._source_points, "#56616d", 1)
        draw_contour(self._inset_points, "#d52222", 2)
        for start, end in self._original_marks:
            self.canvas.create_line(*screen(start), *screen(end),
                                    fill="#7d8792", width=2)
        for start, end in self._copied_marks:
            self.canvas.create_line(*screen(start), *screen(end),
                                    fill="#d52222", width=2)

    def save_output(self) -> None:
        try:
            source = Path(self.source_var.get().strip()).expanduser()
            output_text = self.output_var.get().strip()
            if not output_text:
                raise ValueError("請指定輸出 DXF 路徑。")
            output = Path(output_text).expanduser()
            offsets = self.get_offsets()
            if (self._source_points is None or self._inset_points is None
                    or self._preview_offsets != offsets):
                self.update_preview()
                if self._source_points is None or self._inset_points is None:
                    return
            result = process_file(
                source, output, offsets["上"], offsets["左"], offsets["右"],
                offsets["下"], PREVIEW_SAMPLE_ANGLE_DEGREES,
                DEFAULT_NOTCH_LIMIT, DEFAULT_EXISTING_LINE_TOLERANCE,
                PREVIEW_MARK_TOLERANCE)
        except (OSError, ValueError) as error:
            messagebox.showerror("無法產生 DXF", str(error), parent=self.root)
            self.status_var.set(f"產生失敗：{error}")
            return
        self.status_var.set(f"已輸出：{output}")
        messagebox.showinfo("完成", result, parent=self.root)

    def close(self) -> None:
        if self._drop_hook is not None:
            hwnd, previous_proc, _wndproc, user32, shell32 = self._drop_hook
            try:
                user32.SetWindowLongPtrW(hwnd, -4, previous_proc)
                shell32.DragAcceptFiles(hwnd, False)
            except Exception:
                pass
        self.root.destroy()


def launch_gui(initial_source: Optional[Path] = None) -> None:
    root = tk.Tk()
    app = InsetPreviewApp(root)
    if initial_source is not None:
        root.after(100, lambda: app.set_source(initial_source))
    root.mainloop()


if __name__ == "__main__":
    launch_gui(Path(sys.argv[1]) if len(sys.argv) > 1 else None)
