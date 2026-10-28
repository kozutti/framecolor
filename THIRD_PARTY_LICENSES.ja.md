# サードパーティーに関する表示

[English](THIRD_PARTY_LICENSES.md)

この文書は第三者由来のコードと依存関係の説明です．ライセンス本文の日本語訳では
ありません．再配布条件は同梱した各ライセンスの原文を参照してください．

## [frameeyeosc](https://github.com/sasaken1102r/frameeyeosc)（MIT）

ダッシュボードの描画，Vulkanテクスチャのアップロード，OpenVRオーバーレイを
扱う `src/draw.*`，`src/vk_texture.*`，`src/vr_overlay.*` はframeeyeoscの
コードを改変して利用しています．ダッシュボードのデザインも参考にしました．
原作者はkonsti219で，リンク先のフォークにはsasaken1102rによる変更が
含まれます．著作権表示と許諾文は
[`third_party/frameeyeosc-LICENSE`](third_party/frameeyeosc-LICENSE) に保存しています．

## [OpenVR](https://github.com/ValveSoftware/openvr)（BSD-3-Clause）

`third_party/openvr/openvr.h` はOpenVR SDKのヘッダーです．ライセンスは
[`third_party/openvr/LICENSE`](third_party/openvr/LICENSE) に保存しています．

## [vkBasalt](https://github.com/DadSchoorse/vkBasalt)（zlib）

`third_party/vkbasalt/` のVulkanレイヤーは，vkBasaltの
[コミット `4f97f09`](https://github.com/DadSchoorse/vkBasalt/commit/4f97f09ffe91900e6ca136cc26cf7966f8f6970d)
を元にした，3D LUT機能のみを残す改変版です．
zlibライセンスは
[`third_party/vkbasalt/LICENSE`](third_party/vkbasalt/LICENSE) に保存しています．
FrameColorでは対象を `vrcompositor` に限定し，ユーザー設定に応じてLUTを
更新します．また，使用しないエフェクト，入力フック，深度キャプチャ，
画像ローダー，ReShadeコンパイラーの機能を除いています．

この非公式フォークでは，さらにレイヤーを改変しています．LUTフォルダーからの
ファイル選択，実行中のON/OFF切り替え，別スレッドでの読み込み，レイヤーの
LUTサイズへの補間を追加しました（`effect_lut.*`，`lut_cube.*`，`meson.build`）．
`look_transform.hpp`，`stable_file_change.hpp`，`lut_control.hpp` は
FrameColorで新しく書いたコードで，MITライセンスです．

ソースと配布用アーカイブには，NVIDIA FXAA，AMD CAS，SMAA，DLS，ReShade
コンパイラー，stb画像ローダー，SPIRV-Headersのファイルは含まれません．
残した二つのGLSLシェーダーはvkBasalt由来です．

## システム側の依存関係

[Cairo](https://www.cairographics.org/)，[FreeType](https://freetype.org/)，
[Vulkan](https://www.vulkan.org/)，C/C++ランタイムはSteam Frame側にあるものを
利用します．
