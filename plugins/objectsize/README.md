# Object Size plugin

`Plugins > 物件長寬` 會要求使用者選取物件，計算所選物件集合的 X/Y 軸對齊外框，並同時顯示毫米與英吋：

```text
長度（X）：25.4 mm（1 英吋）
寬度（Y）：50.8 mm（2 英吋）
```

這裡的長寬是整體外框範圍，不是周長、面積或旋轉後的最小外接矩形。按下「複製長寬」後，兩行雙單位資訊會寫入系統剪貼簿。

## 程式檔案

- `objectsize.h`：宣告 `LC_ObjectSizePlugin` 與 `LC_ObjectSizeDialog`。
- `objectsize.cpp`：插件入口、物件外框計算、單位換算、報告與剪貼簿處理。
- `objectsize.json`：Qt 插件 metadata。
- `objectsize.pro`：qmake 專案設定。
- `build-plugin.ps1`：Windows 編譯及部署腳本。

## 執行流程與使用的函式

1. `LC_ObjectSizePlugin::getCapabilities()` 回傳 `PluginCapabilities`，把 `LC_ObjectSizePlugin::name()` 提供的「物件長寬」註冊到 `plugins_menu`。
2. 使用者執行選單後，LibreCAD 呼叫 `LC_ObjectSizePlugin::execComm()`。
3. `execComm()` 呼叫 `Document_Interface::getSelect()`，取得 `QList<Plug_Entity*>` 選取清單。
4. `addEntityBounds()` 對每個物件呼叫 `Plug_Entity::getData()`，從 `QHash<int, QVariant>` 讀取 `DPI::ETYPE` 與座標資料。
5. 折線另外呼叫 `Plug_Entity::getPolylineData()` 取得 `QList<Plug_VertexData>`，再處理各段的 bulge 圓弧。
6. 每個物件的外框合併到總 `Bounds`，最後用 `maxX - minX` 與 `maxY - minY` 得到長寬。
7. `drawingUnitInfo()` 呼叫 `getVariableInt("$INSUNITS", &units)` 判斷 DXF 單位；`dualUnitString()` 將數值轉成 mm 與英吋。
8. `LC_ObjectSizeDialog::setReport()` 顯示報告；`copyDimensions()` 呼叫 `QApplication::clipboard()->setText()` 複製長寬。

## 長寬計算演算法

### 1. `Bounds` 累積最小值與最大值

`Bounds` 保存四個邊界：`minX`、`minY`、`maxX`、`maxY`。`Bounds::add(const QPointF&)` 的做法是：

```text
第一次加入有效點：
    minX = maxX = point.x
    minY = maxY = point.y

後續加入點：
    minX = std::min(minX, point.x)
    minY = std::min(minY, point.y)
    maxX = std::max(maxX, point.x)
    maxY = std::max(maxY, point.y)
```

加入前會以 `std::isfinite()` 排除 `NaN` 與無限大座標。當所有物件處理完成後，公式就是：

```text
長度（X） = maxX - minX
寬度（Y） = maxY - minY
```

這是軸對齊外框（AABB）演算法。多選時，每個物件先得到自己的外框，再把該外框的左下角與右上角加入總 `Bounds`；因為外框已經是軸對齊矩形，所以這兩個角落就足以合併四個方向的邊界。

### 2. 各種物件如何產生候選點

| 物件 | 主要函式 | 計算方式 |
|---|---|---|
| 點 | `addEntityBounds()` | 加入 `STARTX/STARTY`。 |
| 直線 | `addEntityBounds()` | 加入起點與終點；線段的 X/Y 極值一定在端點。 |
| 圓 | `addEntityBounds()` | 以中心 `(cx, cy)` 與半徑 `r` 直接加入 `(cx-r, cy-r)`、`(cx+r, cy+r)`。 |
| 圓弧 | `addCircularArcBounds()` | 加入兩端點，再檢查 `0`、`π/2`、`π`、`3π/2` 四個圓周極值角。 |
| 橢圓 | `addEllipseBounds()` | 加入端點與 X/Y 導數為零的極值參數；完整橢圓直接用解析半徑。 |
| 折線 | `getPolylineData()`、`addBulgeBounds()` | 加入頂點；非零 bulge 依 DXF 公式還原圓弧。 |
| 圖片 | `addEntityBounds()` | 由插入點、U/V 向量與 `SIZEU/SIZEV` 算出四個角落。 |

### 3. 圓弧的精確極值

`addCircularArcBounds()` 不用固定間隔取樣。它使用：

- `positiveAngle()`：用 `std::fmod()` 將角度正規化到 `0` 至 `2π`。
- `directedSweep()`：依 `REVERSED` 計算順時針或逆時針的掃掠角。
- `angleOnArc()`：判斷候選極值角是否落在弧段內。
- `addArcPoint()`：使用 `x = cx + r cos(θ)`、`y = cy + r sin(θ)` 將角度轉成座標。

因此圓弧即使中間經過 X 或 Y 的最高／最低點，也不會因取樣間隔太大而漏算。

### 4. 折線 bulge 的還原

DXF 折線的 `bulge` 定義為：

```text
bulge = tan(包含角 / 4)
包含角 alpha = 4 × atan(bulge)
```

`addBulgeBounds()` 先以 `std::hypot()` 求弦長，再用半弦長與 `sin(alpha / 2)` 求半徑，接著利用弦中點、弦方向與法線方向反推出圓心。最後把這段 bulge 當成普通圓弧，交給 `addCircularArcBounds()` 處理。

`CLOSEPOLY` 不為零時，程式也會計算最後一個頂點到第一個頂點的閉合段；開放折線則只計算相鄰頂點。

### 5. 橢圓的解析幾何

`ellipsePoint()` 使用主軸向量與比例值建立旋轉橢圓參數式：

```text
x = cx + majorX cos(t) - ratio × majorY sin(t)
y = cy + majorY cos(t) + ratio × majorX sin(t)
```

完整橢圓的 X/Y 半徑為：

```text
xRadius = hypot(majorX, ratio × majorY)
yRadius = hypot(majorY, ratio × majorX)
```

部分橢圓則檢查下列導數為零的參數，以及它們相差 `π` 的對向點：

```text
xExtreme = atan2(-ratio × majorY, majorX)
yExtreme = atan2( ratio × majorX, majorY)
```

所以橢圓外框是由解析極值決定，不是由畫面像素或固定次數取樣決定。

## 單位自動判斷與換算

`drawingUnitInfo()` 讀取 DXF `$INSUNITS`，將原始圖面單位對應到毫米倍率 `toMillimeters`。`dualUnitString()` 的公式是：

```text
毫米 = 原始數值 × toMillimeters
英吋 = 毫米 ÷ 25.4
```

常用 `$INSUNITS`：

| 代碼 | 單位 | 毫米倍率 |
|---:|---|---:|
| 1 | 英吋 | 25.4 |
| 2 | 英尺 | 304.8 |
| 4 | 毫米 | 1.0 |
| 5 | 公分 | 10.0 |
| 6 | 公尺 | 1000.0 |
| 7 | 公里 | 1000000.0 |
| 10 | 碼 | 914.4 |
| 21 | 美國測量英尺 | 304.800609601 |
| 22 | 美國測量英吋 | 25.4000508 |

若沒有 `$INSUNITS` 或代碼未知，程式使用 `1.0`，也就是按毫米處理，並在報告中標示未指定或未知單位。以英吋圖面數值 `1` 為例：`1 × 25.4 = 25.4 mm`，再以 `25.4 ÷ 25.4 = 1 英吋`，因此輸出為 `25.4 mm（1 英吋）`。

## 報告、格式化與剪貼簿

- `numberString()` 使用 `QString::number(value, 'f', 6)` 保留最多六位小數，並移除尾端多餘的零。
- `execComm()` 計算 `dualUnitString(bounds.maxX - bounds.minX, unit)` 與 `dualUnitString(bounds.maxY - bounds.minY, unit)`。
- `LC_ObjectSizeDialog` 使用唯讀 `QTextEdit` 顯示報告，使用 `QDialogButtonBox` 建立按鈕。
- `copyDimensions()` 使用 `QApplication::clipboard()->setText(m_copyText)` 將兩行長寬資訊複製到系統剪貼簿。
- `deleteEntities()` 逐一釋放選取清單中的 `Plug_Entity*`，避免結束量測後留下記憶體。

## 效能與限制

對 `n` 個物件與總共 `v` 個折線頂點，計算量約為 `O(n + v)`；圓弧和橢圓只檢查固定數量的解析極值。插件量測的是圖面座標的軸對齊外框；文字、圖塊、剖面線、標註與 spline 因公開插件 API 沒有提供完整幾何資料，會被列為未支援，不會納入外框。

## 建置

在 LibreCAD 原始碼根目錄執行 qmake 後，進入 `plugins/objectsize` 建置；或在 Qt Creator 開啟 `objectsize.pro`。Windows 可執行：

```powershell
cd H:\librecad\LibreCAD-2.2.0.2\plugins\objectsize
.\build-plugin.ps1
```

Windows 輸出檔名：

```text
windows/resources/plugins/objectsize1.dll
```
