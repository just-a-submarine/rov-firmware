#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""離線 OSM 圖磚下載器 — 為地面站 LittleFS 預打包目標水域底圖。

用途：整套系統現場無對外網路（地面站純 AP），線上圖磚一定載不到。
本腳本事先抓「指定四個水域」的 OSM 街道圖磚，存進 data/www/tiles/{z}/{x}/{y}.png，
Leaflet 改指向本地路徑即可離線顯示。

圖源選擇：
  - OSM 官方 tile.openstreetmap.org **禁止批次下載**（會回 403「Access blocked」圖），不可用。
  - 改用 CARTO Voyager（底圖＝OSM 資料、OSM 街道樣式、免金鑰 raster），允許小量離線快取。
  - 備援：Esri World Imagery（衛星，免金鑰）。切換 SOURCE 變數即可。

禮貌原則：
  - 帶可識別 User-Agent；循序下載、每張間隔；已存在則略過（可重跑續抓）。
  - 僅抓四個小水域 z15–17，總量 ~百餘張，一次性快取，非持續打點。
  - 下載後檢查圖磚雷同度，偵測「封鎖頁/錯誤頁」被當成圖磚存下。
"""
import hashlib
import math
import os
import time
import urllib.request
import urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.normpath(os.path.join(HERE, "..", "data", "www", "tiles"))

# 可切換圖源（url 模板, 對應 Leaflet attribution）
SOURCES = {
    "carto_voyager": ("https://basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png",
                      "© OpenStreetMap contributors © CARTO"),
    "esri_imagery":  ("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
                      "Imagery © Esri"),
}
SOURCE = "carto_voyager"
TILE_URL = SOURCES[SOURCE][0]
USER_AGENT = "ROV-Submarine-School-Project/1.0 (offline map cache; nico.94624@gmail.com)"
ZOOMS = [15, 16, 17]
SLEEP_S = 0.12  # 對圖源伺服器友善

# 四個目標水域 (lat_min, lat_max, lon_min, lon_max)
# 座標經 Nominatim(OSM) 地理編碼校正：雙溪濕地公園/福林橋、大湖公園、碧湖公園、美堤/迎風河濱公園。
SITES = {
    "waishuangxi":  (25.0955, 25.1015, 121.5095, 121.5310),  # 外雙溪下游（雙溪濕地公園→福林橋，士林）
    "dahu":         (25.0775, 25.0880, 121.6000, 121.6088),  # 大湖公園（內湖）：西砍住宅、東補貼湖面、南再補一排
    "bihu":         (25.0795, 25.0850, 121.5805, 121.5870),  # 碧湖公園（內湖）
    "keelung_meidi":(25.0700, 25.0805, 121.5520, 121.5710),  # 基隆河 美堤/迎風河濱公園（自然親水彎）
}


def deg2num(lat_deg, lon_deg, z):
    lat_r = math.radians(lat_deg)
    n = 2 ** z
    x = int((lon_deg + 180.0) / 360.0 * n)
    y = int((1.0 - math.asinh(math.tan(lat_r)) / math.pi) / 2.0 * n)
    return x, y


def tiles_for_bbox(bbox, z):
    lat_min, lat_max, lon_min, lon_max = bbox
    x0, y0 = deg2num(lat_max, lon_min, z)  # 左上
    x1, y1 = deg2num(lat_min, lon_max, z)  # 右下
    for x in range(min(x0, x1), max(x0, x1) + 1):
        for y in range(min(y0, y1), max(y0, y1) + 1):
            yield x, y


def collect():
    """彙整所有場域所有 zoom 的 (z,x,y)，跨場域去重。"""
    wanted = set()
    for bbox in SITES.values():
        for z in ZOOMS:
            for x, y in tiles_for_bbox(bbox, z):
                wanted.add((z, x, y))
    return sorted(wanted)


def download(z, x, y):
    path = os.path.join(OUT_DIR, str(z), str(x), f"{y}.png")
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return os.path.getsize(path), True  # 已存在，略過
    os.makedirs(os.path.dirname(path), exist_ok=True)
    req = urllib.request.Request(TILE_URL.format(z=z, x=x, y=y),
                                 headers={"User-Agent": USER_AGENT})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                data = r.read()
            with open(path, "wb") as f:
                f.write(data)
            time.sleep(SLEEP_S)
            return len(data), False
        except urllib.error.HTTPError as e:
            if e.code in (429, 503) and attempt < 2:
                time.sleep(2 * (attempt + 1))
                continue
            raise
        except (urllib.error.URLError, TimeoutError):
            if attempt < 2:
                time.sleep(1.5)
                continue
            raise


def optimize_tiles():
    """把圖磚量化為 8-bit 調色盤 PNG，大幅縮小（地圖色彩少，視覺近乎無損）。

    LittleFS 分割僅 2MB，CARTO raster 原圖約 1.7MB 逼近上限；量化後通常砍半。
    需 Pillow；未安裝則略過並提示。
    """
    try:
        from PIL import Image
    except ImportError:
        print("⚠ 未安裝 Pillow，略過圖磚壓縮（pip install pillow）")
        return
    before = after = count = 0
    for root, _dirs, files in os.walk(OUT_DIR):
        for fn in files:
            if not fn.endswith(".png"):
                continue
            p = os.path.join(root, fn)
            b = os.path.getsize(p)
            with Image.open(p) as im:
                # 轉 RGB 後以中位切割量化至 256 色，再存最佳化調色盤 PNG
                q = im.convert("RGB").quantize(colors=256, method=Image.MEDIANCUT)
                q.save(p, format="PNG", optimize=True)
            before += b
            after += os.path.getsize(p)
            count += 1
    if count:
        print(f"壓縮 {count} 張：{before/1024:.0f} KB → {after/1024:.0f} KB "
              f"（省 {100*(before-after)/before:.0f}%，現 {after/1024/1024:.2f} MB）")


def main():
    import sys
    if "--optimize-only" in sys.argv:
        optimize_tiles()
        return
    wanted = collect()
    print(f"目標圖磚總數（去重後）：{len(wanted)}  zooms={ZOOMS}")
    per_z = {}
    for z, _, _ in wanted:
        per_z[z] = per_z.get(z, 0) + 1
    print("各 zoom 張數：", per_z)

    total_bytes = 0
    skipped = 0
    failed = []
    for i, (z, x, y) in enumerate(wanted, 1):
        try:
            n, was_cached = download(z, x, y)
            total_bytes += n
            if was_cached:
                skipped += 1
        except Exception as e:  # noqa: BLE001 — 邊界記錄後續行，不靜默吞掉
            failed.append((z, x, y, str(e)))
        if i % 25 == 0 or i == len(wanted):
            print(f"  進度 {i}/{len(wanted)}  累計 {total_bytes/1024:.0f} KB  略過(已存){skipped}  失敗{len(failed)}")

    print(f"\n完成：{len(wanted)-len(failed)} 張，總計 {total_bytes/1024:.0f} KB "
          f"（{total_bytes/1024/1024:.2f} MB），已存略過 {skipped} 張，圖源={SOURCE}")
    if failed:
        print(f"⚠ 失敗 {len(failed)} 張：")
        for z, x, y, err in failed[:10]:
            print(f"   z{z}/{x}/{y}: {err}")

    # 雷同度檢查：偵測「封鎖頁/錯誤頁」被當成圖磚（同一張圖佔多數）
    hashes = {}
    for z, x, y in wanted:
        p = os.path.join(OUT_DIR, str(z), str(x), f"{y}.png")
        if os.path.exists(p):
            with open(p, "rb") as f:
                h = hashlib.md5(f.read()).hexdigest()
            hashes[h] = hashes.get(h, 0) + 1
    if hashes:
        top = max(hashes.values())
        print(f"唯一圖磚數：{len(hashes)} / {sum(hashes.values())}；最常見單張出現 {top} 次")
        if top > len(wanted) * 0.4:
            print("🚨 警告：多數圖磚雷同，極可能是封鎖頁/錯誤頁，請換圖源或檢查！")
        else:
            print("✅ 圖磚多樣性正常（非封鎖頁）")


if __name__ == "__main__":
    main()
