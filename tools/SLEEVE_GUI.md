# Python 長袖 DXF GUI

`sleeve_gui.py` 是獨立的 Python/Tkinter 介面，不會改變原本的
LibreCAD Sleeve Analyzer 插件。它可以讀取含有閉合袖片的 DXF，依檔名辨識尺寸，
選擇同尺寸的不同袖長款式，另存新的長袖 DXF。尺寸資料固定從腳本相對位置讀取：
`plugins/sleeveanalyzer/sleeve_sizes.json`。

## GUI 啟動

在 `tools` 目錄執行：

```powershell
python .\sleeve_gui.py
```

GUI 欄位均使用 mm：

- `袖子 DXF`：選擇輸入檔，支援 classic `POLYLINE` 與 `LWPOLYLINE`。
- 若已安裝 `tkinterdnd2`，也可以直接從 Windows 檔案總管把單一 `.dxf` 檔案拖到 GUI 視窗，程式會自動填入來源檔案與預設輸出檔名。
- `輸出 DXF`：選擇新檔案位置。
- `袖長`：長袖實際尺寸內框的袖軸跨度，例如 `627`。
- `袖口寬`：袖口寬度，例如 `225.17`。
- `縫份`：裁切外框外擴量，預設 `7.9375` mm，與原功能的 fallback 相同。
- `袖口折口距離`：裁片線至折口的距離，預設 `19.05` mm（0.75 英吋）。

左側預覽區會依照 Plugins 內 Sleeve Analyzer 的分析資料顯示原始袖片、袖山、
P1/P2/P3、袖軸、原袖口、目標袖口，以及長袖上下側線。選擇或拖入來源 DXF 後，
程式只會從檔名的獨立 token 辨識尺寸；無法辨識或有多尺寸衝突時會清除目前選擇，
要求手動選取，不會沿用前一個檔案的尺寸。修改尺寸欄位只影響本次預覽/輸出，畫面會
標示「未存入尺寸表」；可用「儲存至目前款式」或「另存新款式」明確寫入。

「管理尺寸」視窗支援尺寸與款式的新增、複製、改名、刪除、設為預設及數值編輯。
它在暫存副本上工作，按「取消」不落盤；儲存會先驗證、建立同目錄的
`sleeve_sizes.json.bak` 備份，再用暫存檔原子替換。若 JSON 遺失、損壞或被其他程式
修改，會顯示完整路徑與原因，不會用空資料覆蓋。舊款式的 `cuff_width_mm: 0` 會在
執行時回退到同尺寸 `standard` 的正值，讀取時不會偷偷改寫 JSON。

輸出只建立新的長袖裁片，不會把原始短袖一起輸出，並建立兩個圖層：

- `LONG_SLEEVE_SEAM`：長袖實際尺寸內框。
- `LONG_SLEEVE_CUT`：縫份裁切外框，袖山外框沿用輸入袖片的原始牙口細節。

## 命令列模式

```powershell
python .\sleeve_gui.py input.dxf output_long_sleeve.dxf `
  --sleeve-length 627 --cuff-width 225.17 --cuff-fold-distance 19.05
```

預設不輸出原始短袖；若需要診斷用的原始袖片，可額外加入
`--include-source`。

若要啟用拖放功能，可安裝選用套件：

```powershell
python -m pip install tkinterdnd2
```

## 測試

```powershell
python .\test_sleeve_gui.py -v
```
