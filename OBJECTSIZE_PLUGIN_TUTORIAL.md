# LibreCAD「物件長寬」插件教學

## 功能

「物件長寬」插件可以讀取 LibreCAD 中選取的物件，計算其 X、Y 軸對齊外框，並同時顯示毫米與英吋。

例如：

```text
長度（X）：25.4 mm（1 英吋）
寬度（Y）：50.8 mm（2 英吋）
```

按下「複製長寬」後，以上兩行會複製到剪貼簿，可直接貼到記事本、Excel 或報表中。

## 安裝插件

1. 先關閉 LibreCAD，避免舊版 DLL 被鎖定。
2. 將編譯完成的 `objectsize1.dll` 放到使用者插件資料夾：

   ```text
   %USERPROFILE%\Documents\LibreCAD\plugins
   ```

3. 重新啟動 LibreCAD。
4. 從選單開啟 `Plugins > 物件長寬`。

在 Windows 的本專案中，也可以使用建置腳本自動編譯並安裝：

```powershell
cd H:\librecad\LibreCAD-2.2.0.2\plugins\objectsize
.\build-plugin.ps1 -Deploy
```

## 使用方式

1. 開啟 DXF 圖面。
2. 開啟 `Plugins > 物件長寬`。
3. 點選要量測的物件；可使用 Ctrl 或框選進行多選。
4. 完成選取後按 Enter。
5. 在結果視窗查看長度、寬度與外框座標。
6. 按「複製長寬」複製雙單位結果。

多選時，插件會計算所有可支援物件的整體外框，而不是將每個物件的尺寸相加。

## 單位判斷與換算

插件會讀取 DXF 的 `$INSUNITS` 變數判斷原始圖面單位，再換算成兩種結果：

- 毫米：`mm`
- 英吋：`英吋`，換算公式為 `英吋 = 毫米 ÷ 25.4`

常見換算如下：

| `$INSUNITS` | 原始單位 | 毫米換算 |
|---:|---|---:|
| 1 | 英吋 | 1 in = 25.4 mm |
| 4 | 毫米 | 1 mm = 1 mm |
| 5 | 公分 | 1 cm = 10 mm |
| 6 | 公尺 | 1 m = 1000 mm |

如果檔案沒有設定 `$INSUNITS`，插件會將圖面數值視為毫米，並在結果中顯示「未指定（按 mm）」。

## 支援的物件

目前支援：

- 直線
- 圓
- 圓弧
- 橢圓
- 折線，包含 bulge 圓弧
- 圖片

文字、圖塊、剖面線、標註與 spline 的完整幾何資料目前沒有由 LibreCAD 公開插件介面提供，因此會在結果中列為未支援幾何。

## 建置原理

插件使用 LibreCAD 的 `QC_PluginInterface` 與 `Document_Interface`：

1. `getSelect()` 取得使用者選取的物件。
2. `Plug_Entity::getData()` 取得物件座標與幾何參數。
3. 插件計算每個物件的外框並合併成整體外框。
4. 透過 `$INSUNITS` 將結果轉成 mm，再除以 25.4 得到英吋。
5. 使用 Qt `QClipboard` 複製長寬資訊。

原始碼位於 [`plugins/objectsize`](plugins/objectsize)。
