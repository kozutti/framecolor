# 測定LUT

[English](README.md)

## quest3-match-v1.cube

Steam Frame の表示色を、Quest 3 で見たときの色に近づけるための 3D LUT（33³、`.cube`）です。
SpyderX と ArgyllCMS（`spotread`）で両機の表示色を実測し、そこから計算しました。

### 使い方

FrameColor（この改造版）をインストールすると、このファイルは `~/Documents/FrameColor/luts/` に入り、初めてのインストールでは自動で ON になります。

ON/OFF や切り替えは、SteamVR ダッシュボードの「ディスプレイ色調整」を開き、上部の「色補正LUT」で行います。

手動で入れる場合は、このファイルを Frame の `~/Documents/FrameColor/luts/` にコピーします。コマンドでも入れられます：`~/.local/bin/framecolor-lut install quest3-match-v1.cube`

### 測定条件

| 項目 | Quest 3（目標） | Steam Frame（補正対象） |
|---|---|---|
| 接続 | PC SteamVR → Virtual Desktop（AV1、200Mbps、Streamer 1.34.22） | PC SteamVR → Steam Frame ストリーミング |
| リフレッシュレート | 90Hz | 90Hz |
| パネル輝度 | 既定値 | 既定値（Quest と同じ） |
| 測定した眼 | 左眼 | 左眼 |
| センサー | SpyderX + ArgyllCMS `spotread` | 同左 |
| 測定チャート | 9³格子 + 独立検証色 | 9³格子 + 独立検証色 |

FrameColor のスライダーはすべて標準値（「リセット」後の状態）で測定しています。

### 結果（実測、DeltaE76、Quest 3 との差）

| 状態 | 平均 | 95%点 | 最大 |
|---|---:|---:|---:|
| 補正なし | 18.4 | 35.1 | 52.2 |
| このLUTを適用 | 3.2 | 7.1 | 8.2 |

LUT の計算に使っていない検証用の 36 色で比べた値です。補正なしと適用後は、別の測定回の値です。

### 注意

- **個体差があります。** 測定に使った Frame・Quest 3・センサーは各1台です。お使いの Frame で同じ結果になるとは限りません。
- 左眼の中央1点で測っています。右眼、レンズの周辺部、視線の位置による違いは補正していません。
- パネル輝度を測定時から大きく変えると、合い方が変わります。
- Frame で出せない色（色域外）や明るさの差は残ります。
- 目安としての LUT です。違和感があれば OFF にしてください。

### ライセンス

この LUT データは、リポジトリと同じ [MIT ライセンス](../LICENSE) で提供します。
