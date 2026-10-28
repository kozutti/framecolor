# 測定LUTライブラリ

FrameColorは、測定した `.cube` LUTをSteamVRダッシュボードから選び、SteamVRを起動したまま有効・無効にできます。

## 使い方

`.cube` ファイルを次のフォルダーに置きます。フォルダーがなければ、インストーラーまたはダッシュボードが作成します。

```text
~/Documents/FrameColor/luts/
```

ダッシュボード上部の「色補正LUT」で、ON/OFFボタンを押すと切り替わり、`<` `>` でファイルを選べます。ファイルを選ぶと自動的にONになります。フォルダーは2秒ごとに確認するので、ファイルを追加するとすぐ一覧に出ます。切り替えは1秒ほどで反映され、SteamVRの再起動は不要です。右上に「適用中」「OFF（元の色）」「エラー」などの状態が出ます。

有効なLUTを基礎にして、明るさ・彩度・コントラスト・ガンマの調整を重ねます。測定値をそのまま使う場合は、RGB調整を標準値に戻してください。パネル輝度は別設定です。

対応する `.cube` はサイズ2〜65、ドメイン0〜1、有限なRGB値0〜1です。サイズの違うLUTはレイヤーのテクスチャサイズに合わせて補間します。16ビットの線形フィルターに対応しないGPUでは8ビット形式を使います。

## コマンド

```sh
~/.local/bin/framecolor-lut list
~/.local/bin/framecolor-lut select quest3-match-v1.cube
~/.local/bin/framecolor-lut on
~/.local/bin/framecolor-lut off
~/.local/bin/framecolor-lut status
~/.local/bin/framecolor-lut validate ~/my-measurement.cube
~/.local/bin/framecolor-lut install ~/my-measurement.cube
```

`disable` は `off` の別名です。`install` はLUTを検査してライブラリにコピーし、選択して有効にします。`--restart` を明示した場合はSteamVRも再起動します。通常の選択・ON/OFF・インストールには必要ありません。

`status` はダッシュボードからレイヤーへ渡す `lut.txt` と、レイヤーが書く `lut-status.txt` を表示します。読み込みに失敗した場合は現在のLUTを保ち、理由をステータスに表示します。

## 旧形式からの移行

インストール時に `~/.config/framecolor/external-lut.cube` があり、`lut.txt` がまだない場合、インストーラーはそのファイルをライブラリへコピーして選択状態を作成します。移行後も元ファイルは残ります。未移行の旧設定でも、`lut.txt` がなく旧ファイルが存在すればレイヤーは引き続き読み込みます。

アンインストールでは `lut.txt` と `lut-status.txt` を削除します。ライブラリフォルダー、測定LUT、および旧形式のLUTは削除しません。

## 検証時の注意

プリセットやスライダーのRGB調整はLUT適用後の色を変えます。LUT単体を確認するときは「標準」プリセットに戻してください。パネル輝度は測定時の値を維持してください。

元プロジェクトおよび第三者由来コードのライセンス表示はそのまま保持しています。
