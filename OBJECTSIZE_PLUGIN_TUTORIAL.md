# LibreCAD「物件長寬」插件教學與程式原理

## 1. 功能與輸出結果

「物件長寬」插件會讀取 LibreCAD 中目前選取的物件，計算所有物件合併後的**軸對齊外框**（Axis-Aligned Bounding Box，AABB），再顯示 X 軸方向的長度與 Y 軸方向的寬度。

例如選取範圍換算後為 25.4 mm × 50.8 mm，結果會顯示：

```text
長度（X）：25.4 mm（1 英吋）
寬度（Y）：50.8 mm（2 英吋）
```

這裡的「長」與「寬」是圖面座標系統 X、Y 方向的外框尺寸，不是物件的周長、面積，也不是旋轉後的最小外接矩形。按下「複製長寬」後，兩行結果會複製到剪貼簿，可貼到記事本、Excel 或報表。

## 2. 安裝插件

1. 先關閉 LibreCAD，避免舊版 DLL 被鎖定。
2. 將編譯完成的 `objectsize1.dll` 放到使用者插件資料夾：

   ```text
   %USERPROFILE%\Documents\LibreCAD\plugins
   ```

3. 重新啟動 LibreCAD。
4. 從選單開啟 `Plugins > 物件長寬`。

在本專案的 Windows 環境，也可以使用建置腳本自動編譯並安裝：

```powershell
cd H:\librecad\LibreCAD-2.2.0.2\plugins\objectsize
.\build-plugin.ps1 -Deploy
```

## 3. 使用方式

1. 開啟 DXF 圖面。
2. 開啟 `Plugins > 物件長寬`。
3. 點選要量測的物件；可按住 Ctrl 多選或使用框選。
4. 完成選取後按 Enter。
5. 在結果視窗查看長度、寬度、外框座標與圖面單位。
6. 按「複製長寬」複製雙單位結果。

多選時，插件計算的是所有可支援物件的**聯集外框**，不會把每個物件的長度或寬度相加。例如兩條相距很遠的線段，長度會包含兩條線段之間的空白範圍，因為那是整體外框的 X 軸範圍。

## 4. 程式架構與執行流程

主要程式位於 [`plugins/objectsize/objectsize.cpp`](plugins/objectsize/objectsize.cpp)，介面宣告位於 [`plugins/objectsize/objectsize.h`](plugins/objectsize/objectsize.h)。整體流程如下：

1. LibreCAD 載入 DLL 與 `objectsize.json` 的 Qt 插件 metadata。
2. `LC_ObjectSizePlugin::getCapabilities()` 宣告插件要在 `plugins_menu` 建立「物件長寬」選單項目。
3. 使用者按下選單後，LibreCAD 呼叫 `LC_ObjectSizePlugin::execComm()`。
4. `execComm()` 呼叫 `Document_Interface::getSelect()`，讓使用者選取物件並取得 `QList<Plug_Entity*>`。
5. 對每個 `Plug_Entity` 呼叫 `addEntityBounds()`。該函式使用 `Plug_Entity::getData()` 取得 `QHash<int, QVariant>` 幾何資料，再依 `DPI::ETYPE` 分派到直線、圓、圓弧、橢圓、折線或圖片的處理分支。
6. 每個物件先計算自己的 `[minX, minY, maxX, maxY]`，再交給總外框 `Bounds` 合併。
7. `drawingUnitInfo()` 讀取 DXF 的 `$INSUNITS`，取得原始圖面單位到毫米的倍率。
8. `dualUnitString()` 先將圖面數值換成毫米，再用 `mm / 25.4` 換成英吋。
9. `LC_ObjectSizeDialog::setReport()` 將報告放入唯讀文字框；`copyDimensions()` 使用 Qt 剪貼簿 API 複製兩行長寬資訊。
10. `deleteEntities()` 釋放 LibreCAD API 回傳的選取物件指標。

### 函式與責任對照

| 函式或資料結構 | 使用的 API／演算法 | 責任 |
|---|---|---|
| `Bounds::add()` | `std::isfinite`、`std::min`、`std::max` | 累積一組點的軸對齊最小／最大座標 |
| `positiveAngle()` | `std::fmod` | 將角度正規化到 `0` 至 `2π` |
| `directedSweep()` | `std::fmod`、`std::fabs` | 計算順向或反向圓弧掃掠角 |
| `angleOnArc()` | 角度差與容許誤差 | 判斷極值角是否落在圓弧範圍 |
| `addCircularArcBounds()` | `sin`、`cos`、四個基準角 | 找出圓弧端點與 X/Y 極值 |
| `addBulgeBounds()` | DXF bulge 幾何、`atan`、`hypot` | 將折線 bulge 還原為圓弧後求外框 |
| `ellipsePoint()` | 旋轉橢圓參數式 | 取得橢圓上指定參數的座標 |
| `addEllipseBounds()` | 導數為零的極值參數 | 求完整或部分橢圓的精確外框 |
| `addEntityBounds()` | `getData()`、`getPolylineData()`、`switch` | 依物件類型取得外框 |
| `drawingUnitInfo()` | `getVariableInt("$INSUNITS")` | 判斷原始單位與毫米換算倍率 |
| `dualUnitString()` | `mm = value × 倍率`、`inch = mm / 25.4` | 格式化雙單位數值 |
| `execComm()` | `getSelect()`、外框合併 | 控制整個量測流程並建立報告 |
| `copyDimensions()` | `QApplication::clipboard()` | 將長寬文字複製到剪貼簿 |

## 5. 長寬的實際計算方法

### 5.1 點集合與軸對齊外框

對一個物件或多個物件，先取得幾何上需要納入的點集合 `P`。對每個點 `p = (x, y)` 計算：

```text
minX = min 所有點的 x
maxX = max 所有點的 x
minY = min 所有點的 y
maxY = max 所有點的 y
```

外框四個角落可以表示為：

```text
(minX, minY)  ─────  (maxX, minY)
     │                    │
(minX, maxY)  ─────  (maxX, maxY)
```

因此插件使用下列公式：

```text
長度（X） = maxX - minX
寬度（Y） = maxY - minY
```

程式中由 `Bounds` 結構保存四個值。第一次加入有效點時，四個邊界都設成該點；後續每加入一點，就用 `std::min` 更新左邊界／下邊界，用 `std::max` 更新右邊界／上邊界。遇到 `NaN` 或無限大的座標時，`std::isfinite` 會阻止它污染結果。

### 5.2 多選物件如何合併

每個物件先得到自己的外框 `entityBounds`。`execComm()` 不需要重新走訪物件內的所有點，只要把物件外框的左下角 `(minX, minY)` 與右上角 `(maxX, maxY)` 加入總 `Bounds`，就能得到所有物件的聯集外框：

```cpp
Bounds bounds;
for (Plug_Entity* entity : selected) {
    Bounds entityBounds;
    if (!addEntityBounds(entity, &entityBounds))
        continue;

    bounds.add(QPointF(entityBounds.minX, entityBounds.minY));
    bounds.add(QPointF(entityBounds.maxX, entityBounds.maxY));
}

const double length = bounds.maxX - bounds.minX;
const double width  = bounds.maxY - bounds.minY;
```

因為 `entityBounds` 本身已經是軸對齊矩形，所以只加入左下角與右上角，就足以維持四個方向的最小值與最大值。

### 5.3 各種物件的外框演算法

#### 點與直線

- 點：使用 `DPI::STARTX`、`DPI::STARTY` 加入一個座標。
- 直線：使用 `DPI::STARTX/STARTY` 與 `DPI::ENDX/ENDY` 加入兩個端點。

直線的 X/Y 極值一定出現在兩個端點，因此不需要取樣。

#### 圓

圓由中心 `(cx, cy)` 與半徑 `r` 定義，外框直接是：

```text
minX = cx - |r|       maxX = cx + |r|
minY = cy - |r|       maxY = cy + |r|
```

程式以 `std::fabs(radius)` 確保半徑為正，再加入 `(cx-r, cy-r)` 與 `(cx+r, cy+r)`。

#### 圓弧

圓弧不能只使用起點與終點，因為圓弧中間可能超過端點的 X 或 Y。`addCircularArcBounds()` 採用精確的候選點：

1. 加入起點角度與終點角度。
2. 檢查四個圓的基準極值角：`0`、`π/2`、`π`、`3π/2`。
3. 只有當 `angleOnArc()` 判斷該角度位於實際圓弧掃掠範圍內，才加入該點。
4. 圓上點使用 `addArcPoint()` 計算：

   ```text
   x = cx + r × cos(θ)
   y = cy + r × sin(θ)
   ```

`positiveAngle()` 以 `std::fmod()` 將角度折回 `0` 至 `2π`；`directedSweep()` 依 `DPI::REVERSED` 判斷順時針或逆時針；`angleOnArc()` 再用掃掠角和 `kEpsilon = 1e-10` 判斷極值是否在弧段內。這種方法比固定間隔取樣可靠，因為不會漏掉真正的 X/Y 極值。

#### 橢圓

橢圓資料包含中心、主軸向量、短軸與主軸的比例、起訖參數以及方向。`ellipsePoint()` 使用旋轉橢圓參數式：

```text
x = cx + majorX × cos(t) - ratio × majorY × sin(t)
y = cy + majorY × cos(t) + ratio × majorX × sin(t)
```

若起始參數與結束參數都接近零，程式視為完整橢圓，直接用下列半徑求外框：

```text
xRadius = hypot(majorX, ratio × majorY)
yRadius = hypot(majorY, ratio × majorX)
```

若是部分橢圓，程式會加入起點、終點，以及 X 或 Y 導數為零的四個候選參數：

```text
xExtreme = atan2(-ratio × majorY, majorX)
yExtreme = atan2( ratio × majorX, majorY)
```

再加上各自的 `π` 對向角，並使用 `angleOnArc()` 判斷是否落在部分橢圓範圍內。這是解析幾何的極值計算，不是低精度的畫面取樣。

#### 折線與 bulge 圓弧

`getPolylineData()` 取得 `QList<Plug_VertexData>`。每個頂點先加入外框；相鄰頂點之間的 `bulge` 再由 `addBulgeBounds()` 處理。

- `bulge` 接近零：該段是直線，只加入起點與終點。
- `bulge` 不為零：依 DXF 定義 `bulge = tan(包含角 / 4)`，先計算包含角 `alpha = 4 × atan(bulge)`。
- 以弦長 `chord = hypot(dx, dy)`、半弦長與 `sin(alpha / 2)` 計算圓弧半徑。
- 找出弦中點與垂直方向，反推出圓心，再轉呼叫 `addCircularArcBounds()` 求精確弧段外框。

若 `DPI::CLOSEPOLY` 不為零，最後一個頂點到第一個頂點也會納入計算；否則只計算相鄰的開放線段。

#### 圖片

圖片的資料包含插入點、U/V 方向向量，以及 `SIZEU`、`SIZEV` 尺寸。程式先算出：

```text
u = U方向向量 × SIZEU
v = V方向向量 × SIZEV
```

再加入四個角落：

```text
插入點
插入點 + u
插入點 + v
插入點 + u + v
```

因此即使圖片有旋轉，仍可得到正確的圖面座標軸對齊外框。

#### 未支援的物件

目前公開的 LibreCAD 插件介面沒有提供文字、圖塊、剖面線、標註與 spline 的完整幾何資料，因此 `addEntityBounds()` 會回傳 `false`，`execComm()` 會計數並在報告中列出未支援數量，而不會假裝算出錯誤尺寸。

## 6. 單位自動判斷與雙單位換算

`drawingUnitInfo()` 呼叫：

```cpp
document->getVariableInt(QStringLiteral("$INSUNITS"), &units)
```

取得 DXF header 的 `$INSUNITS` 整數代碼，再由 `switch` 對應到 `toMillimeters` 倍率。完整程式也涵蓋英吋、英尺、英里、毫米、公分、公尺、公里、微英吋、mil、碼、埃、奈米、微米、分米、十米、百米，以及美國測量單位等代碼。

常用代碼如下：

| `$INSUNITS` | 原始單位 | 轉成毫米的倍率 |
|---:|---|---:|
| 1 | 英吋 | `25.4` |
| 2 | 英尺 | `304.8` |
| 3 | 英里 | `1609344.0` |
| 4 | 毫米 | `1.0` |
| 5 | 公分 | `10.0` |
| 6 | 公尺 | `1000.0` |
| 7 | 公里 | `1000000.0` |
| 10 | 碼 | `914.4` |
| 21 | 美國測量英尺 | `304.800609601` |
| 22 | 美國測量英吋 | `25.4000508` |

若 `$INSUNITS` 不存在或代碼未知，程式採用倍率 `1.0`，也就是把圖面數值按毫米處理，並在報告標示未指定或未知單位。這是為了避免沒有單位資訊時產生無法追溯的放大或縮小。

`dualUnitString()` 的實際公式是：

```text
毫米 = 原始圖面數值 × toMillimeters
英吋 = 毫米 ÷ 25.4
```

例如 DXF 使用英吋，圖面長度為 `1`：

```text
毫米 = 1 × 25.4 = 25.4 mm
英吋 = 25.4 ÷ 25.4 = 1 英吋
```

因此輸出會是 `25.4 mm（1 英吋）`。長度與寬度都使用同一個 `$INSUNITS` 倍率，確保多選物件的整體外框在同一單位系統中換算。

## 7. 數字格式與複製流程

`numberString()` 以 `QString::number(value, 'f', 6)` 保留最多六位小數，再移除尾端多餘的零與小數點。例如 `25.400000` 會顯示成 `25.4`。絕對值小於 `0.0000005` 的數值顯示成 `0`，避免浮點運算留下 `-0` 或極小雜訊。

`execComm()` 會分別建立：

```text
length = dualUnitString(maxX - minX, unit)
width  = dualUnitString(maxY - minY, unit)
```

報告文字會顯示物件數量、原始單位、長寬與外框座標；複製文字則保留兩行最容易貼到其他軟體的格式。`LC_ObjectSizeDialog` 使用 `QTextEdit` 顯示唯讀報告，使用 `QDialogButtonBox` 建立「複製長寬」與「關閉」按鈕。

按下複製按鈕後，Qt 函式：

```cpp
QApplication::clipboard()->setText(m_copyText);
```

會將預先準備的文字寫入系統剪貼簿，並透過 `QLabel` 顯示複製成功提示。

## 8. 效能、精度與限制

- 直線、圓、圓弧與橢圓都使用解析幾何的端點／極值計算，不依賴畫面縮放，也不使用固定間隔取樣。
- `n` 個物件與總共 `v` 個折線頂點的計算量約為 `O(n + v)`；每個圓弧與橢圓只檢查固定數量的候選極值。
- 使用 `double` 保存座標與換算結果，並以 `kEpsilon` 處理角度與 bulge 的浮點誤差。
- 量測的是軸對齊外框；如果要找任意旋轉角度下的最小外接矩形，必須另做凸包與旋轉卡尺演算法，本插件目前沒有這項功能。
- 未支援幾何不會被納入總外框，結果視窗會回報未支援數量。

## 9. 建置檔與輸出

在 LibreCAD 原始碼根目錄執行 qmake 後，進入 `plugins/objectsize` 建置；或在 Qt Creator 開啟 `objectsize.pro`。Windows 也可以直接執行：

```powershell
cd H:\librecad\LibreCAD-2.2.0.2\plugins\objectsize
.\build-plugin.ps1
```

Windows 輸出檔名為：

```text
windows/resources/plugins/objectsize1.dll
```

完整原始碼位於 [`plugins/objectsize`](plugins/objectsize)。
