# VRColor用の外部LUT拡張（実機検証前）

元のFrameColorに次の変更を追加しています。

- `~/.config/framecolor/external-lut.cube` がSteamVR起動時に存在すれば外部LUTモードになります。起動後にLUTを置き換えた場合も、約1秒以内に自動で再読込します。スライダーやプリセットのRGB調整は外部LUT適用後に重ねて反映されます。
- 16ビットUNORMのフィルター・転送対応を確認し、対応時は16ビットでLUTを保持。非対応は8ビットへ戻しログを出します。
- 起動時の破損LUTは恒等LUTを使用します。起動後の置換ファイルが破損・サイズ違い・形式非対応の場合は現在のLUTを維持して警告します。対応形式はサイズ2〜65、ドメイン0〜1、有限値0〜1の標準3D `.cube` です。
- ダッシュボードに外部LUTモードを表示します。RGB調整の初期値と「リセット」後の値は標準値（明るさ・ガンマ・彩度は1、コントラストは0）で、外部LUTのみが適用されます。パネル輝度は引き続き操作可能です。
- `framecolor-lut install FILE [--restart]`、`disable [--restart]`、`status`、`validate FILE` を追加。導入前に検査し、旧ファイルはバックアップします。

導入例（改造版をビルド・インストールしたFrame上）：

```sh
~/.local/bin/framecolor-lut install ~/quest-to-frame.cube
~/.local/bin/framecolor-lut status
~/.local/bin/framecolor-lut disable --restart
```

外部LUTモードで実行中なら、`install` 後にSteamVRを再起動する必要はありません。スライダーモードから外部LUTモードへ、またはその逆へ切り替える場合だけ再起動してください。

スライダーやプリセットが標準値以外だと測定結果が変わります。検証・測定時は「リセット」を選び、RGB調整を標準値にしてください。パネル輝度は測定時の値を維持してください。

スライダーモードと外部LUTモードの切替にはSteamVR再起動が必要です。診断用LUT・恒等LUT・測定からの生成方法はVRColorの日本語READMEを参照してください。元のスライダー設定は保持します。アンインストール時にも測定LUTのバックアップは残します。

Windows上で新しいlook_transform.hppのホスト単体テストをMSVCで実行しました。g++ランナーと変更したVulkan処理の構文コンパイル、Frame上のビルド・実行、シェーダーの表示結果は未確認です。元プロジェクトのライセンス表記を保持しています。
