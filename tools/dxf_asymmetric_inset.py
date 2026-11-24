#!/usr/bin/env python3
"""Create an asymmetric inset outline from the largest closed LWPOLYLINE.

Inset values are in the DXF drawing unit. The defaults match the inch-based
pattern workflow: 0.3125 on the top/left/right and 1.0 on the bottom.
The input drawing is preserved and the generated outline is red. Small V
notches in the boundary keep their original width and depth. Separate LINE
marks on notch/needle layers are copied to the inset boundary without changing
their lengths; every original entity remains in the output.

Usage:
    python dxf_asymmetric_inset.py input.dxf
    python dxf_asymmetric_inset.py input.dxf --output result.dxf
"""

from __future__ import annotations

import argparse
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple


Point = Tuple[float, float]
EPSILON = 1.0e-10
DEFAULT_SAMPLE_ANGLE_DEGREES = 1.0
DEFAULT_NOTCH_LIMIT = 0.5
DEFAULT_EXISTING_LINE_TOLERANCE = 0.02


@dataclass(frozen=True)
class Vertex:
    point: Point
    bulge: float = 0.0


@dataclass(frozen=True)
class LwPolyline:
    record_index: int
    layer: str
    color: Optional[int]
    closed: bool
    vertices: Tuple[Vertex, ...]


@dataclass(frozen=True)
class Notch:
    vertex_index: int
    previous: Point
    point: Point
    following: Point
    signed_depth: float


@dataclass(frozen=True)
class OffsetLine:
    start: Point
    direction: Point


def add(a: Point, b: Point) -> Point:
    return a[0] + b[0], a[1] + b[1]


def subtract(a: Point, b: Point) -> Point:
    return a[0] - b[0], a[1] - b[1]


def multiply(point: Point, factor: float) -> Point:
    return point[0] * factor, point[1] * factor


def dot(a: Point, b: Point) -> float:
    return a[0] * b[0] + a[1] * b[1]


def cross(a: Point, b: Point) -> float:
    return a[0] * b[1] - a[1] * b[0]


def magnitude(point: Point) -> float:
    return math.hypot(point[0], point[1])


def normalize(point: Point) -> Point:
    length = magnitude(point)
    if length <= EPSILON:
        return 0.0, 0.0
    return point[0] / length, point[1] / length


def left_normal(point: Point) -> Point:
    return -point[1], point[0]


def signed_area(points: Sequence[Point]) -> float:
    if len(points) < 3:
        return 0.0
    return 0.5 * sum(
        cross(points[index], points[(index + 1) % len(points)])
        for index in range(len(points)))


def sample_segment(start: Point, end: Point, bulge: float,
                   maximum_angle: float) -> List[Point]:
    if abs(bulge) <= EPSILON:
        return [start, end]

    chord = subtract(end, start)
    chord_length = magnitude(chord)
    sweep = 4.0 * math.atan(bulge)
    sine = math.sin(abs(sweep) * 0.5)
    if chord_length <= EPSILON or abs(sine) <= EPSILON:
        return [start, end]

    radius = chord_length / (2.0 * sine)
    midpoint = multiply(add(start, end), 0.5)
    tangent = math.tan(sweep * 0.5)
    if abs(tangent) <= EPSILON:
        return [start, end]
    center = add(
        midpoint,
        multiply(normalize(left_normal(chord)),
                 chord_length / (2.0 * tangent)))
    start_angle = math.atan2(start[1] - center[1], start[0] - center[0])
    divisions = max(1, math.ceil(abs(sweep) / maximum_angle))
    result = []
    for index in range(divisions + 1):
        angle = start_angle + sweep * index / divisions
        result.append((center[0] + radius * math.cos(angle),
                       center[1] + radius * math.sin(angle)))
    result[0] = start
    result[-1] = end
    return result


def sample_polyline(vertices: Sequence[Vertex],
                    maximum_angle: float) -> List[Point]:
    points: List[Point] = []
    for index, vertex in enumerate(vertices):
        following = vertices[(index + 1) % len(vertices)]
        segment = sample_segment(vertex.point, following.point,
                                 vertex.bulge, maximum_angle)
        points.extend(segment[:-1])
    return points


def read_lines(path: Path) -> Tuple[List[str], str, str, bool]:
    raw = path.read_bytes()
    has_bom = raw.startswith(b"\xef\xbb\xbf")
    encoding = "utf-8-sig" if has_bom else "utf-8"
    try:
        text = raw.decode(encoding)
    except UnicodeDecodeError:
        encoding = "cp950"
        text = raw.decode(encoding)
    newline = "\r\n" if b"\r\n" in raw else "\n"
    normalized = text.replace("\r\n", "\n").replace("\r", "\n")
    trailing_newline = normalized.endswith("\n")
    lines = normalized.split("\n")
    if trailing_newline:
        lines.pop()
    return lines, encoding, newline, trailing_newline


def parse_pairs(lines: Sequence[str]) -> List[Tuple[int, str]]:
    if len(lines) % 2:
        raise ValueError("DXF 群組碼資料不完整，檔案行數不是偶數。")
    pairs: List[Tuple[int, str]] = []
    for index in range(0, len(lines), 2):
        code_text = lines[index].strip()
        try:
            code = int(code_text)
        except ValueError as error:
            raise ValueError(
                f"DXF 第 {index + 1} 行不是群組碼：{code_text!r}") from error
        pairs.append((code, lines[index + 1].strip()))
    return pairs


def entities_section(pairs: Sequence[Tuple[int, str]]
                     ) -> Tuple[List[Tuple[int, List[Tuple[int, str]]]], int]:
    section_start = None
    for index in range(len(pairs) - 1):
        if pairs[index] == (0, "SECTION") and pairs[index + 1] == (
                2, "ENTITIES"):
            section_start = index + 2
            break
    if section_start is None:
        raise ValueError("DXF 沒有 ENTITIES 區段。")

    records: List[Tuple[int, List[Tuple[int, str]]]] = []
    cursor = section_start
    while cursor < len(pairs):
        code, value = pairs[cursor]
        if code == 0 and value == "ENDSEC":
            return records, cursor
        if code != 0:
            cursor += 1
            continue
        start = cursor
        cursor += 1
        while cursor < len(pairs) and pairs[cursor][0] != 0:
            cursor += 1
        records.append((start, list(pairs[start:cursor])))
    raise ValueError("DXF 的 ENTITIES 區段沒有 ENDSEC。")


def layer_colors(pairs: Sequence[Tuple[int, str]]) -> Dict[str, int]:
    colors: Dict[str, int] = {}
    for index, (code, value) in enumerate(pairs):
        if code != 0 or value != "LAYER":
            continue
        attributes: List[Tuple[int, str]] = []
        cursor = index + 1
        while cursor < len(pairs) and pairs[cursor][0] != 0:
            attributes.append(pairs[cursor])
            cursor += 1
        name = next((item for item in attributes if item[0] == 2), None)
        color = next((item for item in attributes if item[0] == 62), None)
        if name is not None and color is not None:
            try:
                colors[name[1]] = int(color[1])
            except ValueError:
                continue
    return colors


def parse_lwpolyline(record_index: int,
                     record: Sequence[Tuple[int, str]]) -> Optional[LwPolyline]:
    if not record or record[0] != (0, "LWPOLYLINE"):
        return None

    layer = "0"
    color: Optional[int] = None
    flags = 0
    vertices: List[Vertex] = []
    current: Optional[Dict[str, Optional[float]]] = None

    def finish_vertex() -> None:
        nonlocal current
        if current is None or current.get("x") is None \
                or current.get("y") is None:
            current = None
            return
        vertices.append(Vertex((float(current["x"]), float(current["y"])),
                               float(current.get("bulge") or 0.0)))
        current = None

    for code, value in record[1:]:
        if code == 8:
            layer = value
        elif code == 62:
            try:
                color = int(value)
            except ValueError:
                color = None
        elif code == 70:
            try:
                flags = int(value)
            except ValueError:
                flags = 0
        elif code == 10:
            finish_vertex()
            current = {"x": float(value), "y": None, "bulge": 0.0}
        elif code == 20 and current is not None:
            current["y"] = float(value)
        elif code == 42 and current is not None:
            current["bulge"] = float(value)
    finish_vertex()

    return LwPolyline(record_index, layer, color, bool(flags & 1),
                      tuple(vertices))


def parse_unit_code(pairs: Sequence[Tuple[int, str]]) -> Optional[int]:
    for index, (code, value) in enumerate(pairs[:-1]):
        if code == 9 and value.upper() == "$INSUNITS":
            if pairs[index + 1][0] == 70:
                try:
                    return int(float(pairs[index + 1][1]))
                except ValueError:
                    return None
    return None


def line_intersection(first: OffsetLine, second: OffsetLine) -> Optional[Point]:
    denominator = cross(first.direction, second.direction)
    if abs(denominator) <= EPSILON:
        return None
    amount = cross(subtract(second.start, first.start), second.direction) \
        / denominator
    return add(first.start, multiply(first.direction, amount))


def find_small_notches(vertices: Sequence[Vertex], orientation: float,
                       maximum_size: float) -> List[Notch]:
    notches: List[Notch] = []
    count = len(vertices)
    for index, current in enumerate(vertices):
        previous = vertices[(index - 1) % count]
        following = vertices[(index + 1) % count]
        if abs(previous.bulge) > EPSILON or abs(current.bulge) > EPSILON:
            continue
        incoming = subtract(current.point, previous.point)
        outgoing = subtract(following.point, current.point)
        turn = cross(incoming, outgoing) * orientation
        if turn >= -EPSILON:
            continue
        chord = subtract(following.point, previous.point)
        width = math.hypot(chord[0], chord[1])
        if width <= EPSILON or width > maximum_size:
            continue
        normal = normalize(left_normal(chord))
        midpoint = multiply(add(previous.point, following.point), 0.5)
        signed_depth = dot(subtract(current.point, midpoint), normal)
        if abs(signed_depth) <= maximum_size:
            notches.append(Notch(index, previous.point, current.point,
                                 following.point, signed_depth))
    return notches


def inset_category(start: Point, end: Point, bounds: Tuple[float, float,
                                                           float, float]
                   ) -> str:
    min_x, min_y, max_x, max_y = bounds
    midpoint = multiply(add(start, end), 0.5)
    delta = subtract(end, start)
    # Sampled arcs can overshoot a straight bottom edge by a few microns in
    # drawing units. Use a scale-aware tolerance so that noise does not
    # reclassify the bottom edge as the left or right side.
    drawing_scale = max(max_x - min_x + max_y - min_y, 1.0)
    tolerance = max(1.0e-8, drawing_scale * 1.0e-6)
    if abs(delta[1]) <= tolerance and abs(midpoint[1] - min_y) <= tolerance:
        return "bottom"
    center_y = (min_y + max_y) * 0.5
    if midpoint[1] >= center_y:
        return "top"
    return "left" if midpoint[0] < (min_x + max_x) * 0.5 else "right"


def record_layer(record: Sequence[Tuple[int, str]]) -> str:
    return next((value for code, value in record[1:] if code == 8), "0")


def is_notch_mark_layer(layer: str) -> bool:
    normalized = layer.casefold()
    return "針孔" in layer or "牙口" in layer or "notch" in normalized


def line_coordinates(record: Sequence[Tuple[int, str]]
                     ) -> Optional[Tuple[Point, Point]]:
    values: Dict[int, str] = {}
    for code, value in record[1:]:
        if code in (10, 20, 11, 21):
            values[code] = value
    if not all(code in values for code in (10, 20, 11, 21)):
        return None
    try:
        return ((float(values[10]), float(values[20])),
                (float(values[11]), float(values[21])))
    except ValueError:
        return None


def closest_boundary_segment(point: Point, boundary: Sequence[Point]
                             ) -> Tuple[float, Point, Point]:
    best_distance = math.inf
    best_start = boundary[0]
    best_end = boundary[1 % len(boundary)]
    for index, start in enumerate(boundary):
        end = boundary[(index + 1) % len(boundary)]
        direction = subtract(end, start)
        length_squared = dot(direction, direction)
        if length_squared <= EPSILON:
            continue
        amount = max(0.0, min(1.0, dot(subtract(point, start), direction)
                            / length_squared))
        nearest = add(start, multiply(direction, amount))
        distance = math.hypot(point[0] - nearest[0], point[1] - nearest[1])
        if distance < best_distance:
            best_distance = distance
            best_start, best_end = start, end
    return best_distance, best_start, best_end


def same_line(first: Tuple[Point, Point], second: Tuple[Point, Point],
              tolerance: float = 1.0e-7) -> bool:
    direct = (math.hypot(first[0][0] - second[0][0],
                         first[0][1] - second[0][1]) <= tolerance
              and math.hypot(first[1][0] - second[1][0],
                             first[1][1] - second[1][1]) <= tolerance)
    reverse = (math.hypot(first[0][0] - second[1][0],
                          first[0][1] - second[1][1]) <= tolerance
               and math.hypot(first[1][0] - second[0][0],
                              first[1][1] - second[0][1]) <= tolerance)
    return direct or reverse


def translated_notch_marks(
        records: Sequence[Tuple[int, List[Tuple[int, str]]]],
        boundary: Sequence[Point], orientation: float,
        bounds: Tuple[float, float, float, float],
        offsets: Dict[str, float], tolerance: float
        ) -> Tuple[List[List[Tuple[int, str]]], int]:
    clones: List[List[Tuple[int, str]]] = []
    original_marks: List[Tuple[str, Tuple[Point, Point]]] = []
    candidate_marks: List[Tuple[str, Tuple[Point, Point],
                                 List[Tuple[int, str]]]] = []
    for _record_index, record in records:
        if not record or record[0] != (0, "LINE"):
            continue
        layer = record_layer(record)
        if not is_notch_mark_layer(layer):
            continue
        line = line_coordinates(record)
        if line is None:
            continue
        original_marks.append((layer, line))
        midpoint = multiply(add(line[0], line[1]), 0.5)
        distance, edge_start, edge_end = closest_boundary_segment(
            midpoint, boundary)
        if distance > tolerance:
            continue
        category = inset_category(edge_start, edge_end, bounds)
        normal = multiply(
            normalize(left_normal(subtract(edge_end, edge_start))), orientation)
        shift = multiply(normal, offsets[category])
        shifted = (add(line[0], shift), add(line[1], shift))
        if any(layer == original_layer and same_line(shifted, original)
               for original_layer, original in original_marks):
            continue
        updated: List[Tuple[int, str]] = []
        for code, value in record:
            if code == 5:
                continue
            if code == 10:
                value = format_number(shifted[0][0])
            elif code == 20:
                value = format_number(shifted[0][1])
            elif code == 11:
                value = format_number(shifted[1][0])
            elif code == 21:
                value = format_number(shifted[1][1])
            updated.append((code, value))
        candidate_marks.append((layer, shifted, updated))

    for layer, shifted, clone in candidate_marks:
        if any(layer == other_layer and same_line(shifted, other_line)
               for other_layer, other_line in original_marks):
            continue
        clones.append(clone)
    return clones, len(clones)


def offset_polygon(points: Sequence[Point], edge_distances: Sequence[float],
                   orientation: float) -> List[Point]:
    if len(points) < 3 or len(points) != len(edge_distances):
        raise ValueError("外框不足三個點，無法產生內縮線。")

    lines: List[OffsetLine] = []
    for index, start in enumerate(points):
        end = points[(index + 1) % len(points)]
        direction = subtract(end, start)
        if math.hypot(direction[0], direction[1]) <= EPSILON:
            raise ValueError("外框含有重複相鄰點，無法安全內縮。")
        inward = multiply(normalize(left_normal(direction)), orientation)
        shifted = add(start, multiply(inward, edge_distances[index]))
        lines.append(OffsetLine(shifted, direction))

    result: List[Point] = []
    for index, point in enumerate(points):
        previous = lines[(index - 1) % len(lines)]
        current = lines[index]
        intersection = line_intersection(previous, current)
        if intersection is None:
            previous_end = add(previous.start, previous.direction)
            intersection = multiply(add(previous_end, current.start), 0.5)
        if not all(math.isfinite(value) for value in intersection):
            raise ValueError("內縮線產生非有限座標；請檢查外框是否自交。")
        result.append(intersection)
    return result


def inset_outline(vertices: Sequence[Vertex], top: float, left: float,
                  right: float, bottom: float, maximum_angle: float,
                  notch_limit: float) -> Tuple[List[Point], int, float]:
    original_points = sample_polyline(vertices, maximum_angle)
    area = signed_area(original_points)
    if abs(area) <= EPSILON:
        raise ValueError("最外框面積為零，無法判斷內縮方向。")
    orientation = 1.0 if area > 0 else -1.0
    bounds = (min(point[0] for point in original_points),
              min(point[1] for point in original_points),
              max(point[0] for point in original_points),
              max(point[1] for point in original_points))

    notches = find_small_notches(vertices, orientation, notch_limit)
    removed = {notch.vertex_index for notch in notches}
    base_vertices: List[Vertex] = []
    original_to_base: Dict[int, int] = {}
    for index, vertex in enumerate(vertices):
        if index in removed:
            continue
        original_to_base[index] = len(base_vertices)
        base_vertices.append(vertex)
    if len(base_vertices) < 3:
        raise ValueError("保留牙口後外框點數不足。")

    sampled_points: List[Point] = []
    edge_distances: List[float] = []
    base_to_sample: Dict[int, int] = {}
    for index, vertex in enumerate(base_vertices):
        following = base_vertices[(index + 1) % len(base_vertices)]
        segment = sample_segment(vertex.point, following.point,
                                 vertex.bulge, maximum_angle)
        base_to_sample[index] = len(sampled_points)
        sampled_points.extend(segment[:-1])
        for start, end in zip(segment, segment[1:]):
            category = inset_category(start, end, bounds)
            distance = {"top": top, "left": left,
                        "right": right, "bottom": bottom}[category]
            edge_distances.append(distance)

    inset = offset_polygon(sampled_points, edge_distances, orientation)

    notch_insertions: Dict[int, Point] = {}
    for notch in notches:
        previous_index = (notch.vertex_index - 1) % len(vertices)
        following_index = (notch.vertex_index + 1) % len(vertices)
        start_sample = base_to_sample[original_to_base[previous_index]]
        end_sample = base_to_sample[original_to_base[following_index]]
        if (start_sample + 1) % len(inset) != end_sample:
            raise ValueError("無法在內縮線中定位原有牙口。")
        start = inset[start_sample]
        end = inset[end_sample]
        normal = left_normal(normalize(subtract(end, start)))
        midpoint = multiply(add(start, end), 0.5)
        notch_point = add(midpoint, multiply(normal, notch.signed_depth))
        notch_insertions[end_sample] = notch_point

    if notch_insertions:
        with_notches: List[Point] = []
        for index, point in enumerate(inset):
            with_notches.append(point)
            following_index = (index + 1) % len(inset)
            if following_index in notch_insertions:
                with_notches.append(notch_insertions[following_index])
        inset = with_notches

    inset_area = abs(signed_area(inset))
    if inset_area <= EPSILON or inset_area >= abs(area):
        raise ValueError("內縮輪廓無效；請確認內縮距離小於外框尺寸。")
    return inset, len(notches), abs(area)


def point_segment_distance(point: Point, start: Point, end: Point) -> float:
    direction = subtract(end, start)
    length_squared = dot(direction, direction)
    if length_squared <= EPSILON:
        return math.hypot(point[0] - start[0], point[1] - start[1])
    amount = max(0.0, min(1.0, dot(subtract(point, start), direction)
                        / length_squared))
    closest = add(start, multiply(direction, amount))
    return math.hypot(point[0] - closest[0], point[1] - closest[1])


def directed_path_distance(first: Sequence[Point],
                           second: Sequence[Point]) -> float:
    return max(min(point_segment_distance(
        point, second[index], second[(index + 1) % len(second)])
                   for index in range(len(second))) for point in first)


def is_red(polyline: LwPolyline, colors: Dict[str, int]) -> bool:
    color = polyline.color
    if color is None or color == 256:
        color = colors.get(polyline.layer, 7)
    return abs(color) == 1


def find_matching_red_outline(generated: Sequence[Point],
                              candidates: Sequence[LwPolyline],
                              layer_color_map: Dict[str, int],
                              outer_record_index: int,
                              maximum_angle: float,
                              tolerance: float
                              ) -> Optional[LwPolyline]:
    for candidate in candidates:
        if candidate.record_index == outer_record_index \
                or not candidate.closed or not is_red(candidate, layer_color_map):
            continue
        candidate_points = sample_polyline(candidate.vertices, maximum_angle)
        if len(candidate_points) < 3:
            continue
        deviation = max(directed_path_distance(generated, candidate_points),
                       directed_path_distance(candidate_points, generated))
        if deviation <= tolerance:
            return candidate
    return None


def format_number(value: float) -> str:
    if abs(value) < 5.0e-12:
        value = 0.0
    return f"{value:.10f}".rstrip("0").rstrip(".") or "0"


def inset_entity(points: Sequence[Point]) -> List[Tuple[int, str]]:
    result = [(0, "LWPOLYLINE"), (100, "AcDbEntity"), (8, "0"),
              (62, "1"), (100, "AcDbPolyline"),
              (90, str(len(points))), (70, "1")]
    for x, y in points:
        result.extend([(10, format_number(x)), (20, format_number(y))])
    return result


def serialize(lines: Sequence[str], encoding: str, newline: str,
              trailing_newline: bool) -> bytes:
    text = newline.join(lines)
    if trailing_newline:
        text += newline
    return text.encode(encoding)


def process_file(input_path: Path, output_path: Path,
                 top: float, left: float, right: float, bottom: float,
                 sample_angle_degrees: float, notch_limit: float,
                 match_tolerance: float, mark_tolerance: float) -> str:
    if not input_path.is_file():
        raise ValueError(f"來源 DXF 不存在：{input_path}")
    if input_path.resolve() == output_path.resolve():
        raise ValueError("輸出路徑不可覆寫來源 DXF。")
    if output_path.exists():
        raise ValueError(f"輸出檔已存在，為避免覆蓋已停止：{output_path}")
    for label, value in (("上", top), ("左", left), ("右", right),
                         ("下", bottom), ("牙口判斷尺寸", notch_limit),
                         ("比對容許差", match_tolerance),
                         ("牙口標記比對距離", mark_tolerance)):
        if not math.isfinite(value) or value < 0:
            raise ValueError(f"{label}距離必須是大於或等於 0 的有限數值。")
    if not 0.1 <= sample_angle_degrees <= 10.0:
        raise ValueError("圓弧取樣角度必須介於 0.1 到 10 度。")

    lines, encoding, newline, trailing_newline = read_lines(input_path)
    pairs = parse_pairs(lines)
    records, endsec_pair_index = entities_section(pairs)
    colors = layer_colors(pairs)
    polylines = []
    for record_index, record in records:
        polyline = parse_lwpolyline(record_index, record)
        if polyline is not None and polyline.closed \
                and len(polyline.vertices) >= 3:
            polylines.append(polyline)
    if not polylines:
        raise ValueError("找不到封閉的 LWPOLYLINE 外框。")

    maximum_angle = math.radians(sample_angle_degrees)
    areas = [(abs(signed_area(sample_polyline(
        polyline.vertices, maximum_angle))), polyline)
             for polyline in polylines]
    outer_area, outer = max(areas, key=lambda item: item[0])
    generated, notch_count, measured_area = inset_outline(
        outer.vertices, top, left, right, bottom, maximum_angle, notch_limit)
    source_points = sample_polyline(outer.vertices, maximum_angle)
    source_area = signed_area(source_points)
    bounds = (min(point[0] for point in source_points),
              min(point[1] for point in source_points),
              max(point[0] for point in source_points),
              max(point[1] for point in source_points))
    mark_clones, mark_count = translated_notch_marks(
        records, source_points, 1.0 if source_area > 0 else -1.0,
        bounds, {"top": top, "left": left, "right": right,
                "bottom": bottom}, mark_tolerance)

    matching = find_matching_red_outline(
        generated, polylines, colors, outer.record_index,
        maximum_angle, match_tolerance)
    insert_records: List[List[Tuple[int, str]]] = []
    if matching is not None:
        action = (f"現有紅色內縮線符合結果（差異在 {match_tolerance:g} "
                  "個 DXF 單位內），不重複新增。")
    else:
        insert_records.append(inset_entity(generated))
        action = f"已新增紅色內縮線，共 {len(generated)} 個折線點。"
    insert_records.extend(mark_clones)
    if mark_count:
        action += f" 已複製 {mark_count} 個牙口／針孔標記，長度不變。"

    if not insert_records:
        output_path.write_bytes(input_path.read_bytes())
    else:
        entity_lines: List[str] = []
        for record in insert_records:
            for code, value in record:
                entity_lines.extend([str(code), value])
        insertion_line = endsec_pair_index * 2
        output_lines = list(lines)
        output_lines[insertion_line:insertion_line] = entity_lines
        output_path.write_bytes(serialize(output_lines, encoding, newline,
                                          trailing_newline))

    unit_code = parse_unit_code(pairs)
    unit_label = {0: "未指定", 1: "英吋", 4: "毫米"}.get(
        unit_code, f"代碼 {unit_code}")
    return (f"來源：{input_path}\n輸出：{output_path}\n"
            f"DXF 單位：{unit_label}（內縮值以圖面單位計）\n"
            f"最外框：圖層 {outer.layer}，面積 {outer_area:.6f}；"
            f"內縮面積 {abs(signed_area(generated)):.6f}（外框估算面積 "
            f"{measured_area:.6f}）\n"
            f"內縮：上 {top:g}、左 {left:g}、右 {right:g}、下 {bottom:g}；"
            f"保留小牙口 {notch_count} 個。\n{action}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="依最外層封閉折線產生非對稱紅色內縮輪廓。距離使用 DXF 圖面單位。")
    parser.add_argument("input", type=Path, help="來源 DXF 檔案")
    parser.add_argument("-o", "--output", type=Path,
                        help="輸出 DXF；預設為來源檔名加上 _內縮")
    parser.add_argument("--top", type=float, default=0.3125,
                        help="上側內縮距離，預設 0.3125")
    parser.add_argument("--left", type=float, default=0.3125,
                        help="左側內縮距離，預設 0.3125")
    parser.add_argument("--right", type=float, default=0.3125,
                        help="右側內縮距離，預設 0.3125")
    parser.add_argument("--bottom", type=float, default=1.0,
                        help="下側內縮距離，預設 1.0")
    parser.add_argument("--sample-angle", type=float,
                        default=DEFAULT_SAMPLE_ANGLE_DEGREES,
                        help="圓弧取樣角度，預設 1 度")
    parser.add_argument("--notch-limit", type=float,
                        default=DEFAULT_NOTCH_LIMIT,
                        help="保留小 V 牙口的最大寬度／深度，預設 0.5")
    parser.add_argument("--match-tolerance", type=float,
                        default=DEFAULT_EXISTING_LINE_TOLERANCE,
                        help="避免重複新增紅線的比對容許差，預設 0.02")
    parser.add_argument("--mark-tolerance", type=float, default=0.15,
                        help="辨識貼近外框的牙口標記距離，預設 0.15")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    arguments = list(sys.argv[1:] if argv is None else argv)
    if not arguments:
        try:
            from dxf_asymmetric_inset_gui import launch_gui
            launch_gui()
        except Exception as error:
            print(f"無法開啟 GUI：{error}", file=sys.stderr)
            return 1
        return 0

    args = parser.parse_args(arguments)
    input_path = args.input.expanduser()
    output_path = args.output.expanduser() if args.output else input_path.with_name(
        f"{input_path.stem}_內縮{input_path.suffix or '.dxf'}")
    try:
        print(process_file(
            input_path, output_path, args.top, args.left, args.right,
            args.bottom, args.sample_angle, args.notch_limit,
            args.match_tolerance, args.mark_tolerance))
    except (OSError, ValueError) as error:
        print(f"錯誤：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
