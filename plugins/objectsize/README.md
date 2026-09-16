# Object Size plugin

`Plugins > 物件長寬` 會要求選取物件，然後顯示所選物件集合的軸對齊外框：

- 長度（X）：`max X - min X`，同時顯示 mm 與英吋
- 寬度（Y）：`max Y - min Y`，同時顯示 mm 與英吋

顯示格式範例：`25.4 mm（1 英吋）`。對話框中的「複製長寬」會將兩種單位一起複製到剪貼簿。插件會自動讀取 DXF 的 `$INSUNITS` 判斷檔案單位並換算；未設定單位時按 mm 處理。支援直線、圓、圓弧、橢圓、折線（包含 bulge 圓弧）與圖片；多選時會計算整體外框。

## 建置

在 LibreCAD 原始碼根目錄執行 qmake 後，進入 `plugins/objectsize` 建置；或在 Qt Creator 開啟 `objectsize.pro`。使用目前 Windows Qt SDK 時，也可以執行：

```powershell
cd H:\librecad\LibreCAD-2.2.0.2\plugins\objectsize
.\build-plugin.ps1
```

Windows 輸出檔名為：

```text
windows/resources/plugins/objectsize1.dll
```
