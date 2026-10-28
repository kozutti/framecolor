# 色補正LUT

[English](EXTERNAL_LUT.md)

`.cube` 形式の3D LUTを、SteamVRダッシュボードから選んでON/OFFできます。切り替えはSteamVRを起動したまま反映されます。

## 使い方

`.cube` ファイルを次のフォルダーに置きます。フォルダーがなければ、インストーラーまたはダッシュボードが作成します。

```text
~/Documents/FrameColor/luts/
```

ダッシュボード上部の「色補正LUT」で、ON/OFFボタンを押すと切り替わり、`<` `>` でファイルを選べます。ファイルを選ぶと自動的にONになります。フォルダーは2秒ごとに確認するので、ファイルを追加するとすぐ一覧に出ます。切り替えは1秒ほどで反映され、SteamVRの再起動は不要です。右上に「適用中」「OFF（元の色）」「エラー」などの状態が出ます。

インストールすると、同梱の測定LUT `quest3-match-v1.cube`（Quest 3 の色に近づけるもの）がこのフォルダーに入ります。初めてインストールしたときは、このLUTが選ばれてONになります。すでにLUTを選んでいた場合（OFFにしていた場合も含む）は、その設定をそのまま残します。詳しくは [luts/README.ja.md](luts/README.ja.md) を参照してください。

選んだLUTの上に、明るさ・彩度・コントラスト・ガンマの調整が重なります。LUTだけの色にしたいときは「標準」プリセットに戻してください。パネル輝度は別の設定です。

対応する `.cube` は、サイズ2〜65、ドメイン0〜1、RGB値が0〜1の有限な数のものです。サイズの違うLUTは、内部のサイズに合わせて補間します。16ビットの線形フィルターに対応しないGPUでは8ビットで処理します。

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

`disable` は `off` の別名です。`install` はLUTを検査してフォルダーにコピーし、選択してONにします。`--restart` を付けるとSteamVRも再起動しますが、通常の選択・ON/OFF・インストールには必要ありません。

`status` は、ダッシュボードからレイヤーへ渡す `lut.txt` と、レイヤーが書く `lut-status.txt` を表示します。読み込みに失敗した場合は、それまでのLUTを使い続け、理由を状態に表示します。

## 旧形式からの移行

インストール時に `~/.config/framecolor/external-lut.cube` があり、`lut.txt` がまだない場合、インストーラーはそのファイルをフォルダーへコピーして選択状態にします。元のファイルは残ります。移行していない古い設定でも、`lut.txt` がなく旧ファイルがあれば、レイヤーはそれを読み込みます。

アンインストールでは `lut.txt` と `lut-status.txt` を削除します。LUTフォルダー、LUTファイル、旧形式のLUTは削除しません。

## 測定・検証するときの注意

プリセットやスライダーの調整は、LUT適用後の色を変えます。LUT単体を確認するときは「標準」プリセットに戻してください。パネル輝度は測定時の値のままにしてください。

元プロジェクトおよび第三者由来コードのライセンス表示はそのまま保持しています。
