<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ja" sourcelanguage="en">
<context>
    <name>patchy::ui::PathsPanel</name>
    <message><source>Saved path. Double-click to rename; select to edit with the pen and path tools.</source><translation>保存済みパス。ダブルクリックで名前を変更、選択するとペン/パスツールで編集できます。</translation></message>
    <message><source>Ctrl-click or Ctrl+Enter loads the path as a selection; drag to reorder.</source><translation>Ctrl+クリックまたは Ctrl+Enter でパスを選択範囲として読み込み、ドラッグで並べ替えます。</translation></message>
    <message><source>This is the document&apos;s clipping path.</source><translation>このパスはドキュメントのクリッピングパスです。</translation></message>
    <message><source>The temporary work path. Double-click to save it as a named path.</source><translation>一時的な作業用パス。ダブルクリックで名前付きパスとして保存します。</translation></message>
    <message><source>The active layer&apos;s path (shape or vector mask).</source><translation>アクティブレイヤーのパス（シェイプまたはベクトルマスク）。</translation></message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../src/app/main.cpp" line="+290"/>
        <source>Patchy raster image editor.</source>
        <translation>Patchy ラスター画像エディター。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Image or Photoshop files to open.</source>
        <translation>開く画像または Photoshop ファイル。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Run the profiling stress test and exit (preset: quick, small, standard, or huge).</source>
        <translation>プロファイリング ストレステストを実行して終了します (プリセット: quick、small、standard、huge)。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Directory for stress test reports (with --stress-test).</source>
        <translation>ストレステストのレポート出力先ディレクトリ (--stress-test と併用)。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Save a PNG of the Patchy window to &lt;path&gt;. With a running instance this forwards the request and exits; otherwise the new instance captures after startup and exits.</source>
        <translation>Patchy ウィンドウの PNG を &lt;path&gt; に保存します。実行中のインスタンスがあればリクエストを転送して終了し、なければ新しいインスタンスが起動後にキャプチャして終了します。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Limit --screenshot to the child widget with this Qt object name.</source>
        <translation>--screenshot をこの Qt オブジェクト名の子ウィジェットに限定します。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Limit --screenshot to this region of the captured widget.</source>
        <translation>--screenshot をキャプチャ対象ウィジェットのこの領域に限定します。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Open the given file, save it to &lt;path&gt; (format follows the extension), and exit. Runs unattended: prompts are suppressed and no running instance is reused.</source>
        <translation>指定したファイルを開いて &lt;path&gt; に保存し (形式は拡張子に従います)、終了します。無人実行のためプロンプトは表示されず、実行中のインスタンスも再利用されません。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>With --export: append this text to every text layer, re-rendering each through Patchy&apos;s text engine, before saving.</source>
        <translation>--export と併用: 保存前にこのテキストをすべてのテキストレイヤーに追加し、各レイヤーを Patchy のテキストエンジンで再レンダリングします。</translation>
    </message>
    <message>
        <location filename="../src/ui/image_save_options_dialog.cpp" line="+267"/>
        <source>JPEG Options</source>
        <translation>JPEG オプション</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Quality:</source>
        <translation>品質:</translation>
    </message>
    <message>
        <location line="+109"/>
        <source>BMP Options</source>
        <translation>BMP オプション</translation>
    </message>
    <message>
        <location line="-89"/>
        <source>Icon Options</source>
        <translation>アイコン オプション</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Cursor Options</source>
        <translation>カーソル オプション</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Sizes</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Scaling:</source>
        <translation>拡大縮小:</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>Auto (recommended)</source>
        <translation>自動 (推奨)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nearest neighbor</source>
        <translation>ニアレストネイバー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Smooth</source>
        <translation>スムーズ</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Hotspot X:</source>
        <translation>ホットスポット X:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hotspot Y:</source>
        <translation>ホットスポット Y:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hotspot is in pixels of the largest size; smaller sizes scale it.</source>
        <translation>ホットスポットは最大サイズのピクセル単位で指定します。小さいサイズには自動的に換算されます。</translation>
    </message>
    <message>
        <location line="-181"/>
        <source>Scale:</source>
        <translation>スケール:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1x (nearest neighbor)</source>
        <translation>%1 倍 (ニアレストネイバー)</translation>
    </message>
    <message>
        <location line="+380"/>
        <source>Export Options</source>
        <translation>書き出しオプション</translation>
    </message>
    <message>
        <location filename="../src/ui/sprite_sheet_dialog.cpp" line="+46"/>
        <source>Export Sprite Sheet</source>
        <translation>スプライトシートの書き出し</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>%1 frames, one per visible top-level layer</source>
        <translation>%1 フレーム (表示中のトップレベル レイヤーごとに 1 つ)</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Columns:</source>
        <translation>列数:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Padding:</source>
        <translation>余白:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Transparent background</source>
        <translation>透明な背景</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Sprite Sheet to Layers</source>
        <translation>スプライトシートをレイヤーへ</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Cell width:</source>
        <translation>セルの幅:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cell height:</source>
        <translation>セルの高さ:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Margin:</source>
        <translation>マージン:</translation>
    </message>
    <message>
        <location filename="../src/ui/brush_tip_manager_dialog.cpp" line="+207"/>
        <location filename="../src/ui/sprite_sheet_dialog.cpp" line="+1"/>
        <source>Spacing:</source>
        <translation>間隔:</translation>
    </message>
    <message>
        <location filename="../src/ui/sprite_sheet_dialog.cpp" line="+16"/>
        <source>%1 x %2 = %3 cells</source>
        <translation>%1 x %2 = %3 セル</translation>
    </message>
    <message>
        <source>Image Sequence to Layers</source>
        <translation>画像シーケンスをレイヤーへ</translation>
    </message>
    <message>
        <source>%1 images will import as layers on a %2 x %3 px canvas, in this order:</source>
        <translation>%1 個の画像を次の順序でレイヤーとして読み込みます (カンバス %2 x %3 px):</translation>
    </message>
    <message>
        <source>%1 images will import as layers, in this order:</source>
        <translation>%1 個の画像を次の順序でレイヤーとして読み込みます:</translation>
    </message>
    <message>
        <source>Export Image Sequence</source>
        <translation>画像シーケンスの書き出し</translation>
    </message>
    <message>
        <source>%1 images, one per visible top-level layer</source>
        <translation>%1 個の画像 (表示中のトップレベル レイヤーごとに 1 つ)</translation>
    </message>
    <message>
        <source>Export visible layers only</source>
        <translation>表示中のレイヤーのみ書き出し</translation>
    </message>
    <message>
        <source>Export all layers</source>
        <translation>すべてのレイヤーを書き出し</translation>
    </message>
    <message>
        <source>%1 images, one per top-level layer</source>
        <translation>%1 個の画像 (トップレベル レイヤーごとに 1 つ)</translation>
    </message>
    <message>
        <source>Numbered files</source>
        <translation>連番ファイル</translation>
    </message>
    <message>
        <source>Layer names</source>
        <translation>レイヤー名</translation>
    </message>
    <message>
        <source>%1 ... %2</source>
        <translation>%1 ... %2</translation>
    </message>
    <message>
        <source>Frame %1</source>
        <translation>フレーム %1</translation>
    </message>
    <message>
        <source>%1: %2</source>
        <translation>%1: %2</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+2335"/>
        <source>GIF Image</source>
        <translation>GIF 画像</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Aseprite Image</source>
        <translation>Aseprite 画像</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>PCX Image</source>
        <translation>PCX 画像</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Amiga IFF Image</source>
        <translation>Amiga IFF 画像</translation>
    </message>
    <message>
        <source>SVG Image</source>
        <translation>SVG 画像</translation>
    </message>
    <message>
        <source>SVG was imported as flattened raster: %1</source>
        <translation>SVG は統合されたラスターとしてインポートされました: %1</translation>
    </message>
    <message>
        <source>An embedded SVG image could not be decoded (layer %1)</source>
        <translation>埋め込まれた SVG 画像をデコードできませんでした (レイヤー %1)</translation>
    </message>
    <message>
        <source>Camera Raw Image</source>
        <translation>カメラ Raw 画像</translation>
    </message>
    <message>
        <source>HEIF Image</source>
        <translation>HEIF 画像</translation>
    </message>
    <message>
        <source>Affinity Document</source>
        <translation>Affinity ドキュメント</translation>
    </message>
    <message>
        <source>Open failed</source>
        <translation>開けませんでした</translation>
    </message>
    <message>
        <source>Open Microsoft Store</source>
        <translation>Microsoft Store を開く</translation>
    </message>
    <message>
        <source>Develop Raw - %1</source>
        <translation>Raw 現像 - %1</translation>
    </message>
    <message>
        <source>White Balance</source>
        <translation>ホワイトバランス</translation>
    </message>
    <message>
        <source>As Shot</source>
        <translation>撮影時の設定</translation>
    </message>
    <message>
        <source>Daylight</source>
        <translation>昼光</translation>
    </message>
    <message>
        <source>Cloudy</source>
        <translation>曇天</translation>
    </message>
    <message>
        <source>Shade</source>
        <translation>日陰</translation>
    </message>
    <message>
        <source>Tungsten</source>
        <translation>白熱灯</translation>
    </message>
    <message>
        <source>Fluorescent</source>
        <translation>蛍光灯</translation>
    </message>
    <message>
        <source>Flash</source>
        <translation>フラッシュ</translation>
    </message>
    <message>
        <source>Temperature:</source>
        <translation>色温度:</translation>
    </message>
    <message>
        <source>Tint:</source>
        <translation>色かぶり補正:</translation>
    </message>
    <message>
        <source>Exposure</source>
        <translation>露出</translation>
    </message>
    <message>
        <source>Exposure:</source>
        <translation>露出:</translation>
    </message>
    <message>
        <source>%1 EV</source>
        <translation>%1 EV</translation>
    </message>
    <message>
        <source>Highlights:</source>
        <translation>ハイライト:</translation>
    </message>
    <message>
        <source>Tone</source>
        <translation>トーン</translation>
    </message>
    <message>
        <source>Contrast:</source>
        <translation>コントラスト:</translation>
    </message>
    <message>
        <source>Shadows:</source>
        <translation>シャドウ:</translation>
    </message>
    <message>
        <source>Highlight recovery:</source>
        <translation>ハイライト復元:</translation>
    </message>
    <message>
        <source>Saturation:</source>
        <translation>彩度:</translation>
    </message>
    <message>
        <source>Vibrance:</source>
        <translation>自然な彩度:</translation>
    </message>
    <message>
        <source>Clip to white</source>
        <translation>白にクリップ</translation>
    </message>
    <message>
        <source>Unclipped</source>
        <translation>クリップしない</translation>
    </message>
    <message>
        <source>Blend</source>
        <translation>ブレンド</translation>
    </message>
    <message>
        <source>Rebuild detail</source>
        <translation>ディテールを復元</translation>
    </message>
    <message>
        <source>Auto brighten</source>
        <translation>自動明るさ補正</translation>
    </message>
    <message>
        <source>Brightness:</source>
        <translation>明るさ:</translation>
    </message>
    <message>
        <source>Demosaic:</source>
        <translation>デモザイク:</translation>
    </message>
    <message>
        <source>AHD (default)</source>
        <translation>AHD (標準)</translation>
    </message>
    <message>
        <source>DHT (good for high ISO)</source>
        <translation>DHT (高感度向き)</translation>
    </message>
    <message>
        <source>Modified AHD</source>
        <translation>改良型 AHD</translation>
    </message>
    <message>
        <source>DCB</source>
        <translation>DCB</translation>
    </message>
    <message>
        <source>PPG</source>
        <translation>PPG</translation>
    </message>
    <message>
        <source>VNG</source>
        <translation>VNG</translation>
    </message>
    <message>
        <source>Bilinear (fastest)</source>
        <translation>バイリニア (最速)</translation>
    </message>
    <message>
        <source>Denoise:</source>
        <translation>ノイズ除去:</translation>
    </message>
    <message>
        <source>FBDD noise reduction:</source>
        <translation>FBDD ノイズ低減:</translation>
    </message>
    <message>
        <source>Off</source>
        <translation>オフ</translation>
    </message>
    <message>
        <source>Light</source>
        <translation>弱</translation>
    </message>
    <message>
        <source>Full</source>
        <translation>強</translation>
    </message>
    <message>
        <source>Open at half size (faster)</source>
        <translation>1/2 サイズで開く (高速)</translation>
    </message>
    <message>
        <source>Reading raw file...</source>
        <translation>Raw ファイルを読み込み中...</translation>
    </message>
    <message>
        <source>Open</source>
        <translation>開く</translation>
    </message>
    <message>
        <source>%1 K</source>
        <translation>%1 K</translation>
    </message>
    <message>
        <source>ISO %1</source>
        <translation>ISO %1</translation>
    </message>
    <message>
        <source>%1 s</source>
        <translation>%1 秒</translation>
    </message>
    <message>
        <source>1/%1 s</source>
        <translation>1/%1 秒</translation>
    </message>
    <message>
        <source>f/%1</source>
        <translation>f/%1</translation>
    </message>
    <message>
        <source>%1 mm</source>
        <translation>%1 mm</translation>
    </message>
    <message>
        <source>%1 x %2 (%3 MP)</source>
        <translation>%1 x %2 (%3 MP)</translation>
    </message>
    <message>
        <source>Developing preview...</source>
        <translation>プレビューを現像中...</translation>
    </message>
    <message>
        <source>Developing full resolution...</source>
        <translation>フル解像度で現像中...</translation>
    </message>
    <message>
        <source>Developing half size...</source>
        <translation>1/2 サイズで現像中...</translation>
    </message>
    <message>
        <source>Could not read %1</source>
        <translation>%1 を読み込めませんでした</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Targa Image</source>
        <translation>Targa 画像</translation>
    </message>
    <message>
        <location line="+337"/>
        <source>Animated image: imported the first frame only (%1 frames in the file)</source>
        <translation>アニメーション画像: 最初のフレームのみ読み込みました (ファイル内に %1 フレーム)</translation>
    </message>
    <message>
        <location line="-332"/>
        <source>Windows Icon</source>
        <translation>Windows アイコン</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Windows Cursor</source>
        <translation>Windows カーソル</translation>
    </message>
    <message>
        <location filename="../src/ui/image_save_options_dialog.cpp" line="-151"/>
        <source>32-bit with alpha</source>
        <translation>32 ビット (アルファ付き)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>24-bit RGB</source>
        <translation>24 ビット RGB</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>8-bit indexed</source>
        <translation>8 ビット インデックスカラー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>4-bit indexed</source>
        <translation>4 ビット インデックスカラー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>2-bit indexed (compatibility)</source>
        <translation>2 ビット インデックスカラー (互換)</translation>
    </message>
    <message>
        <location line="-10"/>
        <source>Color depth</source>
        <translation>色深度</translation>
    </message>
    <message>
        <location line="+49"/>
        <source>Exact colors</source>
        <translation>正確な色</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reduce colors automatically</source>
        <translation>色数を自動的に減らす</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Indexed colors</source>
        <translation>インデックスカラー</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Use palette file</source>
        <translation>パレットファイルを使用</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Browse...</source>
        <translation>参照...</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Choose Palette</source>
        <translation>パレットを選択</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Palette Files (*.bmp *.pal);;All Files (*.*)</source>
        <translation>パレットファイル (*.bmp *.pal);;すべてのファイル (*.*)</translation>
    </message>
    <message>
        <location filename="../src/ui/blend_mode_ui.cpp" line="+13"/>
        <source>Pass Through</source>
        <translation>通過</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+42"/>
        <location filename="../src/ui/main_window.cpp" line="+9905"/>
        <location line="+550"/>
        <source>Normal</source>
        <translation>通常</translation>
    </message>
    <message>
        <location line="-40"/>
        <source>Multiply</source>
        <translation>乗算</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Screen</source>
        <translation>スクリーン</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Overlay</source>
        <translation>オーバーレイ</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Darken</source>
        <translation>比較(暗)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Lighten</source>
        <translation>比較(明)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Color Dodge</source>
        <translation>覆い焼きカラー</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Color Burn</source>
        <translation>焼き込みカラー</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hard Light</source>
        <translation>ハードライト</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Soft Light</source>
        <translation>ソフトライト</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Difference</source>
        <translation>差の絶対値</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Linear Burn</source>
        <translation>焼き込み(リニア)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Pin Light</source>
        <translation>ピンライト</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Exclusion</source>
        <translation>除外</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Linear Dodge (Add)</source>
        <translation>覆い焼き (リニア) - 加算</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Subtract</source>
        <translation>減算</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Divide</source>
        <translation>除算</translation>
    </message>
    <message>
        <source>Vivid Light</source>
        <translation>ビビッドライト</translation>
    </message>
    <message>
        <source>Linear Light</source>
        <translation>リニアライト</translation>
    </message>
    <message>
        <source>Hard Mix</source>
        <translation>ハードミックス</translation>
    </message>
    <message>
        <source>Darker Color</source>
        <translation>カラー比較 (暗)</translation>
    </message>
    <message>
        <source>Lighter Color</source>
        <translation>カラー比較 (明)</translation>
    </message>
    <message>
        <location line="-14"/>
        <location filename="../src/ui/filter_workflows.cpp" line="+3419"/>
        <location line="+6"/>
        <source>Saturation</source>
        <translation>彩度</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Luminosity</source>
        <translation>輝度</translation>
    </message>
    <message>
        <location filename="../src/ui/brush_presets.cpp" line="+35"/>
        <source>Soft Round</source>
        <translation>ソフト円形</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Hard Round</source>
        <translation>ハード円形</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Pencil</source>
        <translation>鉛筆</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ink</source>
        <translation>インク</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Airbrush</source>
        <translation>エアブラシ</translation>
    </message>
    <message>
        <location filename="../src/ui/filter_workflows.cpp" line="-2783"/>
        <location line="+366"/>
        <location line="+1801"/>
        <location line="+269"/>
        <location filename="../src/ui/layer_style_dialog.cpp" line="+1268"/>
        <source>Preview</source>
        <translation>プレビュー</translation>
    </message>
    <message>
        <location line="-2379"/>
        <location filename="../src/ui/layer_style_dialog.cpp" line="+319"/>
        <source>Invert</source>
        <translation>反転</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Brightness/Contrast</source>
        <translation>明るさ/コントラスト</translation>
    </message>
    <message>
        <location line="+113"/>
        <location line="+112"/>
        <source>Brightness</source>
        <translation>明るさ</translation>
    </message>
    <message>
        <location line="-111"/>
        <location line="+26"/>
        <location line="+122"/>
        <location line="+17"/>
        <source>Contrast</source>
        <translation>コントラスト</translation>
    </message>
    <message>
        <location line="-276"/>
        <location filename="../src/ui/main_window.cpp" line="-11314"/>
        <source>Grayscale</source>
        <translation>グレースケール</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Desaturate</source>
        <translation>彩度を下げる</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Auto Contrast</source>
        <translation>自動コントラスト</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Soft Glow</source>
        <translation>ソフトグロー</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Punchy Color</source>
        <translation>鮮やかなカラー</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Noir</source>
        <translation>ノワール</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Cinematic Matte</source>
        <translation>シネマ風マット</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Vintage Fade</source>
        <translation>ビンテージフェード</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Vintage Sepia</source>
        <translation>ビンテージセピア</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+125"/>
        <location line="+26"/>
        <source>Threshold</source>
        <translation>しきい値</translation>
    </message>
    <message>
        <location line="-148"/>
        <source>Posterize</source>
        <translation>ポスタリゼーション</translation>
    </message>
    <message>
        <source>Threshold Level</source>
        <translation>しきい値レベル</translation>
    </message>
    <message>
        <source>Posterize: %1 levels</source>
        <translation>ポスタリゼーション: %1 階調</translation>
    </message>
    <message>
        <source>Threshold: level %1</source>
        <translation>しきい値: レベル %1</translation>
    </message>
    <message>
        <source>Brightness/Contrast: brightness %1, contrast %2</source>
        <translation>明るさ・コントラスト: 明るさ %1、コントラスト %2</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Box Blur</source>
        <translation>ボックスぼかし</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Sharpen</source>
        <translation>シャープ</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Gaussian Blur</source>
        <translation>ガウスぼかし</translation>
    </message>
    <message>
        <source>High Pass</source>
        <translation>ハイパス</translation>
    </message>
    <message>
        <source>Median</source>
        <translation>中央値</translation>
    </message>
    <message>
        <source>Dust &amp; Scratches</source>
        <translation>ダスト＆スクラッチ</translation>
    </message>
    <message>
        <source>Surface Blur</source>
        <translation>ぼかし（表面）</translation>
    </message>
    <message>
        <source>Lens Blur</source>
        <translation>レンズぼかし</translation>
    </message>
    <message>
        <source>Iris Blur</source>
        <translation>虹彩絞りぼかし</translation>
    </message>
    <message>
        <source>Tilt-Shift Blur</source>
        <translation>チルトシフトぼかし</translation>
    </message>
    <message>
        <source>Blades</source>
        <translation>絞り羽根</translation>
    </message>
    <message>
        <source>Blade Curvature</source>
        <translation>羽根の湾曲</translation>
    </message>
    <message>
        <source>Rotation</source>
        <translation>回転</translation>
    </message>
    <message>
        <source>Iris Width</source>
        <translation>絞りの幅</translation>
    </message>
    <message>
        <source>Iris Height</source>
        <translation>絞りの高さ</translation>
    </message>
    <message>
        <source>Focus</source>
        <translation>フォーカス</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Edge Detect</source>
        <translation>エッジ検出</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Emboss</source>
        <translation>エンボス</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Twirl</source>
        <translation>渦巻き</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Clouds</source>
        <translation>雲模様</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Pixel Mosaic</source>
        <translation>ピクセルモザイク</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Analog Grain</source>
        <translation>アナログ粒子</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Lens Vignette</source>
        <translation>レンズ周辺減光</translation>
    </message>
    <message>
        <location filename="../src/ui/filter_workflows.cpp" line="706"/>
        <source>Other</source>
        <translation>その他</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Adjustments</source>
        <translation>色調補正</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Photo Looks</source>
        <translation>フォトルック</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Blur</source>
        <translation>ぼかし</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Distort</source>
        <translation>変形</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Pixelate</source>
        <translation>ピクセレート</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stylize</source>
        <translation>表現手法</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Render</source>
        <translation>描画</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Center X</source>
        <translation>中心 X</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Center Y</source>
        <translation>中心 Y</translation>
    </message>
    <message>
        <source>Focus Half-Width</source>
        <translation>フォーカス半幅</translation>
    </message>
    <message>
        <source>Transition Width</source>
        <translation>移行幅</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="231"/>
        <source>Filter Gallery</source>
        <translation>フィルターギャラリー</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="370"/>
        <source>Filters</source>
        <translation>フィルター</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Search filters</source>
        <translation>フィルターを検索</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>All</source>
        <translation>すべて</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Favorites</source>
        <translation>お気に入り</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>No filters match this view.</source>
        <translation>この表示条件に一致するフィルターはありません。</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="243"/>
        <source>Looks</source>
        <translation>ルック</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="270"/>
        <source>Live Canvas Preview</source>
        <translation>キャンバスでライブプレビュー</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="313"/>
        <source>Settings</source>
        <translation>設定</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="598"/>
        <source>Saved Looks</source>
        <translation>保存済みルック</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Save Look...</source>
        <translation>ルックを保存...</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Rename...</source>
        <translation>名前を変更...</translation>
    </message>
    <message>
        <location line="+171"/>
        <source>Apply this saved Look</source>
        <translation>この保存済みルックを適用します</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>This Look uses filters or settings that this version of Patchy cannot apply.</source>
        <translation>このルックには、このバージョンの Patchy では適用できないフィルターまたは設定が含まれています。</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="478"/>
        <source>Favorite filter</source>
        <translation>お気に入りフィルター</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add or remove this filter from Favorites</source>
        <translation>このフィルターをお気に入りに追加、またはお気に入りから削除します</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="672"/>
        <source>Applied Effects</source>
        <translation>適用エフェクト</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Duplicate the selected effect</source>
        <translation>選択したエフェクトを複製します</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remove</source>
        <translation>削除</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remove the selected effect</source>
        <translation>選択したエフェクトを削除します</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="525"/>
        <source>Applies permanently to this layer. To keep effects editable, use Filter &gt; Convert for Smart Filters first.</source>
        <translation>このレイヤーに恒久的に適用されます。エフェクトを編集可能なまま保つには、先に フィルター &gt; スマートフィルター用に変換 を使用してください。</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="574"/>
        <source>Applies to this Smart Object as an editable Smart Filter.</source>
        <translation>このスマートオブジェクトに編集可能なスマートフィルターとして適用されます。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>This filter can run as an editable Smart Filter. Use Filter &gt; Convert for Smart Filters on this layer to keep it editable.</source>
        <translation>このフィルターは編集可能なスマートフィルターとして実行できます。編集可能なまま保つには、このレイヤーで フィルター &gt; スマートフィルター用に変換 を使用してください。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>This filter has no Smart Filter mapping. Applying it will rasterize the Smart Object.</source>
        <translation>このフィルターにはスマートフィルターへの対応がありません。適用するとスマートオブジェクトはラスタライズされます。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Applies permanently to the layer pixels.</source>
        <translation>レイヤーのピクセルに恒久的に適用されます。</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="713"/>
        <source>Applies as editable Smart Filters.</source>
        <translation>編集可能なスマートフィルターとして適用されます。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Applying will rasterize the Smart Object (some effects have no Smart Filter mapping).</source>
        <translation>適用するとスマートオブジェクトはラスタライズされます（一部のエフェクトにはスマートフィルターへの対応がありません）。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>This effect cannot be applied as a Smart Filter. Applying the stack will rasterize the Smart Object.</source>
        <translation>このエフェクトはスマートフィルターとして適用できません。適用するとスマートオブジェクトはラスタライズされます。</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="334"/>
        <source>Apply</source>
        <translation>適用</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="340"/>
        <source>Original</source>
        <translation>元画像</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="390"/>
        <source>Rendering preview...</source>
        <translation>プレビューを描画しています...</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="403"/>
        <source>Ready</source>
        <translation>準備完了</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="670"/>
        <source>Choose a filter to adjust its settings.</source>
        <translation>設定を調整するフィルターを選択してください。</translation>
    </message>
    <message>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="1836"/>
        <source>Look name:</source>
        <translation>ルック名:</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Enter a name for the Look.</source>
        <translation>ルック名を入力してください。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>This Look cannot be saved.</source>
        <translation>このルックは保存できません。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The selected Look no longer exists.</source>
        <translation>選択したルックはもう存在しません。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Could not %1 the Look. Check that the Looks folder is writable.</source>
        <translation>ルックを%1できませんでした。ルックフォルダーが書き込み可能か確認してください。</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Unsupported Look</source>
        <translation>未対応のルック</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Save Look</source>
        <translation>ルックを保存</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Save</source>
        <translation>保存</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>save</source>
        <translation>保存</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Rename Look</source>
        <translation>ルック名を変更</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rename</source>
        <translation>名前を変更</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>rename</source>
        <translation>名前変更</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Delete Look</source>
        <translation>ルックを削除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Delete Look &quot;%1&quot;?</source>
        <translation>ルック「%1」を削除しますか？</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>delete</source>
        <translation>削除</translation>
    </message>
    <message>
        <location filename="../src/ui/zoomable_image_preview.cpp" line="136"/>
        <source>Drag the center or radius handle to position the filter. Drag elsewhere to pan; the mouse wheel zooms.</source>
        <translation>中心または半径のハンドルをドラッグしてフィルターの位置を調整します。その他の場所をドラッグすると表示位置を移動でき、マウスホイールでズームします。</translation>
    </message>
    <message>
        <location line="+1943"/>
        <source>Preset:</source>
        <translation>プリセット:</translation>
    </message>
    <message>
        <location line="+250"/>
        <source>Target</source>
        <translation>画像内調整</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Click and drag on the image to adjust the selected channel</source>
        <translation>画像上をクリックしてドラッグし、選択中のチャンネルを調整</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Set the black point from the image</source>
        <translation>画像から黒点を設定</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Neutralize a gray point from the image</source>
        <translation>画像からグレー点を中立化</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Set the white point from the image</source>
        <translation>画像から白点を設定</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Shadows</source>
        <translation>シャドウ</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show pixels clipped to black</source>
        <translation>黒につぶれているピクセルを表示</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Highlights</source>
        <translation>ハイライト</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show pixels clipped to white</source>
        <translation>白飛びしているピクセルを表示</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Both</source>
        <translation>両方</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show shadow and highlight clipping together</source>
        <translation>シャドウとハイライトのクリッピングを同時に表示</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Curves presets</source>
        <translation>トーンカーブのプリセット</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Custom</source>
        <translation>カスタム</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Current custom curve</source>
        <translation>現在のカスタムカーブ</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Load...</source>
        <translation>読み込み...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Load a Photoshop Curves preset</source>
        <translation>Photoshop のトーンカーブプリセットを読み込み</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save...</source>
        <translation>保存...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save the current curves as a Photoshop preset</source>
        <translation>現在のトーンカーブを Photoshop プリセットとして保存</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Before</source>
        <translation>調整前</translation>
    </message>
    <message>
        <location line="+2"/>
        <location filename="../src/ui/visual_filter_gallery_dialog.cpp" line="267"/>
        <source>Hold to compare with the unadjusted image</source>
        <translation>押している間、調整前の画像と比較</translation>
    </message>
    <message>
        <location line="+220"/>
        <location line="+14"/>
        <source>Load Curves Preset</source>
        <translation>トーンカーブプリセットを読み込み</translation>
    </message>
    <message>
        <location line="-13"/>
        <location line="+21"/>
        <source>Photoshop Curves Preset (*.acv)</source>
        <translation>Photoshop トーンカーブプリセット (*.acv)</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>The Curves preset could not be loaded. The file may be damaged or unsupported.</source>
        <translation>トーンカーブプリセットを読み込めませんでした。ファイルが破損しているか、対応していない可能性があります。</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+12"/>
        <source>Save Curves Preset</source>
        <translation>トーンカーブプリセットを保存</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The Curves preset could not be saved.</source>
        <translation>トーンカーブプリセットを保存できませんでした。</translation>
    </message>
    <message>
        <location line="-2586"/>
        <source>Unsharp Mask</source>
        <translation>アンシャープマスク</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Motion Blur</source>
        <translation>モーションぼかし</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Radial Blur</source>
        <translation>放射状ぼかし</translation>
    </message>
    <message>
        <source>Add Noise</source>
        <translation>ノイズを加える</translation>
    </message>
    <message>
        <source>Uniform</source>
        <translation>均等に分布</translation>
    </message>
    <message>
        <source>Draft</source>
        <translation>標準</translation>
    </message>
    <message>
        <source>Good</source>
        <translation>高い</translation>
    </message>
    <message>
        <source>Best</source>
        <translation>最高</translation>
    </message>
    <message>
        <source>Quality</source>
        <translation>画質</translation>
    </message>
    <message>
        <source>Distribution</source>
        <translation>分布方法</translation>
    </message>
    <message>
        <source>Monochromatic</source>
        <translation>グレースケールノイズ</translation>
    </message>
    <message>
        <source> (Amount %1, Spin, %2 quality)</source>
        <translation> (量 %1、回転、画質: %2)</translation>
    </message>
    <message>
        <source> (%1%, %2%3)</source>
        <translation> (%1%、%2%3)</translation>
    </message>
    <message>
        <source>, Monochromatic</source>
        <translation>、グレースケール</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Glowing Edges</source>
        <translation>光彩エッジ</translation>
    </message>
    <message>
        <location line="+6"/>
        <location filename="../src/ui/warp_text_dialog.cpp" line="+37"/>
        <source>Wave</source>
        <translation>波形</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Pinch/Bloat</source>
        <translation>ピンチ/膨張</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Color Halftone</source>
        <translation>カラーハーフトーン</translation>
    </message>
    <message>
        <location line="+25"/>
        <location line="+68"/>
        <location line="+6"/>
        <location line="+23"/>
        <location line="+17"/>
        <location line="+34"/>
        <location line="+34"/>
        <source>Amount</source>
        <translation>量</translation>
    </message>
    <message>
        <location line="-158"/>
        <source>Glow</source>
        <translation>グロー</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+143"/>
        <source>Intensity</source>
        <translation>強度</translation>
    </message>
    <message>
        <location line="-131"/>
        <source>Fade</source>
        <translation>フェード</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+1859"/>
        <location filename="../src/ui/main_window_shared.cpp" line="+19"/>
        <source>Levels</source>
        <translation>レベル補正</translation>
    </message>
    <message>
        <location line="-1854"/>
        <location line="+14"/>
        <location line="+7"/>
        <location line="+49"/>
        <location line="+18"/>
        <source>Radius</source>
        <translation>半径</translation>
    </message>
    <message>
        <location line="-46"/>
        <location line="+84"/>
        <source>Strength</source>
        <translation>強さ</translation>
    </message>
    <message>
        <location line="-99"/>
        <location line="+21"/>
        <location line="+20"/>
        <location filename="../src/ui/layer_style_dialog.cpp" line="-940"/>
        <location line="+634"/>
        <location line="+148"/>
        <location line="+148"/>
        <location line="+174"/>
        <source>Angle</source>
        <translation>角度</translation>
    </message>
    <message>
        <location line="-18"/>
        <location filename="../src/ui/main_window.cpp" line="+2118"/>
        <location line="+560"/>
        <location line="+43"/>
        <source>Height</source>
        <translation>高さ</translation>
    </message>
    <message>
        <location line="+44"/>
        <location filename="../src/ui/layer_style_dialog.cpp" line="-1101"/>
        <location filename="../src/ui/print_dialog.cpp" line="+472"/>
        <source>Scale</source>
        <translation>拡大縮小</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Detail</source>
        <translation>ディテール</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Seed</source>
        <translation>シード</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Block Size</source>
        <translation>ブロックサイズ</translation>
    </message>
    <message>
        <location line="-67"/>
        <source>Samples</source>
        <translation>サンプル数</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Edge Width</source>
        <translation>エッジ幅</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Smoothness</source>
        <translation>滑らかさ</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Amplitude</source>
        <translation>振幅</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Wavelength</source>
        <translation>波長</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Phase</source>
        <translation>位相</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Cell Size</source>
        <translation>セルサイズ</translation>
    </message>
    <message>
        <location line="+578"/>
        <location line="+15"/>
        <source>Generating clouds</source>
        <translation>雲模様を生成しています</translation>
    </message>
    <message>
        <location line="+12"/>
        <location line="+21"/>
        <source>Twisting pixels</source>
        <translation>ピクセルをねじっています</translation>
    </message>
    <message>
        <location line="+29"/>
        <location line="+13"/>
        <source>Embossing pixels</source>
        <translation>ピクセルをエンボス処理しています</translation>
    </message>
    <message>
        <location line="-213"/>
        <location line="+31"/>
        <location line="+11"/>
        <location line="+15"/>
        <location line="+169"/>
        <location line="+2"/>
        <location line="+30"/>
        <location line="+10"/>
        <location line="+12"/>
        <location line="+15"/>
        <location line="+65"/>
        <location line="+2"/>
        <source>Blurring pixels</source>
        <translation>ピクセルをぼかしています</translation>
    </message>
    <message>
        <location line="-131"/>
        <location line="+16"/>
        <location line="+484"/>
        <location line="+20"/>
        <source>Sharpening pixels</source>
        <translation>ピクセルをシャープにしています</translation>
    </message>
    <message>
        <location line="-376"/>
        <location line="+27"/>
        <location line="+385"/>
        <location line="+20"/>
        <source>Detecting edges</source>
        <translation>エッジを検出しています</translation>
    </message>
    <message>
        <location line="+54"/>
        <location line="+17"/>
        <source>Pixelating blocks</source>
        <translation>ブロックをピクセル化しています</translation>
    </message>
    <message>
        <location line="+16"/>
        <location line="+13"/>
        <source>Adding grain</source>
        <translation>粒子を追加しています</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+12"/>
        <source>Applying vignette</source>
        <translation>周辺減光を適用しています</translation>
    </message>
    <message>
        <location line="-626"/>
        <location line="+11"/>
        <location line="+14"/>
        <location line="+15"/>
        <source>Distorting pixels</source>
        <translation>ピクセルを変形しています</translation>
    </message>
    <message>
        <location line="+78"/>
        <location line="+23"/>
        <source>Rendering halftone</source>
        <translation>ハーフトーンを描画しています</translation>
    </message>
    <message>
        <location line="-745"/>
        <location line="+6"/>
        <location line="+42"/>
        <location line="+7"/>
        <location line="+764"/>
        <location line="+12"/>
        <location line="+403"/>
        <location line="+2"/>
        <source>Filtering pixels</source>
        <translation>ピクセルにフィルターを適用しています</translation>
    </message>
    <message>
        <location line="+368"/>
        <source>Channel:</source>
        <translation>チャンネル:</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Input Levels:</source>
        <translation>入力レベル:</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Output Levels:</source>
        <translation>出力レベル:</translation>
    </message>
    <message>
        <location filename="../src/ui/curves_editor.cpp" line="+536"/>
        <location filename="../src/ui/filter_workflows.cpp" line="+28"/>
        <source>Auto</source>
        <translation>自動</translation>
    </message>
    <message>
        <location filename="../src/ui/filter_workflows.cpp" line="+170"/>
        <location filename="../src/ui/main_window_shared.cpp" line="+2"/>
        <source>Curves</source>
        <translation>トーンカーブ</translation>
    </message>
    <message>
        <source>Shadows Output</source>
        <translation type="vanished">シャドウ出力</translation>
    </message>
    <message>
        <source>Midtones Output</source>
        <translation type="vanished">中間調出力</translation>
    </message>
    <message>
        <source>Highlights Output</source>
        <translation type="vanished">ハイライト出力</translation>
    </message>
    <message>
        <location filename="../src/ui/curves_editor.cpp" line="-379"/>
        <source>Curves graph</source>
        <translation>トーンカーブグラフ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Click to add a point. Drag points or use the arrow keys to adjust them.</source>
        <translation>クリックしてポイントを追加します。ポイントをドラッグするか、矢印キーで調整します。</translation>
    </message>
    <message>
        <location line="+356"/>
        <source>Input:</source>
        <translation>入力:</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Output:</source>
        <translation>出力:</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Set the active channel from its histogram</source>
        <translation>ヒストグラムに基づいて選択中のチャンネルを自動調整</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reset</source>
        <translation>リセット</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reset all channels</source>
        <translation>すべてのチャンネルをリセット</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="-2652"/>
        <source>Curves: RGB %1, Red %2, Green %3, Blue %4 points</source>
        <translation>トーンカーブ: RGB %1、赤 %2、緑 %3、青 %4 ポイント</translation>
    </message>
    <message>
        <location filename="../src/ui/filter_workflows.cpp" line="+471"/>
        <location filename="../src/ui/main_window_shared.cpp" line="+2"/>
        <source>Hue/Saturation</source>
        <translation>色相・彩度</translation>
    </message>
    <message>
        <location filename="../src/ui/blend_mode_ui.cpp" line="+4"/>
        <location filename="../src/ui/filter_workflows.cpp" line="-27"/>
        <location line="+6"/>
        <source>Hue</source>
        <translation>色相</translation>
    </message>
    <message>
        <location filename="../src/ui/filter_workflows.cpp" line="-4"/>
        <location line="+6"/>
        <source>Lightness</source>
        <translation>明度</translation>
    </message>
    <message>
        <location line="+68"/>
        <location filename="../src/ui/main_window_shared.cpp" line="+2"/>
        <source>Color Balance</source>
        <translation>カラーバランス</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cyan / Red</source>
        <translation>シアン / レッド</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Magenta / Green</source>
        <translation>マゼンタ / グリーン</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Yellow / Blue</source>
        <translation>イエロー / ブルー</translation>
    </message>
    <message>
        <location filename="../src/ui/splash_dialog.cpp" line="+217"/>
        <source>Patchy</source>
        <translation>Patchy</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Open source photo editing. Free forever, no subscriptions.</source>
        <translation>オープンソースの写真編集。永久無料、サブスクリプション不要。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Version %1</source>
        <translation>バージョン %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Created by Seth A. Robinson</source>
        <translation>作成: Seth A. Robinson</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Code contributions from %1</source>
        <translation>コード貢献者: %1</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>GitHub: %1</source>
        <translation>GitHub: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Seth&apos;s site: %1</source>
        <translation>Seth のサイト: %1</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Settings file:</source>
        <translation>設定ファイル:</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Open Settings Folder</source>
        <translation>設定フォルダーを開く</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Could not open settings folder.</source>
        <translation>設定フォルダーを開けませんでした。</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Patchy is ready.</source>
        <translation>Patchy の準備ができました。</translation>
    </message>
    <message>
        <location line="-202"/>
        <source>Update available: Patchy %1.</source>
        <translation>更新があります: Patchy %1。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update available.</source>
        <translation>更新があります。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Patchy is up to date (%1).</source>
        <translation>Patchy は最新です (%1)。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Patchy is up to date.</source>
        <translation>Patchy は最新です。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update checks are not supported on this platform.</source>
        <translation>このプラットフォームでは更新確認はサポートされていません。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Update check failed: no manifest entry for %1.</source>
        <translation>更新確認に失敗しました: %1 のマニフェスト項目がありません。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update check failed: no manifest entry for this platform.</source>
        <translation>更新確認に失敗しました: このプラットフォームのマニフェスト項目がありません。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update check failed: invalid update manifest.</source>
        <translation>更新確認に失敗しました: 更新マニフェストが無効です。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update check failed: invalid version data.</source>
        <translation>更新確認に失敗しました: バージョンデータが無効です。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update check failed: invalid download URL.</source>
        <translation>更新確認に失敗しました: ダウンロード URL が無効です。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Update check failed: HTTP %1 (%2).</source>
        <translation>更新確認に失敗しました: HTTP %1 (%2)。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Update check failed: HTTP %1.</source>
        <translation>更新確認に失敗しました: HTTP %1。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Update check failed: %1.</source>
        <translation>更新確認に失敗しました: %1。</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+2"/>
        <source>Update check failed.</source>
        <translation>更新確認に失敗しました。</translation>
    </message>
    <message>
        <location line="+189"/>
        <source>Update checks are disabled.</source>
        <translation>更新確認は無効です。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Checking for updates...</source>
        <translation>更新を確認しています...</translation>
    </message>
    <message>
        <location filename="../src/ui/dialog_utils.cpp" line="+663"/>
        <location filename="../src/ui/splash_dialog.cpp" line="-17"/>
        <source>Close</source>
        <translation>閉じる</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Apply and Close</source>
        <translation>適用して閉じる</translation>
    </message>
    <message>
        <location filename="../src/ui/compatibility_report.cpp" line="+47"/>
        <source>%1 is a smart object linked to an external file; Patchy preserves it and can update it from disk when the source file is available.</source>
        <translation>%1 は外部ファイルにリンクされたスマートオブジェクトです。Patchy はこれを保持し、ソースファイルが利用可能な場合はディスクから更新できます。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>%1 is a smart object with Smart Filters; Patchy preserves them and shows Photoshop&apos;s preview (rasterize the layer to edit it here).</source>
        <translation>%1 はスマートフィルターを持つスマートオブジェクトです。Patchy はそれらを保持し、Photoshop のプレビューを表示します（ここで編集するにはレイヤーをラスタライズしてください）。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>%1 is a smart object with a warp or perspective transform; Patchy preserves it and shows Photoshop&apos;s preview (rasterize the layer to edit it here).</source>
        <translation>%1 はワープまたは遠近変形を持つスマートオブジェクトです。Patchy はそれを保持し、Photoshop のプレビューを表示します（ここで編集するにはレイヤーをラスタライズしてください）。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1 is a smart object Patchy can only preserve, not edit (%2).</source>
        <translation>%1 は Patchy では保持のみ可能で編集できないスマートオブジェクトです (%2)。</translation>
    </message>
    <message>
        <source>%1 contains a Satin effect that Patchy preserves for PSD round-trip but does not render or edit.</source>
        <translation type="vanished">%1 にはサテン効果が含まれています。Patchy は PSD 往復用に保持しますが、描画や編集は行いません。</translation>
    </message>
    <message>
        <source>%1 contains a Satin effect with a custom Photoshop contour. Patchy preserves it until layer styles are edited, then uses the Linear contour.</source>
        <translation type="vanished">%1 には Photoshop のカスタム輪郭を使用したサテン効果が含まれています。レイヤースタイルを編集するまでは保持されますが、編集後はリニア輪郭が使用されます。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>%1 is a group with layer effects. Patchy preserves them for PSD round-trip but does not render group layer effects yet.</source>
        <translation>%1 はレイヤー効果を含むグループです。Patchy は PSD 往復用に保持しますが、グループのレイヤー効果はまだ描画しません。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>%1 contains Photoshop Satin contour settings that Patchy cannot render or edit (a custom curve or anti-aliasing). Patchy preserves them until layer styles are edited, then uses the non-anti-aliased Linear contour.</source>
        <translation>%1 には、Patchy で描画または編集できない Photoshop のサテン輪郭設定（カスタムカーブまたはアンチエイリアス）が含まれています。レイヤースタイルを編集するまでは保持されますが、編集後はアンチエイリアスなしのリニア輪郭が使用されます。</translation>
    </message>
    <message>
        <source>Pattern &quot;%1&quot; is not embedded in this document, so the effect that references it cannot render until you choose another pattern.</source>
        <translation>パターン「%1」はこのドキュメントに埋め込まれていないため、別のパターンを選択するまでこのパターンを参照する効果は描画できません。</translation>
    </message>
    <message>
        <source>Contour</source>
        <translation>輪郭</translation>
    </message>
    <message>
        <source>Texture</source>
        <translation>テクスチャ</translation>
    </message>
    <message>
        <source>Gloss Contour</source>
        <translation>光沢輪郭</translation>
    </message>
    <message>
        <source>Anti-aliased</source>
        <translation>アンチエイリアス</translation>
    </message>
    <message>
        <source>Range</source>
        <translation>適用範囲</translation>
    </message>
    <message>
        <source>Pattern</source>
        <translation>パターン</translation>
    </message>
    <message>
        <source>Link with Layer</source>
        <translation>レイヤーにリンク</translation>
    </message>
    <message>
        <source>Snap to Origin</source>
        <translation>原点にスナップ</translation>
    </message>
    <message>
        <source>Add Pattern Overlay</source>
        <translation>パターンオーバーレイを追加</translation>
    </message>
    <message>
        <source>%1 (missing)</source>
        <translation>%1（見つかりません）</translation>
    </message>
    <message>
        <source>Anchor the pattern to the layer so it follows when the layer moves</source>
        <translation>パターンをレイヤーに固定し、レイヤーの移動に追従させます</translation>
    </message>
    <message>
        <source>Checkerboard</source>
        <translation>チェッカーボード</translation>
    </message>
    <message>
        <source>Diagonal Stripes</source>
        <translation>斜めストライプ</translation>
    </message>
    <message>
        <source>Polka Dots</source>
        <translation>水玉</translation>
    </message>
    <message>
        <source>Grid</source>
        <translation>グリッド</translation>
    </message>
    <message>
        <source>Fine Grain</source>
        <translation>細かい粒子</translation>
    </message>
    <message>
        <source>Canvas Weave</source>
        <translation>カンバス織り</translation>
    </message>
    <message>
        <source>Wood Grain</source>
        <translation>木目</translation>
    </message>
    <message>
        <source>Brushed Metal</source>
        <translation>ヘアライン金属</translation>
    </message>
    <message>
        <source>Bumps</source>
        <translation>凹凸</translation>
    </message>
    <message>
        <source>Bricks</source>
        <translation>レンガ</translation>
    </message>
    <message>
        <source>Scales</source>
        <translation>うろこ</translation>
    </message>
    <message>
        <source>Basketweave</source>
        <translation>バスケット編み</translation>
    </message>
    <message>
        <source>Cone</source>
        <translation>円錐</translation>
    </message>
    <message>
        <source>Cone - Inverted</source>
        <translation>円錐 - 反転</translation>
    </message>
    <message>
        <source>Cove - Deep</source>
        <translation>くぼみ - 深</translation>
    </message>
    <message>
        <source>Cove - Shallow</source>
        <translation>くぼみ - 浅</translation>
    </message>
    <message>
        <source>Gaussian</source>
        <translation>ガウス</translation>
    </message>
    <message>
        <source>Half Round</source>
        <translation>半円</translation>
    </message>
    <message>
        <source>Ring</source>
        <translation>リング</translation>
    </message>
    <message>
        <source>Ring - Double</source>
        <translation>リング - 二重</translation>
    </message>
    <message>
        <source>Rolling Slope - Descending</source>
        <translation>なだらかな下り坂</translation>
    </message>
    <message>
        <source>Rounded Steps</source>
        <translation>丸い階段</translation>
    </message>
    <message>
        <source>Sawtooth</source>
        <translation>のこぎり波</translation>
    </message>
    <message>
        <source>%1 contains non-default Photoshop Blend If data that Patchy preserves for PSD round-trip but does not render or edit.</source>
        <translation type="vanished">%1 には既定値以外の Photoshop「ブレンド条件 (Blend If)」データが含まれています。Patchy は PSD 往復用に保持しますが、描画や編集は行いません。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>%1 contains Photoshop Blend If data for an unsupported color mode or payload shape. Patchy preserves it for PSD round-trip but does not render or edit it.</source>
        <translation>%1 には、未対応のカラーモードまたはペイロード形式の Photoshop「ブレンド条件 (Blend If)」データが含まれています。Patchy は PSD 往復用に保持しますが、描画や編集は行いません。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>%1 contains Blend If data on a Photoshop group-boundary record. Patchy preserves that boundary data but does not render or edit it.</source>
        <translation>%1 には Photoshop のグループ境界レコードに「ブレンド条件 (Blend If)」データが含まれています。Patchy はその境界データを保持しますが、描画や編集は行いません。</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+53"/>
        <source>%1 preserves %2 unknown PSD layer block(s).</source>
        <translation>%1 は不明な PSD レイヤーブロック %2 個を保持しています。</translation>
    </message>
    <message>
        <location line="-36"/>
        <source>%1: extracted editable PSD text from %2, but Patchy generated a placeholder raster preview because the PSD text pixels were not visible.</source>
        <translation>%1: %2 から編集可能な PSD テキストを抽出しましたが、PSD テキストのピクセルが表示されていなかったため、Patchy は仮のラスタープレビューを生成しました。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>text data</source>
        <translation>テキストデータ</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1: extracted editable PSD text from %2 and preserved the original PSD text block; the current pixels use the PSD raster preview until the text is edited.</source>
        <translation>%1: %2 から編集可能な PSD テキストを抽出し、元の PSD テキストブロックを保持しました。テキストを編集するまでは現在のピクセルに PSD のラスタープレビューを使用します。</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>%1 is a Patchy-native adjustment layer; it round-trips in Patchy PSDs but may appear as an unsupported adjustment in other editors.</source>
        <translation>%1 は Patchy ネイティブの調整レイヤーです。Patchy PSD では往復できますが、他のエディターでは未対応の調整として表示される場合があります。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>%1 uses an unsupported layer kind and may not export as editable PSD data.</source>
        <translation>%1 は未対応のレイヤー種別を使用しているため、編集可能な PSD データとして書き出せない場合があります。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>%1 uses a pixel format that can render but is not fully editable in this build.</source>
        <translation>%1 は描画可能ですが、このビルドでは完全には編集できないピクセル形式を使用しています。</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>The source color mode is CMYK; Patchy converted the pixels to RGB/RGBA for editing and will export RGB PSD data from this document.</source>
        <translation>元のカラーモードは CMYK です。Patchy は編集用にピクセルを RGB/RGBA に変換しました。このドキュメントからは RGB PSD データを書き出します。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The source color mode is %1; Patchy currently edits through RGB/RGBA workflows.</source>
        <translation>元のカラーモードは %1 です。Patchy は現在 RGB/RGBA ワークフローで編集します。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The document preserves %1 unknown PSD image resource(s).</source>
        <translation>ドキュメントは不明な PSD 画像リソース %1 個を保持しています。</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>The document embeds %1 smart object source file(s) (%2 MB); they round-trip byte-for-byte.</source>
        <translation>このドキュメントは %1 個のスマートオブジェクトソースファイル (%2 MB) を埋め込んでいます。バイト単位でそのまま保持されます。</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>PSD Compatibility Report</source>
        <translation>PSD 互換性レポート</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Compatibility: %1</source>
        <translation>互換性: %1</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>document</source>
        <translation>ドキュメント</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Patchy preserved the editable data it understands and flagged areas that may differ from Photoshop or other PSD editors.</source>
        <translation>Patchy は認識できる編集可能データを保持し、Photoshop や他の PSD エディターと異なる可能性のある領域を示しました。</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp" line="-286"/>
        <source>Layer Style</source>
        <translation>レイヤースタイル</translation>
    </message>
    <message>
        <location filename="../src/ui/brush_tip_manager_dialog.cpp" line="-14"/>
        <location line="+352"/>
        <location filename="../src/ui/layer_style_dialog.cpp" line="+4"/>
        <source>Name:</source>
        <translation>名前:</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp" line="+668"/>
        <location line="+1497"/>
        <source>Blending Options</source>
        <translation>描画オプション</translation>
    </message>
    <message>
        <location line="-1487"/>
        <source>Show Effects</source>
        <translation>効果を表示</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Layer Mask Hides Effects</source>
        <translation>レイヤーマスクで効果を隠す</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Clip drop shadows, glows, and strokes with the layer mask instead of only shaping them</source>
        <translation>ドロップシャドウ・光彩・境界線をレイヤーマスクの形に合わせるだけでなく、マスクで切り抜きます</translation>
    </message>
    <message>
        <location line="+89"/>
        <source>Decrease %1</source>
        <translation>%1 を減らす</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Increase %1</source>
        <translation>%1 を増やす</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Use Page Up or Page Down to select a handle, arrow keys to move it, and Alt/Option-drag to split a joined handle.</source>
        <translation>Page Up または Page Down でハンドルを選択し、矢印キーで移動します。結合されたハンドルを分割するには Alt/Option キーを押しながらドラッグします。</translation>
    </message>
    <message>
        <location line="+88"/>
        <location line="+1261"/>
        <source>Bevel &amp; Emboss</source>
        <translation>ベベルとエンボス</translation>
    </message>
    <message>
        <source>Inner Bevel</source>
        <translation>ベベル（内側）</translation>
    </message>
    <message>
        <source>Outer Bevel</source>
        <translation>ベベル（外側）</translation>
    </message>
    <message>
        <source>Pillow Emboss</source>
        <translation>ピローエンボス</translation>
    </message>
    <message>
        <source>Stroke Emboss</source>
        <translation>境界線のエンボス</translation>
    </message>
    <message>
        <source>Technique</source>
        <translation>テクニック</translation>
    </message>
    <message>
        <source>Chisel Hard</source>
        <translation>シゼルハード</translation>
    </message>
    <message>
        <source>Chisel Soft</source>
        <translation>シゼルソフト</translation>
    </message>
    <message>
        <source>Soften</source>
        <translation>ソフト</translation>
    </message>
    <message>
        <location line="-1191"/>
        <location line="+1204"/>
        <location filename="../src/ui/main_window.cpp" line="-22"/>
        <source>Stroke</source>
        <translation>境界線</translation>
    </message>
    <message>
        <location line="-927"/>
        <location line="+933"/>
        <location filename="../src/ui/main_window.cpp" line="-9"/>
        <source>Color Overlay</source>
        <translation>カラーオーバーレイ</translation>
    </message>
    <message>
        <location line="-882"/>
        <location line="+884"/>
        <source>Gradient Overlay</source>
        <translation>グラデーションオーバーレイ</translation>
    </message>
    <message>
        <location line="-867"/>
        <location line="+869"/>
        <location filename="../src/ui/main_window.cpp" line="-9"/>
        <source>Outer Glow</source>
        <translation>光彩(外側)</translation>
    </message>
    <message>
        <location line="-817"/>
        <location line="+819"/>
        <location filename="../src/ui/main_window.cpp" line="-6"/>
        <source>Drop Shadow</source>
        <translation>ドロップシャドウ</translation>
    </message>
    <message>
        <location line="-1143"/>
        <location line="+1132"/>
        <location filename="../src/ui/main_window.cpp" line="+3"/>
        <source>Inner Shadow</source>
        <translation>シャドウ（内側）</translation>
    </message>
    <message>
        <location line="-1056"/>
        <location line="+1058"/>
        <location filename="../src/ui/main_window.cpp" line="+6"/>
        <source>Inner Glow</source>
        <translation>光彩（内側）</translation>
    </message>
    <message>
        <location line="-985"/>
        <location line="+987"/>
        <location filename="../src/ui/main_window.cpp" line="+3"/>
        <source>Satin</source>
        <translation>サテン</translation>
    </message>
    <message>
        <location line="-2165"/>
        <location filename="../src/ui/main_window.cpp" line="+9"/>
        <location line="+1036"/>
        <source>Pattern Overlay</source>
        <translation>パターンオーバーレイ</translation>
    </message>
    <message>
        <location line="+658"/>
        <location line="+260"/>
        <location line="+20"/>
        <location line="+37"/>
        <location line="+64"/>
        <location line="+75"/>
        <location line="+73"/>
        <location line="+56"/>
        <location line="+50"/>
        <location line="+17"/>
        <location line="+52"/>
        <source>Blend Mode</source>
        <translation>描画モード</translation>
    </message>
    <message>
        <location line="-1155"/>
        <location line="+452"/>
        <location line="+261"/>
        <location line="+20"/>
        <location line="+36"/>
        <location line="+65"/>
        <location line="+75"/>
        <location line="+73"/>
        <location line="+56"/>
        <location line="+50"/>
        <location line="+17"/>
        <location line="+51"/>
        <source>Opacity</source>
        <translation>不透明度</translation>
    </message>
    <message>
        <source>Custom Photoshop Satin contours are preserved until you edit layer styles. Patchy previews and saves edited Satin with the Linear contour.</source>
        <translation type="vanished">Photoshop のサテンのカスタム輪郭は、レイヤースタイルを編集するまでは保持されます。Patchy でのプレビューおよび編集後の保存にはリニア輪郭が使用されます。</translation>
    </message>
    <message>
        <location line="-1136"/>
        <source>Midpoint</source>
        <translation>中間点</translation>
    </message>
    <message>
        <location line="+664"/>
        <location line="+70"/>
        <location line="+91"/>
        <location line="+67"/>
        <location line="+81"/>
        <location line="+115"/>
        <location line="+58"/>
        <source>Size</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <location line="-479"/>
        <source>Depth</source>
        <translation>深さ</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Altitude</source>
        <translation>高度</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Up</source>
        <translation>上</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Down</source>
        <translation>下</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Highlight</source>
        <translation>ハイライト</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Shadow</source>
        <translation>シャドウ</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Direction</source>
        <translation>方向</translation>
    </message>
    <message>
        <source>Highlight Opacity</source>
        <translation type="vanished">ハイライトの不透明度</translation>
    </message>
    <message>
        <source>Shadow Opacity</source>
        <translation type="vanished">シャドウの不透明度</translation>
    </message>
    <message>
        <location line="+83"/>
        <location line="+73"/>
        <location line="+73"/>
        <location line="+74"/>
        <location line="+42"/>
        <location line="+74"/>
        <location line="+57"/>
        <source>R</source>
        <translation>R</translation>
    </message>
    <message>
        <location line="-390"/>
        <location line="+73"/>
        <location line="+73"/>
        <location line="+73"/>
        <location line="+43"/>
        <location line="+74"/>
        <location line="+57"/>
        <source>G</source>
        <translation>G</translation>
    </message>
    <message>
        <location line="-390"/>
        <location line="+73"/>
        <location line="+73"/>
        <location line="+72"/>
        <location line="+44"/>
        <location line="+74"/>
        <location line="+57"/>
        <source>B</source>
        <translation>B</translation>
    </message>
    <message>
        <location line="-376"/>
        <location line="+75"/>
        <location line="+73"/>
        <location line="+70"/>
        <location line="+51"/>
        <location line="+69"/>
        <location line="+55"/>
        <source>Color RGB</source>
        <translation>カラー RGB</translation>
    </message>
    <message>
        <location line="-441"/>
        <source>Outside</source>
        <translation>外側</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Inside</source>
        <translation>内側</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+161"/>
        <source>Center</source>
        <translation>中央</translation>
    </message>
    <message>
        <location line="-159"/>
        <source>Position</source>
        <translation>位置</translation>
    </message>
    <message>
        <location line="-764"/>
        <location line="+720"/>
        <location line="+20"/>
        <location line="+60"/>
        <location line="+73"/>
        <location line="+73"/>
        <location line="+72"/>
        <location line="+44"/>
        <location line="+1"/>
        <location line="+73"/>
        <location line="+57"/>
        <location filename="../src/ui/main_window.cpp" line="-2079"/>
        <source>Choose Color...</source>
        <translation>色を選択...</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="-15"/>
        <source>Location %</source>
        <translation>位置 %</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp" line="-1157"/>
        <location filename="../src/ui/main_window.cpp" line="+17"/>
        <source>Add Stop</source>
        <translation>ストップを追加</translation>
    </message>
    <message>
        <location line="+2"/>
        <location filename="../src/ui/main_window.cpp" line="+2"/>
        <source>Remove Stop</source>
        <translation>ストップを削除</translation>
    </message>
    <message>
        <location line="+1074"/>
        <location line="+58"/>
        <source>Spread</source>
        <translation>スプレッド</translation>
    </message>
    <message>
        <location line="-320"/>
        <location line="+67"/>
        <source>Choke</source>
        <translation>チョーク</translation>
    </message>
    <message>
        <location filename="../src/ui/filter_workflows.cpp" line="-2603"/>
        <location filename="../src/ui/layer_style_dialog.cpp" line="-74"/>
        <location line="+148"/>
        <location line="+173"/>
        <source>Distance</source>
        <translation>距離</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp" line="-242"/>
        <source>Edge</source>
        <translation>エッジ</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Source</source>
        <translation>ソース</translation>
    </message>
    <message>
        <location line="-27"/>
        <location line="+73"/>
        <source>Instances</source>
        <translation>インスタンス</translation>
    </message>
    <message>
        <location line="-1075"/>
        <source>Remove Selected Instance</source>
        <translation>選択したインスタンスを削除</translation>
    </message>
    <message>
        <location line="+1924"/>
        <source>Add Stroke</source>
        <translation>境界線を追加</translation>
    </message>
    <message>
        <location line="-931"/>
        <location line="+933"/>
        <source>Add Inner Shadow</source>
        <translation>シャドウ（内側）を追加</translation>
    </message>
    <message>
        <location line="-2013"/>
        <source>Photoshop Satin custom contours and contour anti-aliasing are preserved until you edit layer styles. Patchy previews and saves edited Satin with the non-anti-aliased Linear contour.</source>
        <translation>Photoshop のサテンのカスタム輪郭と輪郭アンチエイリアスは、レイヤースタイルを編集するまでは保持されます。Patchy でのプレビューおよび編集後の保存には、アンチエイリアスなしのリニア輪郭が使用されます。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Layer effects on groups are preserved for PSD round-trip but are not rendered yet. Satin controls remain editable, but Preview cannot show the group result.</source>
        <translation>グループのレイヤー効果は PSD 往復用に保持されますが、まだ描画されません。サテンの設定は編集できますが、プレビューにグループの結果は表示されません。</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>This layer contains Photoshop Blend If data for an unsupported color mode or payload shape. Patchy preserves it unchanged and does not preview it unless you replace it.</source>
        <translation>このレイヤーには、未対応のカラーモードまたはペイロード形式の Photoshop「ブレンド条件 (Blend If)」データが含まれています。置き換えない限り、Patchy はデータを変更せずに保持し、プレビューしません。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Replace with Editable Defaults</source>
        <translation>編集可能な既定値に置き換え</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>This folder&apos;s closing PSD record contains separate Blend If data. Patchy preserves that boundary data unchanged; the controls below edit only the visible folder record.</source>
        <translation>このフォルダーの末尾にある PSD レコードには、別の「ブレンド条件 (Blend If)」データが含まれています。Patchy はその境界データを変更せずに保持します。以下のコントロールで編集されるのは表示側のフォルダーレコードのみです。</translation>
    </message>
    <message>
        <location line="+624"/>
        <source>Blend If</source>
        <translation>ブレンド条件 (Blend If)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Blend If:</source>
        <translation>ブレンド条件 (Blend If):</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Reset Channel</source>
        <translation>チャンネルをリセット</translation>
    </message>
    <message>
        <location line="+91"/>
        <source>%1 Blend If range</source>
        <translation>%1 の「ブレンド条件 (Blend If)」範囲</translation>
    </message>
    <message>
        <source>Use Page Up or Page Down to select a handle, arrow keys to move it, and Alt-drag to split a joined handle.</source>
        <translation type="vanished">Page Up または Page Down でハンドルを選択し、矢印キーで移動します。結合されたハンドルを分割するには Alt キーを押しながらドラッグします。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>%1 black transition start</source>
        <translation>%1 の黒側のトランジション開始</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 black transition end</source>
        <translation>%1 の黒側のトランジション終了</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+13"/>
        <source>to</source>
        <translation>～</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>%1 white transition start</source>
        <translation>%1 の白側のトランジション開始</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 white transition end</source>
        <translation>%1 の白側のトランジション終了</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>This Layer</source>
        <translation>このレイヤー</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Underlying Layer</source>
        <translation>下のレイヤー</translation>
    </message>
    <message>
        <location line="+260"/>
        <source>Remove Inner Shadow</source>
        <translation>シャドウ（内側）を削除</translation>
    </message>
    <message>
        <location line="+69"/>
        <location line="+862"/>
        <source>Add Inner Glow</source>
        <translation>光彩（内側）を追加</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add Satin</source>
        <translation>サテンを追加</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add Color Overlay</source>
        <translation>カラーオーバーレイを追加</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add Gradient Overlay</source>
        <translation>グラデーションオーバーレイを追加</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add Outer Glow</source>
        <translation>光彩（外側）を追加</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add Drop Shadow</source>
        <translation>ドロップシャドウを追加</translation>
    </message>
    <message>
        <location line="-868"/>
        <source>Remove Inner Glow</source>
        <translation>光彩（内側）を削除</translation>
    </message>
    <message>
        <location line="+1222"/>
        <source>Choose Color Overlay Color</source>
        <translation>カラーオーバーレイの色を選択</translation>
    </message>
    <message>
        <location line="-41"/>
        <source>Choose Stroke Color</source>
        <translation>境界線の色を選択</translation>
    </message>
    <message>
        <location line="-86"/>
        <source>The preserved Photoshop Blend If payload will be replaced with editable RGB defaults when you choose OK.</source>
        <translation>保持されている Photoshop「ブレンド条件 (Blend If)」ペイロードは、OK を選択すると編集可能な RGB の既定値に置き換えられます。</translation>
    </message>
    <message>
        <location line="+119"/>
        <source>Choose Highlight Color</source>
        <translation>ハイライトの色を選択</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Choose Shadow Color</source>
        <translation>シャドウの色を選択</translation>
    </message>
    <message>
        <location line="-1368"/>
        <location line="+279"/>
        <location filename="../src/ui/main_window.cpp" line="+151"/>
        <source>Choose Gradient Stop Color</source>
        <translation>グラデーションストップの色を選択</translation>
    </message>
    <message>
        <location line="-1108"/>
        <source>Location</source>
        <translation>位置</translation>
    </message>
    <message>
        <location line="+59"/>
        <source>Reverse</source>
        <translation>反転</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Style</source>
        <translation>スタイル</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Linear</source>
        <translation>線形</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Radial</source>
        <translation>円形</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Angle</source>
        <comment>gradient style</comment>
        <translation>円錐形</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reflected</source>
        <translation>反射形</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Diamond</source>
        <translation>菱形</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="-177"/>
        <source>Edit Gradient Stops</source>
        <translation>グラデーションストップを編集</translation>
    </message>
    <message>
        <location filename="../src/ui/blend_mode_ui.cpp" line="+2"/>
        <location filename="../src/ui/layer_style_dialog.cpp" line="-63"/>
        <location line="+729"/>
        <location line="+20"/>
        <location line="+35"/>
        <location filename="../src/ui/main_window.cpp" line="+7"/>
        <source>Color</source>
        <translation>色</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+0"/>
        <source>Alpha %</source>
        <translation>アルファ %</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Reset to FG/BG</source>
        <translation>前景/背景にリセット</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp" line="+1424"/>
        <source>Choose Outer Glow Color</source>
        <translation>光彩（外側）の色を選択</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Choose Inner Glow Color</source>
        <translation>光彩（内側）の色を選択</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Choose Satin Color</source>
        <translation>サテンの色を選択</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Choose Drop Shadow Color</source>
        <translation>ドロップシャドウの色を選択</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Choose Inner Shadow Color</source>
        <translation>シャドウ（内側）の色を選択</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+393"/>
        <source>Move</source>
        <translation>移動</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Marquee</source>
        <translation>長方形選択</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Elliptical Marquee</source>
        <translation>楕円形選択</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Lasso</source>
        <translation>投げ縄</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Magnetic Lasso</source>
        <translation>マグネット投げ縄</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Magic Wand</source>
        <translation>自動選択</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Quick Select</source>
        <translation>クイック選択</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Brush</source>
        <translation>ブラシ</translation>
    </message>
    <message>
        <source>Mixer Brush</source>
        <translation>ミキサーブラシ</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Clone Stamp</source>
        <translation>クローンスタンプ</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_shared.cpp" line="286"/>
        <source>Healing Brush</source>
        <translation>修復ブラシ</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Smudge</source>
        <translation>指先</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Eraser</source>
        <translation>消しゴム</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp" line="-2269"/>
        <location line="+798"/>
        <location line="+32"/>
        <location filename="../src/ui/main_window.cpp" line="+2"/>
        <source>Gradient</source>
        <translation>グラデーション</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+2"/>
        <source>Line</source>
        <translation>直線</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rectangle</source>
        <translation>長方形</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ellipse</source>
        <translation>楕円</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp" line="-30"/>
        <location filename="../src/ui/main_window.cpp" line="+2"/>
        <source>Fill</source>
        <translation>塗りつぶし</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+2"/>
        <source>Eyedropper</source>
        <translation>スポイト</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Type</source>
        <translation>文字</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Pan</source>
        <translation>手のひら</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Zoom</source>
        <translation>ズーム</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Tool</source>
        <translation>ツール</translation>
    </message>
    <message>
        <location filename="../src/ui/brush_tip_manager_dialog.cpp" line="-237"/>
        <location filename="../src/ui/hotkey_registry.cpp" line="+60"/>
        <source>%1 (%2)</source>
        <translation>%1 (%2)</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+103"/>
        <source>Layer visible. Click to hide.</source>
        <translation>レイヤーは表示中です。クリックで非表示にします。</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Layer hidden. Click to show.</source>
        <translation>レイヤーは非表示です。クリックで表示します。</translation>
    </message>
    <message>
        <location line="+1170"/>
        <source>Layer styles. Click to edit them.</source>
        <translation>レイヤースタイル。クリックで編集します。</translation>
    </message>
    <message>
        <location line="-49"/>
        <source>mask</source>
        <translation>マスク</translation>
    </message>
    <message>
        <location line="-844"/>
        <location line="+16"/>
        <source>Collapse panel</source>
        <translation>パネルを折りたたむ</translation>
    </message>
    <message>
        <location line="-277"/>
        <source>Transparent pixels locked</source>
        <translation>透明ピクセルをロックしました</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Layer locked</source>
        <translation>レイヤーをロックしました</translation>
    </message>
    <message>
        <location line="+255"/>
        <location line="+16"/>
        <source>Expand panel</source>
        <translation>パネルを展開</translation>
    </message>
    <message>
        <location line="+123"/>
        <source>adjustment</source>
        <translation>調整</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 adjustment</source>
        <translation>%1 調整</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Pixel Layer</source>
        <translation>ピクセルレイヤー</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Folder</source>
        <translation>フォルダー</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+613"/>
        <source>Adjustment Layer</source>
        <translation>調整レイヤー</translation>
    </message>
    <message>
        <location line="-611"/>
        <source>Text Layer</source>
        <translation>テキストレイヤー</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Vector Layer</source>
        <translation>ベクターレイヤー</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Smart Object</source>
        <translation>スマートオブジェクト</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Layer</source>
        <translation>レイヤー</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>8-bit</source>
        <translation>8 ビット</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>16-bit</source>
        <translation>16 ビット</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>32-bit float</source>
        <translation>32 ビット浮動小数点</translation>
    </message>
    <message>
        <location filename="../src/ui/curves_editor.cpp" line="-39"/>
        <location filename="../src/ui/filter_workflows.cpp" line="+1845"/>
        <location filename="../src/ui/main_window.cpp" line="+7"/>
        <source>RGB</source>
        <translation>RGB</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/ui/filter_workflows.cpp" line="+1"/>
        <location filename="../src/ui/layer_style_dialog.cpp" line="-282"/>
        <source>Red</source>
        <translation>赤</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/ui/filter_workflows.cpp" line="+1"/>
        <location filename="../src/ui/layer_style_dialog.cpp" line="+1"/>
        <source>Green</source>
        <translation>緑</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/ui/filter_workflows.cpp" line="+1"/>
        <location filename="../src/ui/layer_style_dialog.cpp" line="+1"/>
        <source>Blue</source>
        <translation>青</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+6"/>
        <source>CMYK</source>
        <translation>CMYK</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Lab</source>
        <translation>Lab</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1 %2, %3 channels</source>
        <translation>%1 %2、%3 チャンネル</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>empty</source>
        <translation>空</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 x %2 at %3, %4</source>
        <translation>%1 x %2、位置 %3, %4</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Gradient Fill</source>
        <translation>グラデーション塗りつぶし</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Bevel</source>
        <translation>ベベル</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>none</source>
        <translation>なし</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+92"/>
        <source>, </source>
        <translation>、</translation>
    </message>
    <message>
        <location line="-86"/>
        <source>No editable adjustment settings</source>
        <translation>編集可能な調整設定はありません</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Levels: black %1, white %2, gamma %3%, output %4-%5</source>
        <translation>レベル補正: 黒 %1、白 %2、ガンマ %3%、出力 %4-%5</translation>
    </message>
    <message>
        <source>Curves: shadows %1, midtones %2, highlights %3</source>
        <translation type="vanished">トーンカーブ: シャドウ %1、中間調 %2、ハイライト %3</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Hue/Saturation: hue %1, saturation %2, lightness %3</source>
        <translation>色相・彩度: 色相 %1、彩度 %2、明度 %3</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Color Balance: C/R %1, M/G %2, Y/B %3</source>
        <translation>カラーバランス: C/R %1、M/G %2、Y/B %3</translation>
    </message>
    <message>
        <location line="+5"/>
        <location filename="../src/ui/main_window_shared.cpp" line="+2"/>
        <source>Adjustment</source>
        <translation>調整</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>No editable text metadata</source>
        <translation>編集可能なテキストメタデータはありません</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Text</source>
        <translation>テキスト</translation>
    </message>
    <message>
        <location filename="../src/ui/curves_presets.cpp" line="+101"/>
        <location filename="../src/ui/filter_workflows.cpp" line="-12"/>
        <location filename="../src/ui/main_window.cpp" line="+1"/>
        <source>Default</source>
        <translation>既定</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Medium Contrast</source>
        <translation>中程度のコントラスト</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Strong Contrast</source>
        <translation>強いコントラスト</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Lift Shadows</source>
        <translation>シャドウを明るく</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Recover Highlights</source>
        <translation>ハイライトを抑える</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Matte</source>
        <translation>マット</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Warm</source>
        <translation>暖色</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Cool</source>
        <translation>寒色</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+4"/>
        <source>?</source>
        <translation>?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>#000000</source>
        <translation>#000000</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Box</source>
        <translation>ボックス</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Point</source>
        <translation>ポイント</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>patchy_raster</source>
        <translation>patchy_raster</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>bold</source>
        <translation>太字</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>italic</source>
        <translation>斜体</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Source: Patchy text</source>
        <translation>ソース: Patchy テキスト</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Source: PSD %1</source>
        <translation>ソース: PSD %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> placeholder preview</source>
        <translation> 仮プレビュー</translation>
    </message>
    <message>
        <location line="+2"/>
        <source> raster preview</source>
        <translation> ラスタープレビュー</translation>
    </message>
    <message>
        <location line="+578"/>
        <source>Smart object. Click to edit its contents.</source>
        <translation>スマートオブジェクト。クリックで内容を編集します。</translation>
    </message>
    <message>
        <source>Vector shape layer. Click to edit its appearance.</source>
        <translation>ベクトルシェイプレイヤー。クリックで外観を編集します。</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Linked smart object. Click to open the linked file.</source>
        <translation>リンクされたスマートオブジェクト。クリックでリンク先のファイルを開きます。</translation>
    </message>
    <message>
        <location line="+365"/>
        <source>Patchy preserved group layer effects for PSD round-trip but does not render them yet (groups: %1).</source>
        <translation>Patchy はグループのレイヤー効果を PSD 往復用に保持しましたが、まだ描画しません（グループ: %1）。</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Patchy preserved unsupported Photoshop Blend If payloads but does not render or edit them (%1 layer(s)).</source>
        <translation>Patchy は未対応の Photoshop「ブレンド条件 (Blend If)」ペイロードを保持しましたが、描画や編集は行いません (%1 レイヤー)。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Patchy preserved Blend If data on Photoshop group-boundary records but does not render or edit it (%1 group(s)).</source>
        <translation>Patchy は Photoshop のグループ境界レコードにある「ブレンド条件 (Blend If)」データを保持しましたが、描画や編集は行いません (%1 グループ)。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Patchy preserved unsupported Photoshop Blend If data without rendering it (%1 layer payload(s), %2 group-boundary record(s)).</source>
        <translation>Patchy は未対応の Photoshop「ブレンド条件 (Blend If)」データを描画せずに保持しました (レイヤーペイロード: %1、グループ境界レコード: %2)。</translation>
    </message>
    <message>
        <location line="+478"/>
        <source>Missing Font</source>
        <translation>フォントが見つかりません</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Patchy can&apos;t locate the font &quot;%1&quot;. Editing this PSD raster preview will substitute another font. Continue?</source>
        <translation>Patchy はフォント「%1」を見つけられません。この PSD ラスタープレビューを編集すると別のフォントで代用します。続行しますか?</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Patchy can&apos;t locate these fonts: %1. Editing this PSD raster preview will substitute other fonts. Continue?</source>
        <translation>Patchy は次のフォントを見つけられません: %1。この PSD ラスタープレビューを編集すると別のフォントで代用します。続行しますか?</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Continue</source>
        <translation>続行</translation>
    </message>
    <message>
        <location line="-1473"/>
        <source>%1
Font: %2, %3 pt%4
Color: %5
Flow: %6
%7</source>
        <translation>%1
フォント: %2、%3 pt%4
色: %5
フロー: %6
%7</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>, %1</source>
        <translation>、%1</translation>
    </message>
    <message>
        <location line="+416"/>
        <source>Folder is empty</source>
        <translation>フォルダーは空です</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collapse folder (Alt-click includes nested folders)</source>
        <translation>フォルダーを折りたたむ（Altクリックで中のフォルダーもまとめて）</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Expand folder (Alt-click includes nested folders)</source>
        <translation>フォルダーを展開（Altクリックで中のフォルダーもまとめて）</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Layer thumbnail</source>
        <translation>レイヤーサムネイル</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Text layer</source>
        <translation>テキストレイヤー</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Folder layer</source>
        <translation>フォルダーレイヤー</translation>
    </message>
    <message>
        <location line="+24"/>
        <location line="+11"/>
        <source>Layer and mask are linked. Click to unlink.</source>
        <translation>レイヤーとマスクはリンクされています。クリックでリンクを解除します。</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+11"/>
        <source>Layer and mask are unlinked. Click to link.</source>
        <translation>レイヤーとマスクはリンクされていません。クリックでリンクします。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Layer mask. Click to edit it with the paint tools, Alt-click to view it, Shift-click to disable it.</source>
        <translation>レイヤーマスク。クリックでペイントツールによる編集、Altクリックでマスク表示、Shiftクリックで無効化します。</translation>
    </message>
    <message>
        <location line="-37"/>
        <source>Layer pixels. Click to edit them instead of the mask.</source>
        <translation>レイヤーのピクセル。クリックするとマスクではなくレイヤーを編集します。</translation>
    </message>
    <message>
        <location line="+216"/>
        <source>Photoshop Document</source>
        <translation>Photoshop ドキュメント</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>PNG Image</source>
        <translation>PNG 画像</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>JPEG Image</source>
        <translation>JPEG 画像</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Bitmap Image</source>
        <translation>ビットマップ画像</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>TIFF Image</source>
        <translation>TIFF 画像</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>WebP Image</source>
        <translation>WebP 画像</translation>
    </message>
    <message>
        <location line="+70"/>
        <source>Supported Files (%1 and more)</source>
        <translation>サポートされるファイル (%1 など)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Images</source>
        <translation>画像</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>All Files (*.*)</source>
        <translation>すべてのファイル (*.*)</translation>
    </message>
    <message>
        <source>Group layer effects</source>
        <translation type="vanished">グループのレイヤー効果</translation>
    </message>
    <message>
        <location line="+313"/>
        <source>Linked file %1 was not found</source>
        <translation>リンクファイル %1 が見つかりません</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Linked file %1 has changed on disk; use Update Smart Object Content</source>
        <translation>リンクファイル %1 がディスク上で変更されています。「スマートオブジェクトのコンテンツを更新」を使用してください</translation>
    </message>
    <message>
        <location line="+848"/>
        <source>New Document</source>
        <translation>新規ドキュメント</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Clipboard</source>
        <translation>クリップボード</translation>
    </message>
    <message>
        <source>720p</source>
        <translation>720p</translation>
    </message>
    <message>
        <source>Square</source>
        <comment>new document preset</comment>
        <translation>正方形</translation>
    </message>
    <message>
        <source>Social Post</source>
        <translation>SNS投稿</translation>
    </message>
    <message>
        <source>Social Story</source>
        <translation>SNSストーリー</translation>
    </message>
    <message>
        <source>Photo 3:2</source>
        <translation>写真 3:2</translation>
    </message>
    <message>
        <source>A5</source>
        <translation>A5</translation>
    </message>
    <message>
        <source>A4</source>
        <translation>A4</translation>
    </message>
    <message>
        <source>A3</source>
        <translation>A3</translation>
    </message>
    <message>
        <source>US Letter</source>
        <translation>USレター</translation>
    </message>
    <message>
        <source>US Legal</source>
        <translation>米国リーガル</translation>
    </message>
    <message>
        <source>5 x 7 in</source>
        <translation>5 x 7 インチ</translation>
    </message>
    <message>
        <source>8 x 10 in</source>
        <translation>8 x 10 インチ</translation>
    </message>
    <message>
        <source>Create</source>
        <translation>作成</translation>
    </message>
    <message>
        <source>No image</source>
        <translation>画像なし</translation>
    </message>
    <message>
        <source>%1 x %2 px</source>
        <translation>%1 x %2 px</translation>
    </message>
    <message>
        <source>%1 x %2 in at %3 ppi</source>
        <translation>%1 x %2 インチ、%3 ppi</translation>
    </message>
    <message>
        <source>%1: %2 x %3 px at %4 ppi</source>
        <translation>%1: %2 x %3 px、%4 ppi</translation>
    </message>
    <message>
        <source>Create the document from the clipboard image</source>
        <translation>クリップボードの画像からドキュメントを作成</translation>
    </message>
    <message>
        <source>Clipboard does not contain an image</source>
        <translation>クリップボードに画像がありません</translation>
    </message>
    <message>
        <source>Choose background color</source>
        <translation>背景色を選択</translation>
    </message>
    <message>
        <source>Background color</source>
        <translation>背景色</translation>
    </message>
    <message>
        <source>Background Color</source>
        <translation>背景色</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>1080p</source>
        <translation>1080p</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>4K</source>
        <translation>4K</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Preset</source>
        <translation>プリセット</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Swap width and height</source>
        <translation>幅と高さを入れ替え</translation>
    </message>
    <message>
        <location filename="../src/ui/dialog_utils.cpp" line="+1"/>
        <source>Open value slider</source>
        <translation>値のスライダーを開く</translation>
    </message>
    <message>
        <location line="+12"/>
        <location line="+559"/>
        <location line="+42"/>
        <source>Width</source>
        <translation>幅</translation>
    </message>
    <message>
        <location filename="../src/ui/filter_workflows.cpp" line="+262"/>
        <location filename="../src/ui/layer_style_dialog.cpp" line="+115"/>
        <location filename="../src/ui/main_window.cpp" line="-596"/>
        <location line="+664"/>
        <source>White</source>
        <translation>白</translation>
    </message>
    <message>
        <location line="-10"/>
        <location filename="../src/ui/layer_style_dialog.cpp" line="-13"/>
        <location filename="../src/ui/main_window.cpp" line="-663"/>
        <location line="+664"/>
        <source>Black</source>
        <translation>黒</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="-663"/>
        <source>Transparent</source>
        <translation>透明</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Background</source>
        <translation>背景</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Layer %1</source>
        <translation>レイヤー %1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>%1M</source>
        <translation>%1M</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1K</source>
        <translation>%1K</translation>
    </message>
    <message>
        <location line="+131"/>
        <source>Image Size</source>
        <translation>画像サイズ</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>Image Size:</source>
        <translation>画像サイズ:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Dimensions:</source>
        <translation>寸法:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Original Size</source>
        <translation>元のサイズ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fit 640 x 480</source>
        <translation>640 x 480 に合わせる</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fit 1024 x 768</source>
        <translation>1024 x 768 に合わせる</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fit 1920 x 1080</source>
        <translation>1920 x 1080 に合わせる</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fit To:</source>
        <translation>合わせるサイズ:</translation>
    </message>
    <message>
        <location line="+16"/>
        <location line="+3"/>
        <location line="+278"/>
        <location line="+4"/>
        <source>Pixels</source>
        <translation>ピクセル</translation>
    </message>
    <message>
        <location line="-274"/>
        <source>Constrain proportions</source>
        <translation>縦横比を固定</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Width:</source>
        <translation>幅:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Height:</source>
        <translation>高さ:</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Pixels/Inch</source>
        <translation>ピクセル/インチ</translation>
    </message>
    <message>
        <source>Pixels/Centimeter</source>
        <translation>ピクセル/センチメートル</translation>
    </message>
    <message>
        <source>Inches</source>
        <translation>インチ</translation>
    </message>
    <message>
        <source>Centimeters</source>
        <translation>センチメートル</translation>
    </message>
    <message>
        <source>Millimeters</source>
        <translation>ミリメートル</translation>
    </message>
    <message>
        <source>Points</source>
        <translation>ポイント</translation>
    </message>
    <message>
        <source>Percent</source>
        <translation>パーセント</translation>
    </message>
    <message>
        <source>px</source>
        <translation>px</translation>
    </message>
    <message>
        <source>pt</source>
        <translation>pt</translation>
    </message>
    <message>
        <source>Resolution</source>
        <translation>解像度</translation>
    </message>
    <message>
        <source>%1 PPI</source>
        <translation>%1 PPI</translation>
    </message>
    <message>
        <source>%1 x %2 PPI</source>
        <translation>%1 x %2 PPI</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resolution:</source>
        <translation>解像度:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Resample:</source>
        <translation>再サンプル:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Bicubic Sharper (reduction)</source>
        <translation>バイキュービック法(縮小向き)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bicubic Smoother (enlargement)</source>
        <translation>バイキュービック法(拡大向き)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nearest Neighbor</source>
        <translation>ニアレストネイバー</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Create a new, larger document with more detail
Open in Generative Upscale...</source>
        <translation>より詳細な新しい大きいドキュメントを作成
生成アップスケールで開く...</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>%1 px x %2 px</source>
        <translation>%1 px x %2 px</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>Canvas Size</source>
        <translation>キャンバスサイズ</translation>
    </message>
    <message>
        <location line="+112"/>
        <source>Current Size: %1</source>
        <translation>現在のサイズ: %1</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+2"/>
        <source>%1 px</source>
        <translation>%1 px</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Relative to current dimension</source>
        <translation>現在の寸法を基準にする</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Anchor</source>
        <translation>基準位置</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Anchor top left</source>
        <translation>左上を基準</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Anchor top</source>
        <translation>上を基準</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Anchor top right</source>
        <translation>右上を基準</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Anchor left</source>
        <translation>左を基準</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Anchor center</source>
        <translation>中央を基準</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Anchor right</source>
        <translation>右を基準</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Anchor bottom left</source>
        <translation>左下を基準</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Anchor bottom</source>
        <translation>下を基準</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Anchor bottom right</source>
        <translation>右下を基準</translation>
    </message>
    <message>
        <location line="+20"/>
        <location line="+15"/>
        <source>Canvas extension color</source>
        <translation>カンバス拡張カラー</translation>
    </message>
    <message>
        <location line="-9"/>
        <source>Other...</source>
        <translation>その他...</translation>
    </message>
    <message>
        <location filename="../src/ui/filter_workflows.cpp" line="+5"/>
        <location filename="../src/ui/layer_style_dialog.cpp" line="-105"/>
        <location filename="../src/ui/main_window.cpp" line="+3"/>
        <source>Gray</source>
        <translation>グレー</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+7"/>
        <source>Choose canvas extension color</source>
        <translation>カンバス拡張カラーを選択</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Canvas Extension Color</source>
        <translation>カンバス拡張カラー</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>New Size: %1</source>
        <translation>新しいサイズ: %1</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>OK</source>
        <translation>OK</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Free Transform</source>
        <translation>自由変形</translation>
    </message>
    <message>
        <location line="+18"/>
        <location filename="../src/ui/print_dialog.cpp" line="-28"/>
        <source>X</source>
        <translation>X</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/ui/print_dialog.cpp" line="+1"/>
        <source>Y</source>
        <translation>Y</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>W</source>
        <translation>W</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>H</source>
        <translation>H</translation>
    </message>
    <message>
        <location line="+7584"/>
        <source>Warp:</source>
        <translation>ワープ:</translation>
    </message>
    <message>
        <location line="+38"/>
        <location filename="../src/ui/warp_text_dialog.cpp" line="+68"/>
        <source>Bend:</source>
        <translation>カーブ:</translation>
    </message>
    <message>
        <location line="+223"/>
        <location line="+550"/>
        <source>Fixed Ratio</source>
        <translation>縦横比固定</translation>
    </message>
    <message>
        <location line="-549"/>
        <location line="+550"/>
        <source>Fixed Size</source>
        <translation>サイズ固定</translation>
    </message>
    <message>
        <location filename="../src/ui/print_dialog.cpp" line="-357"/>
        <source>Patchy Document</source>
        <translation>Patchy ドキュメント</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Unnamed Printer</source>
        <translation>名前のないプリンター</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> (Default)</source>
        <translation> (既定)</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>No printers installed</source>
        <translation>プリンターがインストールされていません</translation>
    </message>
    <message>
        <location line="+110"/>
        <source>%1 in x %2 in at %3%</source>
        <translation>%1 in x %2 in、%3%</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>%1 x %2 %3</source>
        <translation>%1 x %2 %3</translation>
    </message>
    <message>
        <location line="+91"/>
        <location line="+7"/>
        <location line="+192"/>
        <location line="+39"/>
        <source>Patchy Print</source>
        <translation>Patchy 印刷</translation>
    </message>
    <message>
        <location line="-215"/>
        <location line="+38"/>
        <location line="+87"/>
        <source>Print</source>
        <translation>印刷</translation>
    </message>
    <message>
        <location line="-108"/>
        <source>Output</source>
        <translation>出力</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Printer</source>
        <translation>プリンター</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Page Setup...</source>
        <translation>ページ設定...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Position and Size</source>
        <translation>位置とサイズ</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Document</source>
        <translation>ドキュメント</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Selection</source>
        <translation>選択範囲</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Center Image</source>
        <translation>画像を中央に配置</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Scaled Print Size</source>
        <translation>拡大縮小後の印刷サイズ</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Scale to fit media</source>
        <translation>用紙に合わせて拡大縮小</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Image</source>
        <translation>画像</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Units:</source>
        <translation>単位:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>in</source>
        <translation>in</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>cm</source>
        <translation>cm</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>mm</source>
        <translation>mm</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Print Resolution</source>
        <translation>印刷解像度</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Print crop marks</source>
        <translation>トンボを印刷</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Save PDF...</source>
        <translation>PDF を保存...</translation>
    </message>
    <message>
        <location line="+59"/>
        <source>Save Print PDF</source>
        <translation>印刷 PDF を保存</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>PDF Document (*.pdf)</source>
        <translation>PDF ドキュメント (*.pdf)</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>PDF failed</source>
        <translation>PDF 作成に失敗</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Print failed</source>
        <translation>印刷に失敗</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="-11757"/>
        <source>Image pixels locked</source>
        <translation>画像ピクセルをロック</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Position locked</source>
        <translation>位置をロック</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1 by folder</source>
        <translation>%1 (フォルダーによる)</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>%1
Mixed selection</source>
        <translation>%1
選択範囲で混在</translation>
    </message>
    <message>
        <location filename="../src/ui/brush_presets.cpp" line="-15"/>
        <location filename="../src/ui/brush_tip_picker.cpp" line="+46"/>
        <source>Round</source>
        <translation>丸</translation>
    </message>
    <message>
        <location filename="../src/ui/brush_tip_manager_dialog.cpp" line="-140"/>
        <source>Brush Tips</source>
        <translation>ブラシ先端</translation>
    </message>
    <message>
        <location line="+47"/>
        <source>Size:</source>
        <translation>サイズ:</translation>
    </message>
    <message>
        <location line="-10"/>
        <source>Distance between stamps as a percentage of the brush size</source>
        <translation>ブラシサイズに対するスタンプ間隔の割合</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Import .abr…</source>
        <translation>.abr を読み込む…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Define from Selection</source>
        <translation>選択範囲から定義</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Create a brush tip from the current selection (or the whole image): dark pixels paint, light pixels stay clear</source>
        <translation>現在の選択範囲(または画像全体)からブラシ先端を作成します。暗いピクセルが描画され、明るいピクセルは透明のままになります</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Duplicate</source>
        <translation>複製</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Delete</source>
        <translation>削除</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Use Brush</source>
        <translation>このブラシを使う</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>%1 (%2×%3)</source>
        <translation>%1 (%2×%3)</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>%1 × %2 px</source>
        <translation>%1 × %2 px</translation>
    </message>
    <message>
        <location line="+150"/>
        <source>Import Photoshop Brushes</source>
        <translation>Photoshop ブラシを読み込む</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Photoshop Brushes (*.abr)</source>
        <translation>Photoshop ブラシ (*.abr)</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+8"/>
        <source>Import Brushes</source>
        <translation>ブラシを読み込む</translation>
    </message>
    <message>
        <location line="+12"/>
        <location line="+5"/>
        <location line="+8"/>
        <source>Define Brush Tip</source>
        <translation>ブラシ先端を定義</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>There is no image content to define a brush from.</source>
        <translation>ブラシの定義に使用できる画像がありません。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>The selection is empty or too large to use as a brush tip.</source>
        <translation>選択範囲が空か、ブラシ先端として使用するには大きすぎます。</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Brush %1</source>
        <translation>ブラシ %1</translation>
    </message>
    <message>
        <location line="+47"/>
        <source>%1 Copy</source>
        <translation>%1 のコピー</translation>
    </message>
    <message>
        <location line="-191"/>
        <source>Delete brush tip &quot;%1&quot;?</source>
        <translation>ブラシ先端「%1」を削除しますか?</translation>
    </message>
    <message>
        <location line="-204"/>
        <source>Folder:</source>
        <translation>フォルダー:</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Edit Dynamics…</source>
        <translation>ダイナミクスを編集…</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Dynamics:</source>
        <translation>ダイナミクス:</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Tip shape, dynamics, texture, dual brush, color, and effects for the selected brush tip</source>
        <translation>選択したブラシ先端のシェイプ、ダイナミクス、テクスチャ、デュアルブラシ、カラー、効果</translation>
    </message>
    <message>
        <location line="+268"/>
        <source>Brush Dynamics: %1</source>
        <translation>ブラシダイナミクス: %1</translation>
    </message>
    <message>
        <location line="-179"/>
        <source> • dynamics</source>
        <translation> • ダイナミクス</translation>
    </message>
    <message>
        <location line="-62"/>
        <location line="+339"/>
        <location line="+3"/>
        <source>Restore Default Brushes</source>
        <translation>標準ブラシを復元</translation>
    </message>
    <message>
        <location line="-340"/>
        <source>Bring back any deleted built-in brush tips</source>
        <translation>削除した内蔵ブラシ先端を復元します</translation>
    </message>
    <message numerus="yes">
        <location line="+332"/>
        <source>Restored %n default brush tip(s).</source>
        <translation>
            <numerusform>%n 個の標準ブラシ先端を復元しました。</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+3"/>
        <source>Reset %n default brush tip(s) to factory settings.</source>
        <translation>
            <numerusform>%n 個の標準ブラシ先端を工場出荷時の設定に戻しました。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+6"/>
        <source>All default brush tips are already present with factory settings.</source>
        <translation>標準のブラシ先端はすべて工場出荷時の設定で揃っています。</translation>
    </message>
    <message>
        <location line="-385"/>
        <source>No folder</source>
        <translation>フォルダーなし</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Folder for the selected brush tip(s); leave empty to remove them from folders</source>
        <translation>選択したブラシ先端のフォルダー。空にするとフォルダーから外れます</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Delete the selected brush tips or folders (Del)</source>
        <translation>選択したブラシ先端またはフォルダーを削除 (Del)</translation>
    </message>
    <message>
        <location line="+177"/>
        <source>Delete Brush Tips</source>
        <translation>ブラシ先端を削除</translation>
    </message>
    <message numerus="yes">
        <location line="-2"/>
        <source>Delete %n brush tip(s)?</source>
        <translation>
            <numerusform>%n 個のブラシ先端を削除しますか?</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="-29"/>
        <source>%n brush tip(s) selected</source>
        <translation>
            <numerusform>%n 個のブラシ先端を選択中</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ui/default_brush_tips.cpp" line="+1056"/>
        <source>Patchy Defaults</source>
        <translation>Patchy 標準</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Chalk</source>
        <translation>チョーク</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Charcoal</source>
        <translation>木炭</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Pastel</source>
        <translation>パステル</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Bristle</source>
        <translation>剛毛</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Sponge</source>
        <translation>スポンジ</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Canvas</source>
        <translation>キャンバス</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Smoke</source>
        <translation>煙</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Spray</source>
        <translation>スプレー</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Spatter</source>
        <translation>飛沫</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Stipple</source>
        <translation>点描</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Ink Splat</source>
        <translation>インクはね</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Grunge</source>
        <translation>グランジ</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Square</source>
        <translation>四角</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Calligraphy</source>
        <translation>カリグラフィ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Star</source>
        <translation>星</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Grass</source>
        <translation>草</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Dotted Line</source>
        <translation>点線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Dashed Line</source>
        <translation>破線</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Stitches</source>
        <translation>ステッチ</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Chain</source>
        <translation>チェーン</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rope</source>
        <translation>ロープ</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Arrow</source>
        <translation>矢印</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Brick Road</source>
        <translation>レンガ道</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cobblestone</source>
        <translation>石畳</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Leaf</source>
        <translation>葉</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Snowflake</source>
        <translation>雪の結晶</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Rain</source>
        <translation>雨</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Bubbles</source>
        <translation>泡</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Flower</source>
        <translation>花</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Sparkle</source>
        <translation>きらめき</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Heart</source>
        <translation>ハート</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Confetti</source>
        <translation>紙吹雪</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Paw Prints</source>
        <translation>動物の足跡</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Footprints</source>
        <translation>足跡</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Crosshatch</source>
        <translation>クロスハッチ</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>RTsoft Logo</source>
        <translation>RTsoft ロゴ</translation>
    </message>
    <message>
        <source>Textured Chalk</source>
        <translation>テクスチャチョーク</translation>
    </message>
    <message>
        <source>Dual Brush Dots</source>
        <translation>デュアルブラシドット</translation>
    </message>
    <message>
        <source>Color Scatter</source>
        <translation>カラースキャッター</translation>
    </message>
    <message>
        <source>Wet Edge Wash</source>
        <translation>ウェットエッジウォッシュ</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/brush_tip_library.cpp" line="+161"/>
        <source>Imported %n brush tip(s).</source>
        <translation>
            <numerusform>%n 個のブラシ先端を読み込みました。</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+12"/>
        <source>%n brush(es) could not be imported (no bitmap tip, or unreadable).</source>
        <translation>
            <numerusform>%n 個のブラシを読み込めませんでした (ビットマップ先端がないか、読み取れません)。</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%n brush(es) use input-driven texture depth; Patchy imported a static depth instead.</source>
        <translation>
            <numerusform>%n 個のブラシは入力連動のテクスチャ深度を使用しているため、Patchy は代わりに固定の深度で読み込みました。</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ui/palette_convert_dialog.cpp" line="+374"/>
        <source>Convert to Indexed (Palette)</source>
        <translation>インデックス (パレット) に変換</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Optimized (median cut)</source>
        <translation>最適化 (メディアンカット)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Exact image colors</source>
        <translation>画像の色をそのまま使用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Current palette</source>
        <translation>現在のパレット</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+193"/>
        <source>From file...</source>
        <translation>ファイルから...</translation>
    </message>
    <message>
        <location line="-185"/>
        <source>Palette:</source>
        <translation>パレット:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Colors:</source>
        <translation>色数:</translation>
    </message>
    <message>
        <location line="+4"/>
        <location filename="../src/ui/warp_text_dialog.cpp" line="-46"/>
        <source>None</source>
        <translation>なし</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Floyd-Steinberg</source>
        <translation>フロイド・スタインバーグ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ordered 4x4</source>
        <translation>組織的 4x4</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ordered 8x8</source>
        <translation>組織的 8x8</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Dither:</source>
        <translation>ディザ:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Pixels with alpha below this become fully transparent; the rest become opaque</source>
        <translation>アルファがこの値未満のピクセルは完全に透明になり、それ以外は不透明になります</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Alpha threshold:</source>
        <translation>アルファしきい値:</translation>
    </message>
    <message>
        <location line="-331"/>
        <source>Drag to pan. The mouse wheel zooms.</source>
        <translation>ドラッグで表示位置を移動できます。マウスホイールでズームします。</translation>
    </message>
    <message>
        <location line="+351"/>
        <source>Fit</source>
        <translation>全体</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fit the image in the preview</source>
        <translation>画像をプレビュー全体に収めます</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Zoom to 100% (1 image pixel = 1 screen pixel)</source>
        <translation>100% 表示 (画像 1 ピクセル = 画面 1 ピクセル)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Zoom out</source>
        <translation>ズームアウト</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Zoom in</source>
        <translation>ズームイン</translation>
    </message>
    <message>
        <location line="+80"/>
        <source>Fit (%1%)</source>
        <translation>全体 (%1%)</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>The image has more than 256 colors; choose Optimized instead.</source>
        <translation>画像の色数が 256 を超えています。「最適化」を選んでください。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Choose a palette.</source>
        <translation>パレットを選択してください。</translation>
    </message>
    <message numerus="yes">
        <location line="+8"/>
        <source>%n color(s)</source>
        <translation>
            <numerusform>%n 色</numerusform>
        </translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+16"/>
        <location filename="../src/ui/palette_panel.cpp" line="+414"/>
        <location line="+13"/>
        <source>Load Palette</source>
        <translation>パレットを読み込む</translation>
    </message>
    <message>
        <location filename="../src/ui/palette_panel.cpp" line="+14"/>
        <location line="+42"/>
        <source>Save Palette</source>
        <translation>パレットを保存</translation>
    </message>
    <message>
        <location line="-41"/>
        <source>GIMP Palette (*.gpl);;Hex Colors (*.hex);;JASC Palette (*.pal);;Adobe Color Table (*.act);;Adobe Color Swatches (*.aco);;PNG Swatch Strip (*.png)</source>
        <translation>GIMP パレット (*.gpl);;Hex カラー (*.hex);;JASC パレット (*.pal);;Adobe カラーテーブル (*.act);;Adobe カラースウォッチ (*.aco);;PNG スウォッチストリップ (*.png)</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Could not save the palette file.
%1</source>
        <translation>パレットファイルを保存できませんでした。
%1</translation>
    </message>
    <message>
        <location filename="../src/ui/palette_convert_dialog.cpp" line="-15"/>
        <location filename="../src/ui/palette_panel.cpp" line="-69"/>
        <source>Palette Files (*.pal *.gpl *.hex *.act *.aco *.ase *.bmp);;All Files (*)</source>
        <translation>パレットファイル (*.pal *.gpl *.hex *.act *.aco *.ase *.bmp);;すべてのファイル (*)</translation>
    </message>
    <message>
        <location line="+16"/>
        <location filename="../src/ui/palette_panel.cpp" line="+13"/>
        <source>Could not load the palette file.
%1</source>
        <translation>パレットファイルを読み込めませんでした。
%1</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>From file: %1</source>
        <translation>ファイルから: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/warp_text_dialog.cpp" line="-30"/>
        <source>Arc</source>
        <translation>円弧</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Arc Lower</source>
        <translation>下弦</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Arc Upper</source>
        <translation>上弦</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Arch</source>
        <translation>アーチ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bulge</source>
        <translation>でこぼこ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shell Lower</source>
        <translation>貝殻（下向き）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shell Upper</source>
        <translation>貝殻（上向き）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Flag</source>
        <translation>旗</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Fish</source>
        <translation>魚形</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rise</source>
        <translation>上昇</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fisheye</source>
        <translation>魚眼レンズ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Inflate</source>
        <translation>膨張</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Squeeze</source>
        <translation>絞り込み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Twist</source>
        <translation>旋回</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Warp Text</source>
        <translation>ワープテキスト</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Style:</source>
        <translation>スタイル:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Horizontal</source>
        <translation>水平方向</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Vertical</source>
        <translation>垂直方向</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Horizontal Distortion:</source>
        <translation>水平方向のゆがみ:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Vertical Distortion:</source>
        <translation>垂直方向のゆがみ:</translation>
    </message>
    <message>
        <location filename="../src/ui/filter_workflows.cpp" line="+462"/>
        <source>Colorize</source>
        <translation>色彩の統一</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+473"/>
        <source>Colorize: hue %1, saturation %2, lightness %3</source>
        <translation>色彩の統一: 色相 %1、彩度 %2、明度 %3</translation>
    </message>
    <message>
        <source>Clipped to the layer below</source>
        <translation type="vanished">下のレイヤーにクリップされています</translation>
    </message>
    <message>
        <location line="+459"/>
        <source>Clipped to the layer below. Click to release.</source>
        <translation>下のレイヤーにクリップされています。クリックで解除します。</translation>
    </message>
    <message>
        <location line="+130"/>
        <source>clipped</source>
        <translation>クリップ</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp"/>
        <source>%1 / %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp"/>
        <source>Manage…</source>
        <translation>管理…</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp"/>
        <source>Open Pattern Manager</source>
        <translation>パターンマネージャーを開く</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Patterns</source>
        <translation>パターン</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Could not update the selected pattern. Check that the pattern library folder is writable.</source>
        <translation>選択したパターンを更新できませんでした。パターンライブラリのフォルダーに書き込めることを確認してください。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Folder for the selected pattern(s); leave empty to remove them from folders</source>
        <translation>選択したパターンのフォルダー。空欄にするとフォルダーから外します</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Import…</source>
        <translation>読み込む…</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Import Photoshop .pat pattern files or images</source>
        <translation>Photoshop の .pat パターンファイルまたは画像を読み込みます</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Open as Image</source>
        <translation>画像として開く</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Open the selected pattern&apos;s texture as a new image</source>
        <translation>選択したパターンのテクスチャを新しい画像として開きます</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Drag to pan. Mouse wheel zooms. Double-click resets the view.</source>
        <translation>ドラッグでパン、マウスホイールでズーム、ダブルクリックで表示をリセットします。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Could not load the selected pattern&apos;s texture.</source>
        <translation>選択したパターンのテクスチャを読み込めませんでした。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Delete the selected patterns or folders (Del)</source>
        <translation>選択したパターンまたはフォルダーを削除します (Del)</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Restore Default Patterns</source>
        <translation>標準パターンを復元</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Bring back deleted built-in patterns and reset changed defaults</source>
        <translation>削除した内蔵パターンを復元し、変更された標準パターンを初期状態に戻します</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Use Pattern</source>
        <translation>このパターンを使う</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>%n pattern(s) selected</source>
        <translation>
            <numerusform>%n 個のパターンを選択中</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Delete pattern &quot;%1&quot;?</source>
        <translation>パターン「%1」を削除しますか？</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Delete %n pattern(s)?</source>
        <translation>
            <numerusform>%n 個のパターンを削除しますか？</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Delete Patterns</source>
        <translation>パターンを削除</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Could not delete %n pattern(s). Check that the pattern library folder is writable.</source>
        <translation>
            <numerusform>%n 個のパターンを削除できませんでした。パターンライブラリのフォルダーに書き込めることを確認してください。</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Patterns and Images</source>
        <translation>パターンと画像</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Photoshop Patterns</source>
        <translation>Photoshop パターン</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>No patterns could be imported.</source>
        <translation>パターンを読み込めませんでした。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Import Patterns</source>
        <translation>パターンを読み込む</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Imported %n pattern(s).</source>
        <translation>
            <numerusform>%n 個のパターンを読み込みました。</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>All default patterns are already present with factory settings.</source>
        <translation>すべての標準パターンは既に初期設定の状態で揃っています。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Some default patterns could not be restored. Check that the pattern library folder is writable.</source>
        <translation>一部の標準パターンを復元できませんでした。パターンライブラリのフォルダーに書き込めることを確認してください。</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Restored %n default pattern(s).</source>
        <translation>
            <numerusform>%n 個の標準パターンを復元しました。</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/pattern_manager_dialog.cpp"/>
        <source>Reset %n default pattern(s) to factory settings.</source>
        <translation>
            <numerusform>%n 個の標準パターンを初期設定に戻しました。</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Styles</source>
        <translation>スタイル</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp"/>
        <source>Style Presets</source>
        <translation>スタイルプリセット</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp"/>
        <source>Click a preset to replace the current effects. Right-click a folder or style to export it as a Photoshop .asl file.</source>
        <translation>プリセットをクリックすると現在の効果が置き換わります。フォルダーやスタイルを右クリックすると Photoshop の .asl ファイルとして書き出せます。</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp"/>
        <source>New Style…</source>
        <translation>新規スタイル…</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp"/>
        <source>Save the current effects (and optionally blending options) as a preset</source>
        <translation>現在の効果(必要に応じてブレンドオプションも)をプリセットとして保存します</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp"/>
        <source>Manage Styles…</source>
        <translation>スタイルの管理…</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp"/>
        <source>New Style</source>
        <translation>新規スタイル</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp"/>
        <source>Include blending options (opacity, Fill, blend mode, Blend If)</source>
        <translation>ブレンドオプション(不透明度、塗り、描画モード、ブレンド条件)を含める</translation>
    </message>
    <message>
        <location filename="../src/ui/layer_style_dialog.cpp"/>
        <source>Could not save the style. Check that the style library folder is writable.</source>
        <translation>スタイルを保存できませんでした。スタイルライブラリのフォルダーに書き込めることを確認してください。</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Contains:</source>
        <translation>内容:</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Import .asl…</source>
        <translation>.asl を読み込み…</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Export…</source>
        <translation>書き出し…</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Export the selected styles or folders to a .asl file</source>
        <translation>選択したスタイルまたはフォルダーを .asl ファイルに書き出します</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Delete the selected styles or folders (Del)</source>
        <translation>選択したスタイルまたはフォルダーを削除 (Del)</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Restore Default Styles</source>
        <translation>標準スタイルを復元</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Bring back deleted built-in styles and reset changed defaults</source>
        <translation>削除した標準スタイルを復元し、変更された標準スタイルを初期設定に戻します</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Use Style</source>
        <translation>スタイルを使用</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Folder for the selected style(s); leave empty to remove them from folders</source>
        <translation>選択したスタイルのフォルダー。空にするとフォルダーから外れます</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Could not update the selected style. Check that the style library folder is writable.</source>
        <translation>選択したスタイルを更新できませんでした。スタイルライブラリのフォルダーに書き込めることを確認してください。</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>No effects</source>
        <translation>効果なし</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>%n style(s) selected</source>
        <translation>
            <numerusform>%n 個のスタイルを選択中</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Delete style &quot;%1&quot;?</source>
        <translation>スタイル「%1」を削除しますか?</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Delete %n style(s)?</source>
        <translation>
            <numerusform>%n 個のスタイルを削除しますか?</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Delete Styles</source>
        <translation>スタイルの削除</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Could not delete %n style(s). Check that the style library folder is writable.</source>
        <translation>
            <numerusform>%n 個のスタイルを削除できませんでした。スタイルライブラリのフォルダーに書き込めることを確認してください。</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Import Photoshop Styles</source>
        <translation>Photoshop スタイルの読み込み</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Photoshop Styles (*.asl)</source>
        <translation>Photoshop スタイル (*.asl)</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Import Styles</source>
        <translation>スタイルの読み込み</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Imported %n style(s).</source>
        <translation>
            <numerusform>%n 個のスタイルを読み込みました。</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>All default styles are already present with factory settings.</source>
        <translation>すべての標準スタイルは既に初期設定の状態で揃っています。</translation>
    </message>
    <message>
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Some default styles could not be restored. Check that the style library folder is writable.</source>
        <translation>一部の標準スタイルを復元できませんでした。スタイルライブラリのフォルダーに書き込めることを確認してください。</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Restored %n default style(s).</source>
        <translation>
            <numerusform>%n 個の標準スタイルを復元しました。</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/style_manager_dialog.cpp"/>
        <source>Reset %n default style(s) to factory settings.</source>
        <translation>
            <numerusform>%n 個の標準スタイルを初期設定に戻しました。</numerusform>
        </translation>
    </message>
    <message>
        <source>Basics</source>
        <translation>基本</translation>
    </message>
    <message>
        <source>Adventure</source>
        <translation>アドベンチャー</translation>
    </message>
    <message>
        <source>Hack the Gibson</source>
        <translation>ハック・ザ・ギブソン</translation>
    </message>
    <message>
        <source>A Galaxy Far Away</source>
        <translation>遠い銀河</translation>
    </message>
    <message>
        <source>Neon Nights</source>
        <translation>ネオンナイト</translation>
    </message>
    <message>
        <source>Arcade Cabinet</source>
        <translation>アーケード筐体</translation>
    </message>
    <message>
        <source>Chrome Bumper</source>
        <translation>クロームバンパー</translation>
    </message>
    <message>
        <source>Liquid Gold</source>
        <translation>リキッドゴールド</translation>
    </message>
    <message>
        <source>Ice Cold</source>
        <translation>アイスコールド</translation>
    </message>
    <message>
        <source>Molten Core</source>
        <translation>溶岩コア</translation>
    </message>
    <message>
        <source>Toxic Ooze</source>
        <translation>毒スライム</translation>
    </message>
    <message>
        <source>Midnight Horror</source>
        <translation>ミッドナイトホラー</translation>
    </message>
    <message>
        <source>Wanted Poster</source>
        <translation>手配書</translation>
    </message>
    <message>
        <source>Comic Pow</source>
        <translation>コミックパウ</translation>
    </message>
    <message>
        <source>Bubble Pop</source>
        <translation>バブルポップ</translation>
    </message>
    <message>
        <source>Saturday Cartoon</source>
        <translation>サタデーカートゥーン</translation>
    </message>
    <message>
        <source>Space Cadet</source>
        <translation>スペースカデット</translation>
    </message>
    <message>
        <source>Royal Decree</source>
        <translation>王室の勅令</translation>
    </message>
    <message>
        <source>Stamped Steel</source>
        <translation>刻印スチール</translation>
    </message>
    <message>
        <source>Honey Drip</source>
        <translation>ハニードリップ</translation>
    </message>
    <message>
        <source>Blueprint</source>
        <translation>ブループリント</translation>
    </message>
    <message>
        <source>Soft Shadow</source>
        <translation>ソフトシャドウ</translation>
    </message>
    <message>
        <source>Sticker Outline</source>
        <translation>ステッカー縁取り</translation>
    </message>
    <message>
        <source>Simple Emboss</source>
        <translation>シンプルエンボス</translation>
    </message>
    <message>
        <source>Warm Glow</source>
        <translation>温かい光彩</translation>
    </message>
    <message>
        <source>Neon Edge</source>
        <translation>ネオンエッジ</translation>
    </message>
    <message>
        <source>Letterpress</source>
        <translation>活版印刷</translation>
    </message>
    <message>
        <source>Materials</source>
        <translation>マテリアル</translation>
    </message>
    <message>
        <source>Textures</source>
        <translation>テクスチャ</translation>
    </message>
    <message>
        <source>Fine Wood Grain</source>
        <translation>細かい木目</translation>
    </message>
    <message>
        <source>Dark Walnut</source>
        <translation>ダークウォールナット</translation>
    </message>
    <message>
        <source>Oak Veneer</source>
        <translation>オーク突板</translation>
    </message>
    <message>
        <source>Weathered Wood</source>
        <translation>風化した木材</translation>
    </message>
    <message>
        <source>Old Planks</source>
        <translation>古い板材</translation>
    </message>
    <message>
        <source>Medieval Wood</source>
        <translation>中世の木材</translation>
    </message>
    <message>
        <source>Tree Bark</source>
        <translation>樹皮</translation>
    </message>
    <message>
        <source>Weathered Marble</source>
        <translation>風化した大理石</translation>
    </message>
    <message>
        <source>Slate Slabs</source>
        <translation>スレート板</translation>
    </message>
    <message>
        <source>Granite Blocks</source>
        <translation>御影石ブロック</translation>
    </message>
    <message>
        <source>Rock Face</source>
        <translation>岩肌</translation>
    </message>
    <message>
        <source>Coarse Rust</source>
        <translation>粗い錆</translation>
    </message>
    <message>
        <source>Steel Plate</source>
        <translation>鋼板</translation>
    </message>
    <message>
        <source>Brown Leather</source>
        <translation>ブラウンレザー</translation>
    </message>
    <message>
        <source>Denim Weave</source>
        <translation>デニム織り</translation>
    </message>
    <message>
        <source>Burlap</source>
        <translation>麻布</translation>
    </message>
    <message>
        <source>Rippled Sand</source>
        <translation>波紋の砂</translation>
    </message>
    <message>
        <source>Snow</source>
        <translation>雪</translation>
    </message>
    <message>
        <source>Cracked Earth</source>
        <translation>ひび割れた大地</translation>
    </message>
    <message>
        <source>Mossy Forest Floor</source>
        <translation>苔むした森の地面</translation>
    </message>
    <message>
        <source>Carved Oak</source>
        <translation>オーク彫刻</translation>
    </message>
    <message>
        <source>Walnut Gloss</source>
        <translation>ウォールナット光沢</translation>
    </message>
    <message>
        <source>Weathered Sign</source>
        <translation>風化した看板</translation>
    </message>
    <message>
        <source>Driftwood</source>
        <translation>流木</translation>
    </message>
    <message>
        <source>Timber Grain</source>
        <translation>木目材</translation>
    </message>
    <message>
        <source>Marble Monument</source>
        <translation>大理石の碑</translation>
    </message>
    <message>
        <source>Slate Etched</source>
        <translation>スレート刻印</translation>
    </message>
    <message>
        <source>Granite Bold</source>
        <translation>御影石ボールド</translation>
    </message>
    <message>
        <source>Rust Bucket</source>
        <translation>錆びた鉄</translation>
    </message>
    <message>
        <source>Riveted Steel</source>
        <translation>リベット鋼板</translation>
    </message>
    <message>
        <source>Leather Stamp</source>
        <translation>レザー型押し</translation>
    </message>
    <message>
        <source>Frost Drift</source>
        <translation>雪だまり</translation>
    </message>
    <message>
        <source>Cracked Desert</source>
        <translation>ひび割れた砂漠</translation>
    </message>
    <message>
        <source>Fill Opacity</source>
        <translation>塗りの不透明度</translation>
    </message>
    <message><source>Dodge</source><translation>覆い焼き</translation></message>
    <message><source>Burn</source><translation>焼き込み</translation></message>
    <message><source>Pattern Stamp</source><translation>パターンスタンプ</translation></message>
    <message><source>Run the JavaScript file. With a running instance this forwards the request and exits; otherwise a new unattended instance opens the given files, runs the script, and exits (0 = ok, 4 = script error).</source><translation>JavaScript ファイルを実行します。実行中のインスタンスがあればリクエストを転送して終了し、なければ新しい無人インスタンスが指定ファイルを開いてスクリプトを実行し終了します (0 = 成功、4 = スクリプトエラー)。</translation></message>
    <message><source>With --run-script: write console output, errors, and a final [done]/[failed] line to this file when the script completes.</source><translation>--run-script と併用: スクリプト完了時にコンソール出力・エラー・最終行 [done]/[failed] をこのファイルに書き込みます。</translation></message>
</context>
<context>
    <name>ScannerImport</name>
    <message>
        <location filename="../src/ui/scanner_import_mac.mm" line="+149"/>
        <source>Import from Scanner</source>
        <translation>スキャナーから読み込み</translation>
    </message>
    <message>
        <location line="+72"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="-181"/>
        <source>Unknown scanner error</source>
        <translation>不明なスキャナーエラー</translation>
    </message>
    <message>
        <location filename="../src/ui/scanner_import_win.cpp" line="+39"/>
        <source>Windows Image Acquisition is unavailable (%1)</source>
        <translation>Windows Image Acquisition を利用できません (%1)</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>The scan could not be completed (%1)</source>
        <translation>スキャンを完了できませんでした (%1)</translation>
    </message>
    <message>
        <location filename="../src/ui/scanner_import_mac.mm" line="+307"/>
        <source>The scanner did not return an image file.</source>
        <translation>スキャナーから画像ファイルが返されませんでした。</translation>
    </message>
    <message>
        <location line="+76"/>
        <source>macOS scanner import requires the Cocoa platform.</source>
        <translation>macOS のスキャナー読み込みには Cocoa プラットフォームが必要です。</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+25"/>
        <source>The scanner window could not be opened.</source>
        <translation>スキャナーウィンドウを開けませんでした。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The temporary scan folder could not be created.</source>
        <translation>スキャン用の一時フォルダーを作成できませんでした。</translation>
    </message>
</context>
<context>
    <name>patchy::ui::BrushDynamicsButton</name>
    <message>
        <location filename="../src/ui/brush_dynamics_popup.cpp" line="+414"/>
        <source>Dynamics</source>
        <translation>ダイナミクス</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Brush dynamics and effects for the active brush tip</source>
        <translation>アクティブなブラシ先端のダイナミクスと効果</translation>
    </message>
    <message>
        <location line="-2"/>
        <source>Brush dynamics and effects for the Round brush (this session only; resets on the next launch)</source>
        <translation>丸ブラシのダイナミクスと効果（このセッション限定。次回起動時にリセットされます）</translation>
    </message>
</context>
<context>
    <name>patchy::ui::BrushDynamicsPanel</name>
    <message>
        <location line="-261"/>
        <source>Tip Shape</source>
        <translation>先端のシェイプ</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Angle:</source>
        <translation>角度:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Roundness:</source>
        <translation>真円率:</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Shape Dynamics</source>
        <translation>シェイプダイナミクス</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Size Jitter:</source>
        <translation>サイズのジッター:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Minimum Diameter:</source>
        <translation>最小の直径:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Angle Jitter:</source>
        <translation>角度のジッター:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Angle Control:</source>
        <translation>角度のコントロール:</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>Size Control:</source>
        <translation>サイズのコントロール:</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Roundness Control:</source>
        <translation>真円率のコントロール:</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Scatter Control:</source>
        <translation>散布のコントロール:</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Count Control:</source>
        <translation>数のコントロール:</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Opacity Control:</source>
        <translation>不透明度のコントロール:</translation>
    </message>
    <message>
        <source>Flow Control:</source>
        <translation>フローのコントロール:</translation>
    </message>
    <message>
        <location line="-131"/>
        <source>Use Global Pen Setting</source>
        <translation>グローバルペン設定を使用</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+61"/>
        <source>Stylus Wheel</source>
        <translation>スタイラスホイール</translation>
    </message>
    <message>
        <location line="-65"/>
        <location line="+58"/>
        <source>Off</source>
        <translation>オフ</translation>
    </message>
    <message>
        <location line="-57"/>
        <location line="+58"/>
        <source>Fade</source>
        <translation>フェード</translation>
    </message>
    <message>
        <location line="-57"/>
        <location line="+58"/>
        <source>Pen Pressure</source>
        <translation>筆圧</translation>
    </message>
    <message>
        <location line="-57"/>
        <location line="+58"/>
        <source>Pen Tilt</source>
        <translation>ペンの傾き</translation>
    </message>
    <message>
        <location line="-56"/>
        <location line="+57"/>
        <source>Pen Rotation</source>
        <translation>ペンの回転</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Initial Direction</source>
        <translation>初期方向</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Direction</source>
        <translation>進行方向</translation>
    </message>
    <message>
        <location line="-54"/>
        <location line="+60"/>
        <source>Spacing steps to fade over</source>
        <translation>フェードにかける間隔ステップ数</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Roundness Jitter:</source>
        <translation>真円率のジッター:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Minimum Roundness:</source>
        <translation>最小の真円率:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Flip X Jitter</source>
        <translation>Xジッターを反転</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Flip Y Jitter</source>
        <translation>Yジッターを反転</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Scattering</source>
        <translation>散布</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Scatter:</source>
        <translation>散布:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Both Axes</source>
        <translation>両軸</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Count:</source>
        <translation>数:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Count Jitter:</source>
        <translation>数のジッター:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Transfer</source>
        <translation>転写</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Opacity Jitter:</source>
        <translation>不透明度のジッター:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Minimum Opacity:</source>
        <translation>最小の不透明度:</translation>
    </message>
    <message>
        <source>Flow Jitter:</source>
        <translation>フローのジッター:</translation>
    </message>
    <message>
        <source>Minimum Flow:</source>
        <translation>最小のフロー:</translation>
    </message>
    <message>
        <source>Texture</source>
        <translation>テクスチャ</translation>
    </message>
    <message>
        <source>Enable Texture</source>
        <translation>テクスチャを有効化</translation>
    </message>
    <message>
        <source>Grain:</source>
        <translation>粒子:</translation>
    </message>
    <message>
        <source>Fine Grain</source>
        <translation>細かい粒子</translation>
    </message>
    <message>
        <source>Canvas</source>
        <translation>キャンバス</translation>
    </message>
    <message>
        <source>Speckle</source>
        <translation>斑点</translation>
    </message>
    <message>
        <source>Scale:</source>
        <translation>スケール:</translation>
    </message>
    <message>
        <source>Depth:</source>
        <translation>深さ:</translation>
    </message>
    <message>
        <source>Invert Texture</source>
        <translation>テクスチャを反転</translation>
    </message>
    <message>
        <source>Dual Brush</source>
        <translation>デュアルブラシ</translation>
    </message>
    <message>
        <source>Enable Dual Brush</source>
        <translation>デュアルブラシを有効化</translation>
    </message>
    <message>
        <source>Secondary Size:</source>
        <translation>サブブラシのサイズ:</translation>
    </message>
    <message>
        <source>Secondary Hardness:</source>
        <translation>サブブラシの硬さ:</translation>
    </message>
    <message>
        <source>Secondary Spacing:</source>
        <translation>サブブラシの間隔:</translation>
    </message>
    <message>
        <source>Color Dynamics</source>
        <translation>カラーダイナミクス</translation>
    </message>
    <message>
        <source>Enable Color Dynamics</source>
        <translation>カラーダイナミクスを有効化</translation>
    </message>
    <message>
        <source>Foreground/Background Jitter:</source>
        <translation>描画色/背景色のジッター:</translation>
    </message>
    <message>
        <source>Color Control:</source>
        <translation>カラーのコントロール:</translation>
    </message>
    <message>
        <source>Hue Jitter:</source>
        <translation>色相のジッター:</translation>
    </message>
    <message>
        <source>Saturation Jitter:</source>
        <translation>彩度のジッター:</translation>
    </message>
    <message>
        <source>Brightness Jitter:</source>
        <translation>明るさのジッター:</translation>
    </message>
    <message>
        <source>Purity:</source>
        <translation>純度:</translation>
    </message>
    <message>
        <source>Apply Per Tip</source>
        <translation>ブラシ先端ごとに適用</translation>
    </message>
    <message>
        <source>Brush Effects</source>
        <translation>ブラシ効果</translation>
    </message>
    <message>
        <source>Wet Edges</source>
        <translation>ウェットエッジ</translation>
    </message>
    <message>
        <source>Builds paint along stroke edges for a watercolor wash. It does not smear canvas colors; use Smudge for that.</source>
        <translation>水彩のにじみのようにストロークの縁に絵の具をためます。キャンバスの色はこすり混ぜません。その場合は指先ツールを使用してください。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Reset</source>
        <translation>リセット</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reset the tip shape and all dynamics to defaults</source>
        <translation>先端のシェイプとすべてのダイナミクスを初期設定に戻します</translation>
    </message>
</context>
<context>
    <name>patchy::ui::BrushTipLibrary</name>
    <message>
        <location filename="../src/ui/brush_tip_library.cpp" line="+245"/>
        <source>Could not open &quot;%1&quot;.</source>
        <translation>「%1」を開けませんでした。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Brush %1</source>
        <translation>ブラシ %1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Could not save brush &quot;%1&quot;.</source>
        <translation>ブラシ「%1」を保存できませんでした。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>No brush tips could be imported from &quot;%1&quot;.</source>
        <translation>「%1」からブラシ先端を読み込めませんでした。</translation>
    </message>
</context>
<context>
    <name>patchy::ui::BrushTipPicker</name>
    <message>
        <location filename="../src/ui/brush_tip_picker.cpp" line="+65"/>
        <source>Brush tip: %1</source>
        <translation>ブラシ先端: %1</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+27"/>
        <source> • dynamics</source>
        <translation> • ダイナミクス</translation>
    </message>
    <message>
        <location line="-29"/>
        <source>Brush tip: %1 (%2×%3)</source>
        <translation>ブラシ先端: %1 (%2×%3)</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>%1 (%2×%3)</source>
        <translation>%1 (%2×%3)</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>Manage…</source>
        <translation>管理…</translation>
    </message>
    <message>
        <location line="-41"/>
        <source>All Brushes</source>
        <translation>すべてのブラシ</translation>
    </message>
    <message>
        <location line="-51"/>
        <source>%1 — %2 (%3×%4)</source>
        <translation>%1 — %2 (%3×%4)</translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Import .abr…</source>
        <translation>.abr を読み込む…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Import brushes from a Photoshop .abr file</source>
        <translation>Photoshop の .abr ファイルからブラシを読み込みます</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>New from Selection…</source>
        <translation>選択範囲から新規…</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Create a brush tip from the current selection (or the whole image): dark pixels paint, light pixels stay clear</source>
        <translation>現在の選択範囲(または画像全体)からブラシ先端を作成します。暗いピクセルが描画され、明るいピクセルは透明のままになります</translation>
    </message>
</context>
<context>
    <name>patchy::ui::CanvasWidget</name>
    <message>
        <location filename="../src/ui/canvas_widget.cpp" line="+9988"/>
        <source>Size: %1 px  Soft: %2%</source>
        <translation>サイズ: %1 px  柔らかさ: %2%</translation>
    </message>
    <message>
        <source>Transform path: drag inside to move, handles to scale, outside to rotate. Enter commits, Esc cancels.</source>
        <translation>パスを変形: 内側をドラッグで移動、ハンドルで拡大縮小、外側で回転。Enter で確定、Esc でキャンセルします。</translation>
    </message>
    <message>
        <source>Transform path</source>
        <translation>パスを変形</translation>
    </message>
    <message>
        <source>Transformed the path</source>
        <translation>パスを変形しました</translation>
    </message>
    <message>
        <source>Cancelled the path transform</source>
        <translation>パスの変形をキャンセルしました</translation>
    </message>
    <message>
        <location line="-2067"/>
        <source>%1 x %2 px</source>
        <translation>%1 x %2 px</translation>
    </message>
    <message>
        <location line="-1232"/>
        <source>Brush opacity: %1%</source>
        <translation>ブラシの不透明度: %1%</translation>
    </message>
    <message>
        <source>Brush flow: %1%</source>
        <translation>ブラシのフロー: %1%</translation>
    </message>
    <message>
        <location line="-5"/>
        <source>Gradient opacity: %1%</source>
        <translation>グラデーションの不透明度: %1%</translation>
    </message>
    <message>
        <location line="+2926"/>
        <source>Finish the open dialog before editing the document</source>
        <translation>開いているダイアログを閉じてからドキュメントを編集してください</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Layer pixels are locked.</source>
        <translation>レイヤーの画像ピクセルはロックされています。</translation>
    </message>
    <message>
        <location line="-4462"/>
        <location line="+4468"/>
        <source>Layer position is locked.</source>
        <translation>レイヤーの位置はロックされています。</translation>
    </message>
    <message>
        <location line="-6773"/>
        <source>Select an editable pixel layer to transform</source>
        <translation>変形する編集可能なピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>This smart object is preview-only and can&apos;t be transformed. Rasterize the layer first.</source>
        <translation>このスマートオブジェクトはプレビュー専用のため変形できません。先にレイヤーをラスタライズしてください。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Layer has no opaque pixels to transform</source>
        <translation>変形できる不透明ピクセルがレイヤーにありません</translation>
    </message>
    <message>
        <location line="+32"/>
        <location line="+10924"/>
        <source>Drag handles to transform. Shift keeps aspect ratio.</source>
        <translation>ハンドルをドラッグして変形します。Shift で縦横比を保持します。</translation>
    </message>
    <message>
        <location line="-10268"/>
        <source>Inverted selection</source>
        <translation>選択範囲を反転しました</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Reselected previous selection</source>
        <translation>前の選択範囲を再選択しました</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Showing selection edges</source>
        <translation>選択範囲の境界を表示しています</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Hiding selection edges</source>
        <translation>選択範囲の境界を非表示にしています</translation>
    </message>
    <message>
        <location line="+190"/>
        <source>Expanded selection by %1 px</source>
        <translation>選択範囲を %1 px 拡張しました</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Contracted selection by %1 px</source>
        <translation>選択範囲を %1 px 縮小しました</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Selected %1 px border</source>
        <translation>%1 px の境界を選択しました</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Select a pixel layer first</source>
        <translation>先にピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Layer has no opaque pixels</source>
        <translation>レイヤーに不透明ピクセルがありません</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Selected layer opacity</source>
        <translation>レイヤーの不透明部分を選択しました</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Layer has no mask</source>
        <translation>レイヤーにマスクがありません</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Layer mask has no selected pixels</source>
        <translation>レイヤーマスクに選択済みピクセルがありません</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Selected layer mask</source>
        <translation>レイヤーマスクを選択しました</translation>
    </message>
    <message>
        <location line="+146"/>
        <source>Make a selection before growing</source>
        <translation>拡張する前に選択範囲を作成してください</translation>
    </message>
    <message>
        <location line="+109"/>
        <source>Grew selection to %1 px</source>
        <translation>選択範囲を %1 px に拡張しました</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Make a selection before selecting similar pixels</source>
        <translation>類似ピクセルを選択する前に選択範囲を作成してください</translation>
    </message>
    <message>
        <location line="+70"/>
        <source>Selected %1 similar px</source>
        <translation>類似ピクセル %1 px を選択しました</translation>
    </message>
    <message>
        <location line="+201"/>
        <source>No document</source>
        <translation>ドキュメントがありません</translation>
    </message>
    <message>
        <location line="+513"/>
        <source>This tool is unavailable while viewing a document channel</source>
        <translation>ドキュメントチャンネルの表示中はこのツールを使用できません</translation>
    </message>
    <message>
        <location line="+49"/>
        <source>Clone is unavailable while editing a grayscale channel</source>
        <translation>グレースケールチャンネルの編集中はコピースタンプを使用できません</translation>
    </message>
    <message>
        <location filename="../src/ui/canvas_widget_events.cpp" line="553"/>
        <source>Healing is unavailable while editing a grayscale channel</source>
        <translation>グレースケールチャンネルの編集中は修復ブラシを使用できません</translation>
    </message>
    <message>
        <location line="+327"/>
        <source>Smudge is unavailable while editing a grayscale channel</source>
        <translation>グレースケールチャンネルの編集中は指先ツールを使用できません</translation>
    </message>
    <message>
        <source>Mixer Brush is unavailable while editing a grayscale channel</source>
        <translation>グレースケールチャンネルの編集中はミキサーブラシを使用できません</translation>
    </message>
    <message>
        <location line="+4887"/>
        <source>Select a saved channel to edit</source>
        <translation>編集する保存済みチャンネルを選択してください</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Spot channels are read-only</source>
        <translation>スポットカラーチャンネルは読み取り専用です</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Color component channels are read-only</source>
        <translation>色成分チャンネルは読み取り専用です</translation>
    </message>
    <message>
        <location line="+3038"/>
        <source>Select an editable pixel layer to warp</source>
        <translation>ワープするには編集可能なピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Text layers use Warp Text (the Type tool&apos;s Warp... button). For a custom mesh, convert to a smart object or rasterize first.</source>
        <translation>テキストレイヤーはワープテキスト（文字ツールのワープ...ボタン）を使用します。カスタムメッシュにするには、スマートオブジェクトに変換するかラスタライズしてください。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>This smart object is preview-only and can&apos;t be warped. Rasterize the layer first.</source>
        <translation>このスマートオブジェクトはプレビュー専用のためワープできません。先にレイヤーをラスタライズしてください。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>This smart object&apos;s contents can&apos;t be decoded for warping</source>
        <translation>このスマートオブジェクトのコンテンツをワープ用にデコードできません</translation>
    </message>
    <message>
        <location line="+47"/>
        <source>Layer has no pixels to warp</source>
        <translation>ワープできるピクセルがレイヤーにありません</translation>
    </message>
    <message>
        <location line="+52"/>
        <location line="+459"/>
        <source>Drag the warp grid handles. Enter applies, Esc cancels.</source>
        <translation>ワープグリッドのハンドルをドラッグします。Enter で適用、Esc でキャンセルします。</translation>
    </message>
    <message>
        <location line="-624"/>
        <location line="+178"/>
        <location line="+231"/>
        <source>Warp Transform cancelled</source>
        <translation>ワープ変形をキャンセルしました</translation>
    </message>
    <message>
        <location line="-422"/>
        <location line="+412"/>
        <source>Warp Transform</source>
        <translation>ワープ変形</translation>
    </message>
    <message>
        <location line="-399"/>
        <location line="+409"/>
        <source>Warped layer</source>
        <translation>レイヤーをワープしました</translation>
    </message>
    <message>
        <location line="-10317"/>
        <source>Forced refresh</source>
        <translation>強制再描画しました</translation>
    </message>
    <message>
        <location line="-105"/>
        <location line="+5082"/>
        <source>Processing...</source>
        <translation>処理中...</translation>
    </message>
    <message>
        <source>Clone is unavailable while editing a layer mask</source>
        <translation type="vanished">レイヤーマスク編集中はクローンを使用できません</translation>
    </message>
    <message>
        <location line="-3297"/>
        <source>Alt-click to set a clone source</source>
        <translation>Alt クリックでクローン元を設定します</translation>
    </message>
    <message>
        <location filename="../src/ui/canvas_widget_events.cpp" line="562"/>
        <source>Alt-click to set a healing source</source>
        <translation>Alt クリックで修復元を設定します</translation>
    </message>
    <message>
        <location line="+5007"/>
        <source>Select the Clone or Healing Brush tool to set a sample source</source>
        <translation>サンプル元を設定するにはクローンまたは修復ブラシツールを選択してください</translation>
    </message>
    <message>
        <location line="-5003"/>
        <source>Clone stamp</source>
        <translation>クローンスタンプ</translation>
    </message>
    <message>
        <location filename="../src/ui/canvas_widget_events.cpp" line="567"/>
        <source>Healing brush</source>
        <translation>修復ブラシ</translation>
    </message>
    <message>
        <source>Choose a pattern before painting</source>
        <translation>ペイントする前にパターンを選択してください</translation>
    </message>
    <message>
        <source>Pattern stamp</source>
        <translation>パターンスタンプ</translation>
    </message>
    <message>
        <location line="+127"/>
        <source>Click an editable layer to move</source>
        <translation>移動する編集可能なレイヤーをクリックしてください</translation>
    </message>
    <message>
        <location line="+172"/>
        <source>Fill</source>
        <translation>塗りつぶし</translation>
    </message>
    <message>
        <source>Smudge is unavailable while editing a layer mask</source>
        <translation type="vanished">レイヤーマスク編集中は指先ツールを使用できません</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Erase</source>
        <translation>消去</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Brush stroke</source>
        <translation>ブラシストローク</translation>
    </message>
    <message>
        <source>Mixer Brush stroke</source>
        <translation>ミキサーブラシストローク</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Smudge</source>
        <translation>指先</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Gradient</source>
        <translation>グラデーション</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+4320"/>
        <source>Shape</source>
        <translation>図形</translation>
    </message>
    <message>
        <location line="-3734"/>
        <source>Move layers</source>
        <translation>レイヤーを移動</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Move layer</source>
        <translation>レイヤーを移動</translation>
    </message>
    <message>
        <location line="+651"/>
        <source>Nudge layers</source>
        <translation>レイヤーを少し移動</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Nudge layer</source>
        <translation>レイヤーを少し移動</translation>
    </message>
    <message>
        <location line="+3080"/>
        <location line="+12"/>
        <source>Selection</source>
        <translation>選択範囲</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Text</source>
        <translation>テキスト</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Selection bounds</source>
        <translation>選択範囲の境界</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Zoom</source>
        <translation>ズーム</translation>
    </message>
    <message>
        <location line="+541"/>
        <source>Select a layer mask to edit</source>
        <translation>編集するレイヤーマスクを選択してください</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Select an editable 8-bit pixel layer first</source>
        <translation>先に編集可能な 8 ビットピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Select a normal pixel layer before painting on text</source>
        <translation>テキスト上に描画する前に通常のピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+880"/>
        <source>Clone source set at %1, %2</source>
        <translation>クローン元を %1, %2 に設定しました</translation>
    </message>
    <message>
        <location filename="../src/ui/canvas_widget_brush.cpp" line="1694"/>
        <source>Healing source set at %1, %2</source>
        <translation>修復元を %1, %2 に設定しました</translation>
    </message>
    <message>
        <location line="+634"/>
        <source>Select a pixel layer before using Magic Wand</source>
        <translation>自動選択を使う前にピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+129"/>
        <source>Magic Wand selected %1 px</source>
        <translation>自動選択で %1 px を選択しました</translation>
    </message>
    <message>
        <location line="-6614"/>
        <source>Magic Wand</source>
        <translation>自動選択</translation>
    </message>
    <message>
        <location line="+6719"/>
        <source>Select a pixel layer before using Quick Select</source>
        <translation>クイック選択を使う前にピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+118"/>
        <source>Quick Select selected %1 px</source>
        <translation>クイック選択で %1 px を選択しました</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Quick Select removed %1 px</source>
        <translation>クイック選択で %1 px を選択解除しました</translation>
    </message>
    <message>
        <location line="-46"/>
        <location line="+49"/>
        <source>Quick Select</source>
        <translation>クイック選択</translation>
    </message>
    <message>
        <location line="-6073"/>
        <location line="+32"/>
        <location line="+61"/>
        <source>Deselect</source>
        <translation>選択を解除</translation>
    </message>
    <message>
        <location line="-88"/>
        <location line="+6718"/>
        <source>Move Selection</source>
        <translation>選択範囲を移動</translation>
    </message>
    <message>
        <location line="-6698"/>
        <source>Elliptical Marquee</source>
        <translation>楕円形選択</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Rectangular Marquee</source>
        <translation>長方形選択</translation>
    </message>
    <message>
        <location line="+101"/>
        <source>Lasso</source>
        <translation>投げ縄</translation>
    </message>
    <message>
        <location line="+4085"/>
        <source>Smart object contents can&apos;t be painted. Rasterize the layer to edit its pixels.</source>
        <translation>スマートオブジェクトの内容には描画できません。ピクセルを編集するにはレイヤーをラスタライズしてください。</translation>
    </message>
    <message>
        <source>Shape layers can&apos;t be painted. Rasterize the layer to edit its pixels.</source>
        <translation>シェイプレイヤーには描画できません。ピクセルを編集するにはレイヤーをラスタライズしてください。</translation>
    </message>
    <message>
        <source>The Pen tool draws paths on layer content</source>
        <translation>ペンツールはレイヤーコンテンツにパスを描きます</translation>
    </message>
    <message>
        <source>Click to add points, drag for curves. Click the first point to close; Enter commits an open path; Esc cancels.</source>
        <translation>クリックでポイントを追加、ドラッグで曲線を描画。最初のポイントをクリックすると閉じ、Enter で開いたパスを確定、Esc でキャンセルします。</translation>
    </message>
    <message>
        <source>Select a shape layer or draw a path first</source>
        <translation>先にシェイプレイヤーを選択するかパスを描いてください</translation>
    </message>
    <message>
        <source>Move shape</source>
        <translation>シェイプを移動</translation>
    </message>
    <message>
        <source>Edit path</source>
        <translation>パスを編集</translation>
    </message>
    <message>
        <source>Delete anchors</source>
        <translation>アンカーポイントを削除</translation>
    </message>
    <message>
        <source>Nudge anchors</source>
        <translation>アンカーポイントを移動</translation>
    </message>
    <message>
        <source>Convert point</source>
        <translation>ポイントを切り替え</translation>
    </message>
    <message>
        <source>Delete anchor</source>
        <translation>アンカーポイントを削除</translation>
    </message>
    <message>
        <source>Add anchor</source>
        <translation>アンカーポイントを追加</translation>
    </message>
    <message>
        <source>Change shape combine mode</source>
        <translation>シェイプの結合モードを変更</translation>
    </message>
    <message>
        <source>Vector masks are edited with the pen and path tools</source>
        <translation>ベクトルマスクはペンツールとパスツールで編集します</translation>
    </message>
    <message>
        <source>Add to vector mask</source>
        <translation>ベクトルマスクに追加</translation>
    </message>
    <message>
        <source>This layer&apos;s vector data is preserved but can&apos;t be edited.</source>
        <translation>このレイヤーのベクトルデータは保持されていますが編集できません。</translation>
    </message>
    <message>
        <source>Shape layers and vector masks can&apos;t be transformed yet. Rasterize first.</source>
        <translation>シェイプレイヤーとベクトルマスクはまだ変形できません。先にラスタライズしてください。</translation>
    </message>
    <message>
        <source>Shape layers can&apos;t be warped. Convert to a smart object or rasterize first.</source>
        <translation>シェイプレイヤーはワープできません。スマートオブジェクトに変換するか、先にラスタライズしてください。</translation>
    </message>
    <message>
        <source>Shape layers and vector masks can&apos;t be warped. Convert to a smart object or rasterize first.</source>
        <translation>シェイプレイヤーとベクトルマスクはワープできません。スマートオブジェクトに変換するか、先にラスタライズしてください。</translation>
    </message>
    <message>
        <location line="+2304"/>
        <source>Magnetic Lasso</source>
        <translation>マグネット投げ縄</translation>
    </message>
    <message>
        <location line="+582"/>
        <source>Free Transform</source>
        <translation>自由変形</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Transformed layer</source>
        <translation>レイヤーを変形しました</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Free Transform cancelled</source>
        <translation>自由変形をキャンセルしました</translation>
    </message>
    <message>
        <location line="-9459"/>
        <location line="+5532"/>
        <source>New Guide</source>
        <translation>新規ガイド</translation>
    </message>
    <message>
        <location line="-5520"/>
        <source>Clear Guides</source>
        <translation>ガイドを消去</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+5520"/>
        <source>Clear Selected Guide</source>
        <translation>選択中のガイドを消去</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Move Guide</source>
        <translation>ガイドを移動</translation>
    </message>
    <message>
        <source>Inverted Quick Mask</source>
        <translation>クイックマスクを反転しました</translation>
    </message>
    <message>
        <source>This tool is unavailable in Quick Mask mode</source>
        <translation>このツールはクイックマスクモードでは使用できません</translation>
    </message>
    <message>
        <source>Select All is unavailable in Quick Mask mode</source>
        <translation>クイックマスクモードでは「すべてを選択」は使用できません</translation>
    </message>
    <message>
        <source>Could not rebuild the Smart Filter preview and cache</source>
        <translation>スマートフィルターのプレビューとキャッシュを再構築できませんでした</translation>
    </message>
    <message>
        <source>This tool is unavailable while editing a Smart Filter mask</source>
        <translation>スマートフィルターマスクの編集中はこのツールを使用できません</translation>
    </message>
    <message>
        <source>The Smart Filter mask is no longer available</source>
        <translation>スマートフィルターマスクは利用できなくなりました</translation>
    </message>
    <message>
        <source>Local adjustment brushes are unavailable while editing a grayscale channel</source>
        <translation>グレースケールチャンネルの編集中は部分補正ブラシを使用できません</translation>
    </message>
    <message><source>Dodge</source><translation>覆い焼き</translation></message>
    <message><source>Burn</source><translation>焼き込み</translation></message>
    <message><source>Sponge</source><translation>スポンジ</translation></message>
    <message><source>Blur brush</source><translation>ぼかしブラシ</translation></message>
    <message><source>Sharpen brush</source><translation>シャープブラシ</translation></message>
</context>
<context>
    <name>patchy::ui::ChannelPanel</name>
    <message>
        <location filename="../src/ui/channel_panel.cpp" line="+104"/>
        <source>Select for a grayscale view. Check to show a colored overlay.</source>
        <translation>選択するとグレースケールで表示します。チェックするとカラーオーバーレイを表示します。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Spot channels can be previewed but not edited.</source>
        <translation>スポットカラーチャンネルはプレビューできますが、編集はできません。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Show the normal composite image.</source>
        <translation>通常のコンポジット画像を表示します。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preview this component as grayscale. Component channels are read-only.</source>
        <translation>この色成分をグレースケールでプレビューします。色成分チャンネルは読み取り専用です。</translation>
    </message>
    <message>
        <location line="-9"/>
        <location line="+5"/>
        <location line="+6"/>
        <source>Ctrl-click to load this channel as a selection.</source>
        <translation>Ctrl+クリックでこのチャンネルを選択範囲として読み込みます。</translation>
    </message>
    <message>
        <source>New</source>
        <translation type="vanished">新規</translation>
    </message>
    <message>
        <location line="+244"/>
        <source>New alpha channel</source>
        <translation>新規アルファチャンネル</translation>
    </message>
    <message>
        <source>Save</source>
        <translation type="vanished">保存</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Save selection as channel</source>
        <translation>選択範囲をチャンネルとして保存</translation>
    </message>
    <message>
        <source>Load</source>
        <translation type="vanished">読み込み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Load channel as selection</source>
        <translation>チャンネルを選択範囲として読み込み</translation>
    </message>
    <message>
        <source>Rename</source>
        <translation type="vanished">名前を変更</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rename channel</source>
        <translation>チャンネル名を変更</translation>
    </message>
    <message>
        <source>Invert</source>
        <translation type="vanished">反転</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invert channel</source>
        <translation>チャンネルを反転</translation>
    </message>
    <message>
        <source>Delete</source>
        <translation type="vanished">削除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Delete channel</source>
        <translation>チャンネルを削除</translation>
    </message>
    <message>
        <source>Temporary selection mask. White selects, black masks, and gray creates partial selection.</source>
        <translation>一時的な選択マスクです。白で選択、黒でマスク、グレーで部分選択を作成します。</translation>
    </message>
</context>
<context>
    <name>patchy::ui::FontPickerCombo</name>
    <message>
        <location filename="../src/ui/font_picker.cpp" line="+295"/>
        <source>The quick brown fox jumps over the lazy dog. 0123456789</source>
        <extracomment>Latin sample text in the font preview; keep it Latin in every language (it demonstrates the font&apos;s Latin glyph coverage).</extracomment>
        <translation>The quick brown fox jumps over the lazy dog. 0123456789</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Also supports: %1</source>
        <translation>その他の対応スクリプト: %1</translation>
    </message>
    <message>
        <location line="+147"/>
        <source>Search fonts...</source>
        <translation>フォントを検索...</translation>
    </message>
</context>
<context>
    <name>patchy::ui::HotkeyEditorPanel</name>
    <message>
        <location filename="../src/ui/hotkey_editor.cpp" line="+175"/>
        <source>Search commands or shortcuts...</source>
        <translation>コマンドまたはショートカットを検索...</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Reset All</source>
        <translation>すべてリセット</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Restore every hotkey to its default</source>
        <translation>すべてのホットキーを既定に戻します</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Click a shortcut to change it. Backspace clears it. Esc cancels. Changes apply when you click OK.</source>
        <translation>ショートカットをクリックすると変更できます。Backspace でクリア、Esc でキャンセル。変更は OK をクリックすると適用されます。</translation>
    </message>
    <message>
        <location line="+265"/>
        <source>Built-in canvas keys (not editable)</source>
        <translation>キャンバスの組み込みキー（変更不可）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Pan canvas (hold); while dragging, moves the selection or shape</source>
        <translation>キャンバスをパン（長押し）。ドラッグ中は選択範囲やシェイプを移動</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Set tool opacity (10%-100%)</source>
        <translation>ツールの不透明度を設定 (10%-100%)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nudge layer by 1 px / 10 px</source>
        <translation>レイヤーを 1 px / 10 px 移動</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Commit / cancel a transform or text edit</source>
        <translation>変形やテキスト編集を確定 / キャンセル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Delete selected guides</source>
        <translation>選択したガイドを削除</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>%1 is assigned to %2.</source>
        <translation>%1 は %2 に割り当てられています。</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Reset to default (no shortcut)</source>
        <translation>既定に戻す（ショートカットなし）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reset to default (%1)</source>
        <translation>既定に戻す (%1)</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Click to change this shortcut</source>
        <translation>クリックしてこのショートカットを変更</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Click to assign</source>
        <translation>クリックして割り当て</translation>
    </message>
    <message>
        <location line="+48"/>
        <source>Reserved for canvas and dialog use</source>
        <translation>キャンバスとダイアログ用に予約されています</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Press a shortcut...</source>
        <translation>ショートカットを入力...</translation>
    </message>
    <message>
        <location line="+103"/>
        <source>%1 is already used by %2.</source>
        <translation>%1 は既に %2 で使用されています。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Use here instead</source>
        <translation>こちらに割り当てる</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="+60"/>
        <source>Tools</source>
        <translation>ツール</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Color</source>
        <translation>カラー</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Brush</source>
        <translation>ブラシ</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Channels</source>
        <translation>チャンネル</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Other</source>
        <translation>その他</translation>
    </message>
</context>
<context>
    <name>patchy::ui::MainWindow</name>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+13149"/>
        <source>Import Notes</source>
        <translation>読み込みに関する注意</translation>
    </message>
    <message>
        <location line="-4738"/>
        <source>I&amp;mport</source>
        <translation>読み込み(&amp;M)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>From &amp;Scanner or Camera...</source>
        <translation>スキャナーまたはカメラから(&amp;S)...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>From &amp;Scanner...</source>
        <translation>スキャナーから(&amp;S)...</translation>
    </message>
    <message>
        <location line="+4769"/>
        <location line="+5"/>
        <source>Import from Scanner</source>
        <translation>スキャナーから読み込み</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>No scanner or camera was found. Connect a WIA-compatible device and try again.</source>
        <translation>スキャナーまたはカメラが見つかりませんでした。WIA 対応デバイスを接続して再試行してください。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No scanner was found. Connect a scanner recognized by macOS and try again.</source>
        <translation>スキャナーが見つかりませんでした。macOS で認識されるスキャナーを接続して再試行してください。</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Scanned Image</source>
        <translation>スキャン画像</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Import from scanner</source>
        <translation>スキャナーから読み込み</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Imported image from scanner</source>
        <translation>スキャナーから画像を読み込みました</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+54"/>
        <source>Import failed</source>
        <translation>読み込みに失敗しました</translation>
    </message>
    <message>
        <location line="-4864"/>
        <source>Sprite Sheet to &amp;Layers...</source>
        <translation>スプライトシートをレイヤーへ(&amp;L)...</translation>
    </message>
    <message>
        <location line="+4823"/>
        <location line="+18"/>
        <source>Sprite Sheet to Layers</source>
        <translation>スプライトシートをレイヤーへ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No non-empty cells were found with these settings.</source>
        <translation>この設定では空でないセルが見つかりませんでした。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Sprite Frames</source>
        <translation>スプライト フレーム</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Import sprite sheet</source>
        <translation>スプライトシートの読み込み</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Imported %1 frames from %2</source>
        <translation>%2 から %1 フレームを読み込みました</translation>
    </message>
    <message>
        <location line="-4851"/>
        <source>Export Layers as Sprite S&amp;heet...</source>
        <translation>レイヤーをスプライトシートに書き出し(&amp;H)...</translation>
    </message>
    <message>
        <location line="+4873"/>
        <location line="+15"/>
        <source>Export Sprite Sheet</source>
        <translation>スプライトシートの書き出し</translation>
    </message>
    <message>
        <location line="-15"/>
        <source>There are no visible layers to export.</source>
        <translation>書き出せる表示レイヤーがありません。</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Exported sprite sheet %1</source>
        <translation>スプライトシート %1 を書き出しました</translation>
    </message>
    <message>
        <location line="-79"/>
        <source>Frame %1</source>
        <translation>フレーム %1</translation>
    </message>
    <message>
        <source>&amp;Image Sequence to Layers...</source>
        <translation>画像シーケンスをレイヤーへ(&amp;I)...</translation>
    </message>
    <message>
        <source>Export Layers as Image Se&amp;quence...</source>
        <translation>レイヤーを画像シーケンスとして書き出し(&amp;Q)...</translation>
    </message>
    <message>
        <source>Image Sequence to Layers</source>
        <translation>画像シーケンスをレイヤーへ</translation>
    </message>
    <message>
        <source>Image Sequence</source>
        <translation>画像シーケンス</translation>
    </message>
    <message>
        <source>Import image sequence</source>
        <translation>画像シーケンスの読み込み</translation>
    </message>
    <message>
        <source>Imported %1 images as layers</source>
        <translation>%1 個の画像をレイヤーとして読み込みました</translation>
    </message>
    <message>
        <source>Export Image Sequence</source>
        <translation>画像シーケンスの書き出し</translation>
    </message>
    <message>
        <source>There are no layers to export.</source>
        <translation>書き出せるレイヤーがありません。</translation>
    </message>
    <message>
        <source>%1 of %2 files already exist in this folder. Overwrite them?</source>
        <translation>このフォルダーには %2 個中 %1 個のファイルが既に存在します。上書きしますか?</translation>
    </message>
    <message>
        <source>Exported %1 images to %2</source>
        <translation>%1 個の画像を %2 に書き出しました</translation>
    </message>
    <message>
        <location line="-4146"/>
        <source>Seamless &amp;Tile Preview</source>
        <translation>シームレスタイル プレビュー(&amp;T)</translation>
    </message>
    <message>
        <location line="+4038"/>
        <source>%1 opened with notes:

%2</source>
        <translation>%1 を開きましたが、次の注意があります:

%2</translation>
    </message>
    <message>
        <location line="+618"/>
        <source>Development</source>
        <translation>開発</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The profiling stress test builds a large scripted scene to measure rendering performance. It closes all open documents and takes several minutes. Primarily a development tool.</source>
        <translation>プロファイリング ストレステストは、描画性能を測定するために大きなシーンをスクリプトで作成します。開いているドキュメントはすべて閉じられ、数分かかります。主に開発用のツールです。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Quick (1024 px)</source>
        <translation>クイック (1024 px)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Small (2048 px)</source>
        <translation>小 (2048 px)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Standard (4096 px)</source>
        <translation>標準 (4096 px)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Huge (8192 px, needs lots of RAM)</source>
        <translation>特大 (8192 px、大量のRAMが必要)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Stress test size:</source>
        <translation>ストレステストのサイズ:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Run Profiling Stress Test...</source>
        <translation>プロファイリング ストレステストを実行...</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_stress_test.cpp" line="+2154"/>
        <source>The profiling stress test closes all open documents, then builds a large scripted scene to measure performance. It takes several minutes; please leave the mouse and keyboard alone while it runs. This is primarily a development tool.</source>
        <translation>プロファイリング ストレステストは、開いているドキュメントをすべて閉じてから、性能測定用の大きなシーンをスクリプトで作成します。数分かかりますので、実行中はマウスとキーボードに触れないでください。これは主に開発用のツールです。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Warning: this is a DEBUG build - results will not reflect release performance.</source>
        <translation>警告: これはデバッグビルドです。結果はリリース版の性能を反映しません。</translation>
    </message>
    <message>
        <location line="-163"/>
        <location line="+166"/>
        <source>Profiling Stress Test</source>
        <translation>プロファイリング ストレステスト</translation>
    </message>
    <message>
        <location line="-15"/>
        <location line="+25"/>
        <source>Stress test cancelled</source>
        <translation>ストレステストをキャンセルしました</translation>
    </message>
    <message>
        <location line="-179"/>
        <source>Preparing stress test...</source>
        <translation>ストレステストを準備中...</translation>
    </message>
    <message>
        <location line="-1239"/>
        <source>Step %1 of %2: %3</source>
        <translation>ステップ %1 / %2: %3</translation>
    </message>
    <message>
        <location line="+1372"/>
        <source>Running stress test...</source>
        <translation>ストレステストを実行中...</translation>
    </message>
    <message>
        <location line="-1325"/>
        <source>Running stress test... (%1)</source>
        <translation>ストレステストを実行中... (%1)</translation>
    </message>
    <message>
        <location line="+1347"/>
        <source>Stress test complete</source>
        <translation>ストレステストが完了しました</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stress test failed</source>
        <translation>ストレステストが失敗しました</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Stress Test Results</source>
        <translation>ストレステストの結果</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rating: %1</source>
        <translation>レーティング: %1</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Rating: n/a</source>
        <translation>レーティング: なし</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Open Report Folder</source>
        <translation>レポートフォルダーを開く</translation>
    </message>
    <message>
        <location line="-958"/>
        <source>Stress test</source>
        <translation>ストレステスト</translation>
    </message>
    <message>
        <location line="+675"/>
        <source>Stress extra 1</source>
        <translation>ストレス追加 1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Stress extra 2</source>
        <translation>ストレス追加 2</translation>
    </message>
    <message>
        <location line="-657"/>
        <source>Texture base</source>
        <translation>テクスチャのベース</translation>
    </message>
    <message>
        <location line="+445"/>
        <source>Vignette base</source>
        <translation>ビネットのベース</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Grain base</source>
        <translation>粒子のベース</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+306"/>
        <source>Hotkeys</source>
        <translation>ホットキー</translation>
    </message>
    <message>
        <location line="-6882"/>
        <location line="+5676"/>
        <location line="+3"/>
        <source>New document</source>
        <translation>新規ドキュメント</translation>
    </message>
    <message>
        <location line="-5606"/>
        <location line="+1006"/>
        <source>Ready</source>
        <translation>準備完了</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>Minimize</source>
        <translation>最小化</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Maximize / Restore</source>
        <translation>最大化 / 元に戻す</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+4242"/>
        <source>Close</source>
        <translation>閉じる</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close Others</source>
        <translation>他を閉じる</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close All</source>
        <translation>すべて閉じる</translation>
    </message>
    <message>
        <location line="-4232"/>
        <source>&amp;File</source>
        <translation>ファイル(&amp;F)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Edit</source>
        <translation>編集(&amp;E)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Image</source>
        <translation>画像(&amp;I)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Layer</source>
        <translation>レイヤー(&amp;L)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Type</source>
        <translation>文字(&amp;T)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Select</source>
        <translation>選択範囲(&amp;S)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Filter</source>
        <translation>フィルター(&amp;F)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Plugins</source>
        <translation>プラグイン(&amp;P)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;View</source>
        <translation>表示(&amp;V)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Window</source>
        <translation>ウィンドウ(&amp;W)</translation>
    </message>
    <message>
        <location line="+960"/>
        <source>Force Refresh</source>
        <translation>強制再描画</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Set Screen Size</source>
        <translation>画面サイズを設定</translation>
    </message>
    <message>
        <location line="-939"/>
        <source>&amp;Help</source>
        <translation>ヘルプ(&amp;H)</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>&amp;New</source>
        <translation>新規(&amp;N)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Open...</source>
        <translation>開く(&amp;O)...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open &amp;Recent File</source>
        <translation>最近使ったファイル(&amp;R)</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>&amp;Save</source>
        <translation>保存(&amp;S)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Save &amp;As...</source>
        <translation>名前を付けて保存(&amp;A)...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Export &amp;Flat Image...</source>
        <translation>統合画像を書き出し(&amp;F)...</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Page Set&amp;up...</source>
        <translation>ページ設定(&amp;U)...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Print...</source>
        <translation>印刷(&amp;P)...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>&amp;Close</source>
        <translation>閉じる(&amp;C)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close &amp;All</source>
        <translation>すべて閉じる(&amp;A)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>&amp;Preferences...</source>
        <translation>環境設定(&amp;P)...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>&amp;Quit</source>
        <translation>終了(&amp;Q)</translation>
    </message>
    <message>
        <location line="+56"/>
        <source>&amp;Undo</source>
        <translation>取り消し(&amp;U)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Redo</source>
        <translation>やり直し(&amp;R)</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Cu&amp;t</source>
        <translation>切り取り(&amp;T)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Copy</source>
        <translation>コピー(&amp;C)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy Merged</source>
        <translation>結合部分をコピー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Paste</source>
        <translation>貼り付け(&amp;P)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Free &amp;Transform...</source>
        <translation>自由変形(&amp;T)...</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Select &amp;All</source>
        <translation>すべてを選択(&amp;A)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Clear Selection</source>
        <translation>選択を解除(&amp;C)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Reselect</source>
        <translation>再選択(&amp;R)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Inverse</source>
        <translation>選択範囲を反転(&amp;I)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Grow</source>
        <translation>拡張(&amp;G)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Simi&amp;lar</source>
        <translation>類似部分を選択(&amp;L)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Expand...</source>
        <translation>拡張(&amp;E)...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Con&amp;tract...</source>
        <translation>縮小(&amp;T)...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Border...</source>
        <translation>境界線(&amp;B)...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Load Layer &amp;Transparency</source>
        <translation>レイヤーの透明部分を読み込み(&amp;T)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Stroke Selection</source>
        <translation>選択範囲の境界線を描く(&amp;S)</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Select All</source>
        <translation>すべてを選択</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Deselect</source>
        <translation>選択を解除</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reselect</source>
        <translation>再選択</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Inverse Selection</source>
        <translation>選択範囲を反転</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Grow Selection</source>
        <translation>選択範囲を拡張</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Similar</source>
        <translation>類似部分を選択</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>&amp;New Layer</source>
        <translation>新規レイヤー(&amp;N)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>New &amp;Folder</source>
        <translation>新規フォルダー(&amp;F)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>New &amp;Adjustment Layer</source>
        <translation>新規調整レイヤー(&amp;A)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Layer Via &amp;Copy</source>
        <translation>コピーしてレイヤー化(&amp;C)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Layer Via Cu&amp;t</source>
        <translation>カットしてレイヤー化(&amp;T)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Layer &amp;Mask</source>
        <translation>レイヤーマスクを追加(&amp;M)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Edit Layer Mask</source>
        <translation>レイヤーマスクを編集(&amp;E)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Show Mask &amp;Overlay</source>
        <translation>マスクオーバーレイを表示(&amp;O)</translation>
    </message>
    <message>
        <location line="+223"/>
        <source>&amp;Mode</source>
        <translation>モード(&amp;M)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>&amp;RGB Color</source>
        <translation>RGB カラー(&amp;R)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>&amp;Indexed (Palette)...</source>
        <translation>インデックスカラー (パレット)(&amp;I)...</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Snap &amp;Layer to Palette</source>
        <translation>レイヤーをパレットにスナップ(&amp;L)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Snap Image to &amp;Palette</source>
        <translation>画像をパレットにスナップ(&amp;P)</translation>
    </message>
    <message>
        <location line="+1939"/>
        <source>Warp...</source>
        <translation>ワープ...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Warp Text (Photoshop-style styles: arc, flag, fish, ...)</source>
        <translation>ワープテキスト（円弧・旗・魚形などのスタイル）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Character...</source>
        <translation>文字...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Character panel (leading, tracking, glyph scales)</source>
        <translation>文字パネル（行送り・トラッキング・字形の比率）</translation>
    </message>
    <message>
        <location line="+527"/>
        <source>Channels</source>
        <translation>チャンネル</translation>
    </message>
    <message>
        <location line="+28"/>
        <location filename="../src/ui/main_window_channels.cpp" line="+457"/>
        <source>New Channel</source>
        <translation>新規チャンネル</translation>
    </message>
    <message>
        <location line="+2"/>
        <location filename="../src/ui/main_window_channels.cpp" line="+22"/>
        <source>Save Selection as Channel</source>
        <translation>選択範囲をチャンネルとして保存</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Load Channel as Selection</source>
        <translation>チャンネルを選択範囲として読み込み</translation>
    </message>
    <message>
        <location line="+3"/>
        <location filename="../src/ui/main_window_channels.cpp" line="+98"/>
        <source>Rename Channel</source>
        <translation>チャンネル名を変更</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Invert Channel</source>
        <translation>チャンネルを反転</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Delete Channel</source>
        <translation>チャンネルを削除</translation>
    </message>
    <message>
        <location line="+2071"/>
        <source>Saved Channels Will Be Discarded</source>
        <translation>保存済みチャンネルは破棄されます</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This file format cannot store saved channels. Continue saving and discard them?</source>
        <translation>このファイル形式では保存済みチャンネルを保存できません。チャンネルを破棄して保存を続けますか？</translation>
    </message>
    <message>
        <location line="-24"/>
        <source>This file format cannot store layers. Continue saving and flatten the linked file?</source>
        <translation>このファイル形式ではレイヤーを保存できません。リンクされたファイルを統合して保存を続けますか？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This file format cannot store layers, so Patchy will save a flattened copy. The open document will keep its layers and unsaved changes. To keep layers in the file, save as a Photoshop document (.psd) instead.</source>
        <translation>このファイル形式ではレイヤーを保存できないため、Patchy は統合したコピーを保存します。開いているドキュメントのレイヤーと未保存の変更はそのまま残ります。レイヤーをファイルに残すには、Photoshop ドキュメント (.psd) として保存してください。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Layers Will Be Flattened</source>
        <translation>レイヤーは統合されます</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>Saved flattened copy %1</source>
        <translation>統合したコピー %1 を保存しました</translation>
    </message>
    <message>
        <source>SVG keeps shape layers as vectors, but masks, layer styles, text, and adjustments are baked into images, so Patchy will save a copy. The open document will keep its layers and unsaved changes. To keep everything editable, save as a Photoshop document (.psd) instead.</source>
        <translation>SVG はシェイプレイヤーをベクターのまま保存しますが、マスク、レイヤースタイル、テキスト、調整レイヤーは画像として書き出されるため、Patchy はコピーを保存します。開いているドキュメントのレイヤーと未保存の変更はそのまま残ります。すべてを編集可能なまま残すには、Photoshop ドキュメント (.psd) として保存してください。</translation>
    </message>
    <message>
        <source>Saved SVG copy %1.</source>
        <translation>SVG のコピー %1 を保存しました。</translation>
    </message>
    <message numerus="yes">
        <source> (+%n more export note(s))</source>
        <translation>
            <numerusform> (ほか %n 件のエクスポートノート)</numerusform>
        </translation>
    </message>
    <message>
        <source>Define Custom Shape from SVG File</source>
        <translation>SVG ファイルからカスタムシェイプを定義</translation>
    </message>
    <message>
        <source>SVG Files (*.svg *.svgz);;All Files (*.*)</source>
        <translation>SVG ファイル (*.svg *.svgz);;すべてのファイル (*.*)</translation>
    </message>
    <message>
        <source>Could not read the SVG: %1</source>
        <translation>SVG を読み込めませんでした: %1</translation>
    </message>
    <message>
        <source>The SVG has no shape geometry to define</source>
        <translation>この SVG には定義できるシェイプのジオメトリがありません</translation>
    </message>
    <message>
        <source>Defined custom shape %1 from the SVG</source>
        <translation>SVG からカスタムシェイプ %1 を定義しました</translation>
    </message>
    <message>
        <source>SVG Shape</source>
        <translation>SVG シェイプ</translation>
    </message>
    <message>
        <source>Paste shape</source>
        <translation>シェイプをペースト</translation>
    </message>
    <message numerus="yes">
        <source>Pasted %n SVG shape layer(s)</source>
        <translation>
            <numerusform>SVG シェイプレイヤーを %n 枚ペーストしました</numerusform>
        </translation>
    </message>
    <message>
        <location line="+963"/>
        <location filename="../src/ui/main_window_adjustments.cpp" line="+415"/>
        <source>Filters are unavailable while viewing a document channel</source>
        <translation>ドキュメントチャンネルの表示中はフィルターを使用できません</translation>
    </message>
    <message>
        <location line="+1630"/>
        <source>Character</source>
        <translation>文字</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Click in text with the Type tool to edit these settings.</source>
        <translation>文字ツールでテキストをクリックすると、これらの設定を編集できます。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Auto leading</source>
        <translation>自動行送り</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Leading:</source>
        <translation>行送り:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Tracking:</source>
        <translation>トラッキング:</translation>
    </message>
    <message>
        <location line="-2"/>
        <source>Space between characters, in 1/1000 em (Photoshop tracking)</source>
        <translation>文字間隔（1/1000 em 単位、Photoshop のトラッキング）</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Horizontal scale:</source>
        <translation>水平比率:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Vertical scale:</source>
        <translation>垂直比率:</translation>
    </message>
    <message>
        <location line="-9"/>
        <location line="+7"/>
        <source> %</source>
        <translation> %</translation>
    </message>
    <message>
        <location line="-4646"/>
        <source>Palette</source>
        <translation>パレット</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Foreground: palette index %1 (%2)</source>
        <translation>描画色: パレットインデックス %1 (%2)</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Set palette</source>
        <translation>パレットを設定</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Palette set to %1</source>
        <translation>パレットを %1 に設定しました</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_palette.cpp" line="+360"/>
        <source>Edit Palette Color</source>
        <translation>パレットカラーを編集</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Remap existing pixels</source>
        <translation>既存のピクセルを再マップ</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+15"/>
        <location filename="../src/ui/main_window_palette.cpp" line="+16"/>
        <source>Edit palette entry</source>
        <translation>パレットカラーの編集</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_palette.cpp" line="+1"/>
        <source>Palette color updated</source>
        <translation>パレットカラーを更新しました</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>Swap palette colors</source>
        <translation>パレットカラーを入れ替え</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Swapped palette indexes %1 and %2</source>
        <translation>パレットインデックス %1 と %2 を入れ替えました</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="-33"/>
        <location filename="../src/ui/main_window_palette.cpp" line="+13"/>
        <source>Copied palette color %1</source>
        <translation>パレットカラー %1 をコピーしました</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_palette.cpp" line="+20"/>
        <source>The clipboard does not contain a color (expected #RRGGBB)</source>
        <translation>クリップボードに色がありません (#RRGGBB 形式が必要です)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Paste palette color</source>
        <translation>パレットカラーを貼り付け</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pasted %1 into palette index %2</source>
        <translation>%1 をパレットインデックス %2 に貼り付けました</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>The foreground color is already in the palette</source>
        <translation>描画色はすでにパレットにあります</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The palette is full (256 colors)</source>
        <translation>パレットは上限です (256 色)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Add palette color</source>
        <translation>パレットカラーを追加</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Added the foreground color to the palette</source>
        <translation>描画色をパレットに追加しました</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>A palette needs at least one color</source>
        <translation>パレットには少なくとも 1 色必要です</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Remove palette color</source>
        <translation>パレットカラーを削除</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Removed the palette color</source>
        <translation>パレットカラーを削除しました</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>The image has more than 256 colors. Use Image &gt; Mode &gt; Indexed (Palette) to optimize it down.</source>
        <translation>画像の色数が 256 を超えています。イメージ &gt; モード &gt; インデックスカラー (パレット) で色数を最適化してください。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Extract palette</source>
        <translation>パレットを抽出</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>Extracted %n color(s) from the image</source>
        <translation>
            <numerusform>画像から %n 色を抽出しました</numerusform>
        </translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Load palette</source>
        <translation>パレットを読み込む</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Loaded palette %1</source>
        <translation>パレット %1 を読み込みました</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Saved palette %1</source>
        <translation>パレット %1 を保存しました</translation>
    </message>
    <message>
        <location line="+89"/>
        <location line="+20"/>
        <source>Convert to Indexed (Palette)</source>
        <translation>インデックス (パレット) に変換</translation>
    </message>
    <message numerus="yes">
        <location line="+26"/>
        <source>Converted to indexed palette editing (%n color(s))</source>
        <translation>
            <numerusform>インデックスパレット編集に変換しました (%n 色)</numerusform>
        </translation>
    </message>
    <message>
        <location line="+24"/>
        <location line="+18"/>
        <source>Convert to RGB Color</source>
        <translation>RGB カラーに変換</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Converted to RGB color; pixels are unchanged</source>
        <translation>RGB カラーに変換しました。ピクセルは変更されていません</translation>
    </message>
    <message>
        <location line="-38"/>
        <source>Some layers contain colors outside the palette, so the canvas shows them snapped to it (filters, adjustments, pasting, and text can cause this).</source>
        <translation>一部のレイヤーにパレット外の色が含まれているため、キャンバスではパレットにスナップして表示しています (フィルター、色調補正、貼り付け、テキストなどが原因になります)。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Keep the palettized look by making those snapped colors permanent, or restore the layers&apos; original colors?</source>
        <translation>スナップされた色を確定してパレット化された見た目を維持しますか？それともレイヤーの元の色に戻しますか？</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keep Palettized Look</source>
        <translation>パレットの見た目を維持</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Restore Original Colors</source>
        <translation>元の色に戻す</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Converted to RGB color; the palettized look was kept</source>
        <translation>RGB カラーに変換しました。パレット化された見た目を維持しています</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Snap layer to palette</source>
        <translation>レイヤーをパレットにスナップ</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Snap image to palette</source>
        <translation>画像をパレットにスナップ</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Layer snapped to the palette</source>
        <translation>レイヤーをパレットにスナップしました</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Image snapped to the palette</source>
        <translation>画像をパレットにスナップしました</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Indexed Image</source>
        <translation>インデックス画像</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>This image uses a %n-color palette.</source>
        <translation>
            <numerusform>この画像は %n 色のパレットを使用しています。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keep editing with the palette? Painting will snap to its colors; you can switch back any time with Image &gt; Mode &gt; RGB Color.</source>
        <translation>このパレットで編集を続けますか？描画はパレットの色にスナップされます。イメージ &gt; モード &gt; RGB カラーでいつでも戻せます。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use Palette</source>
        <translation>パレットを使用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Edit as RGB</source>
        <translation>RGB として編集</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Do this for every indexed image</source>
        <translation>すべてのインデックス画像でこの選択を使う</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Editing with the image&apos;s palette</source>
        <translation>画像のパレットで編集しています</translation>
    </message>
    <message numerus="yes">
        <location line="+47"/>
        <source>Palette: %n color(s)</source>
        <translation>
            <numerusform>パレット: %n 色</numerusform>
        </translation>
    </message>
    <message>
        <location line="+2"/>
        <source> (off-palette)</source>
        <translation> (パレット外)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Painting is constrained to the document palette. Click to show the Palette panel.</source>
        <translation>描画はドキュメントのパレットに制限されています。クリックでパレットパネルを表示します。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Some layers contain colors outside the palette (filters, layer styles, or text can cause this). Use Image &gt; Snap Image to Palette to fix them. Click to show the Palette panel.</source>
        <translation>パレット外の色を含むレイヤーがあります (フィルター、レイヤースタイル、テキストなどが原因になります)。イメージ &gt; 画像をパレットにスナップで修正できます。クリックでパレットパネルを表示します。</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+1318"/>
        <source>Clipboard Image</source>
        <translation>クリップボード画像</translation>
    </message>
    <message>
        <location line="+885"/>
        <source>Ask every time</source>
        <translation>毎回確認する</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Always use the palette</source>
        <translation>常にパレットを使用する</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Always edit as RGB</source>
        <translation>常に RGB として編集する</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Opening indexed images:</source>
        <translation>インデックス画像を開くとき:</translation>
    </message>
    <message>
        <location line="+5009"/>
        <source>Show Mask Overlay</source>
        <translation>マスクオーバーレイを表示</translation>
    </message>
    <message>
        <location line="-2106"/>
        <source>Mask overlay shown. Red marks the areas the mask hides.</source>
        <translation>マスクオーバーレイを表示しました。赤い部分はマスクで隠れる領域です。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Mask overlay hidden</source>
        <translation>マスクオーバーレイを非表示にしました</translation>
    </message>
    <message>
        <location line="-5529"/>
        <location line="+5548"/>
        <source>Showing the layer mask. Alt-click the mask thumbnail to return.</source>
        <translation>レイヤーマスクを表示しています。マスクサムネールをAltクリックすると戻ります。</translation>
    </message>
    <message>
        <location line="-8036"/>
        <source>&amp;Delete Layer Mask</source>
        <translation>レイヤーマスクを削除(&amp;D)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Link Layer &amp;Mask</source>
        <translation>レイヤーマスクをリンク(&amp;M)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Disable Layer Mask</source>
        <translation>レイヤーマスクを無効化(&amp;D)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Invert Layer Mask</source>
        <translation>レイヤーマスクを反転(&amp;I)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Apply Layer Mask</source>
        <translation>レイヤーマスクを適用(&amp;A)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>&amp;Edit Adjustment...</source>
        <translation>調整を編集(&amp;E)...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Edit Layer &amp;Styles...</source>
        <translation>レイヤースタイルを編集(&amp;S)...</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>&amp;Duplicate Layer</source>
        <translation>レイヤーを複製(&amp;D)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Merge &amp;Visible to New Layer</source>
        <translation>表示レイヤーを新規レイヤーに結合(&amp;V)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Merge &amp;Down</source>
        <translation>下のレイヤーと結合(&amp;D)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>&amp;Rename Layer...</source>
        <translation>レイヤー名を変更(&amp;R)...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Delete Layer</source>
        <translation>レイヤーを削除(&amp;D)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>&amp;Fill Layer / Selection</source>
        <translation>レイヤー / 選択範囲を塗りつぶし(&amp;F)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fill With &amp;Background Color</source>
        <translation>背景色で塗りつぶし(&amp;B)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Clear Layer / Selection</source>
        <translation>レイヤー / 選択範囲を消去(&amp;C)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Flip Layer &amp;Horizontal</source>
        <translation>レイヤーを水平方向に反転(&amp;H)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Flip Layer &amp;Vertical</source>
        <translation>レイヤーを垂直方向に反転(&amp;V)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Move Layer &amp;Up</source>
        <translation>レイヤーを上へ(&amp;U)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Move Layer &amp;Down</source>
        <translation>レイヤーを下へ(&amp;D)</translation>
    </message>
    <message>
        <location line="+152"/>
        <source>Fill background</source>
        <translation>背景を塗りつぶし</translation>
    </message>
    <message>
        <location line="+47"/>
        <source>&amp;Adjustments</source>
        <translation>色調補正(&amp;A)</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>&amp;Invert</source>
        <translation>反転(&amp;I)</translation>
    </message>
    <message>
        <source>Invert</source>
        <translation>反転</translation>
    </message>
    <message>
        <source>Invert has no settings to edit</source>
        <translation>反転には編集できる設定がありません</translation>
    </message>
    <message>
        <source>&amp;Posterize...</source>
        <translation>ポスタリゼーション(&amp;P)...</translation>
    </message>
    <message>
        <source>&amp;Threshold...</source>
        <translation>しきい値(&amp;T)...</translation>
    </message>
    <message>
        <source>Posterize</source>
        <translation>ポスタリゼーション</translation>
    </message>
    <message>
        <source>Threshold</source>
        <translation>しきい値</translation>
    </message>
    <message>
        <source>Cancelled Posterize</source>
        <translation>ポスタリゼーションをキャンセルしました</translation>
    </message>
    <message>
        <source>Cancelled Threshold</source>
        <translation>しきい値をキャンセルしました</translation>
    </message>
    <message>
        <source>Brightness/Contrast...</source>
        <translation>明るさ・コントラスト...</translation>
    </message>
    <message>
        <source>Brightness/Contrast</source>
        <translation>明るさ・コントラスト</translation>
    </message>
    <message>
        <source>Cancelled Brightness/Contrast</source>
        <translation>明るさ・コントラストをキャンセルしました</translation>
    </message>
    <message>
        <location line="+2"/>
        <location filename="../src/ui/main_window_adjustments.cpp" line="+210"/>
        <source>&amp;Levels...</source>
        <translation>レベル補正(&amp;L)...</translation>
    </message>
    <message>
        <location line="+6"/>
        <location filename="../src/ui/main_window_adjustments.cpp" line="+2"/>
        <source>&amp;Curves...</source>
        <translation>トーンカーブ(&amp;C)...</translation>
    </message>
    <message>
        <location line="+6"/>
        <location filename="../src/ui/main_window_adjustments.cpp" line="+2"/>
        <source>&amp;Hue/Saturation...</source>
        <translation>色相・彩度(&amp;H)...</translation>
    </message>
    <message>
        <location line="+6"/>
        <location filename="../src/ui/main_window_adjustments.cpp" line="+2"/>
        <source>Color &amp;Balance...</source>
        <translation>カラーバランス(&amp;B)...</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>&amp;Desaturate</source>
        <translation>彩度を下げる(&amp;D)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Auto &amp;Contrast</source>
        <translation>自動コントラスト(&amp;C)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>&amp;Brightness/Contrast...</source>
        <translation>明るさ/コントラスト(&amp;B)...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>&amp;Threshold</source>
        <translation>しきい値(&amp;T)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>&amp;Posterize</source>
        <translation>ポスタリゼーション(&amp;P)</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="11176"/>
        <source>Filter &amp;Gallery...</source>
        <translation>フィルターギャラリー(&amp;G)...</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="11180"/>
        <source>Preview and apply visual filters and photo looks</source>
        <translation>ビジュアルフィルターとフォトルックをプレビューして適用</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>&amp;Image Size...</source>
        <translation>画像サイズ(&amp;I)...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>&amp;Canvas Size...</source>
        <translation>キャンバスサイズ(&amp;C)...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>&amp;Crop to Selection</source>
        <translation>選択範囲で切り抜き(&amp;C)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Rotate 90 &amp;Clockwise</source>
        <translation>90 度時計回りに回転(&amp;C)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rotate 90 Counterclockwise</source>
        <translation>90 度反時計回りに回転</translation>
    </message>
    <message>
        <location line="+92"/>
        <location line="+14"/>
        <source>Apply %1 to the active layer</source>
        <translation>%1 をアクティブレイヤーに適用</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>&amp;Scan Legacy Photoshop Plug-ins...</source>
        <translation>従来の Photoshop プラグインをスキャン(&amp;S)...</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Legacy 8BF plug-ins run on Windows only</source>
        <translation>レガシー 8BF プラグインは Windows でのみ動作します</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+5309"/>
        <source>Legacy Photoshop Plug-ins</source>
        <translation>従来の Photoshop プラグイン</translation>
    </message>
    <message>
        <location line="-5306"/>
        <source>Zoom &amp;In</source>
        <translation>ズームイン(&amp;I)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Zoom &amp;Out</source>
        <translation>ズームアウト(&amp;O)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Fit on Screen</source>
        <translation>画面に合わせる(&amp;F)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Actual Pixels</source>
        <translation>実際のピクセル(&amp;A)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Show Selection &amp;Edges</source>
        <translation>選択範囲の境界を表示(&amp;E)</translation>
    </message>
    <message>
        <location line="+168"/>
        <source>&amp;English</source>
        <translation>英語(&amp;E)</translation>
    </message>
    <message>
        <location line="+96"/>
        <source>&amp;About Patchy</source>
        <translation>Patchy について(&amp;A)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Tool Palette</source>
        <translation>ツールパレット</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Move</source>
        <translation>移動</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Marquee Tools</source>
        <translation>選択ツール</translation>
    </message>
    <message>
        <location line="+72"/>
        <source>Wand Tools</source>
        <translation>自動選択ツール</translation>
    </message>
    <message>
        <location line="-36"/>
        <source>Lasso Tools</source>
        <translation>投げ縄ツール</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Magnetic Lasso</source>
        <translation>マグネット投げ縄</translation>
    </message>
    <message>
        <location line="+1062"/>
        <source>Contrast:</source>
        <translation>コントラスト:</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Frequency:</source>
        <translation>頻度:</translation>
    </message>
    <message>
        <location line="-28"/>
        <source>Edge search width in document pixels — press [ or ]</source>
        <translation>エッジ検索幅（ドキュメントのピクセル単位）— [ または ] キーで調整</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Minimum edge contrast the trace snaps to</source>
        <translation>トレースが吸着する最小エッジコントラスト</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>How often anchor points are placed while tracing</source>
        <translation>トレース中にアンカーポイントを置く頻度</translation>
    </message>
    <message>
        <location line="+10429"/>
        <source>Width: %1 px | Contrast: %2% | Frequency: %3</source>
        <translation>幅: %1 px | コントラスト: %2% | 頻度: %3</translation>
    </message>
    <message>
        <location line="-11476"/>
        <source>Quick Select</source>
        <translation>クイック選択</translation>
    </message>
    <message>
        <location line="+995"/>
        <source>Enhance Edge</source>
        <translation>エッジを強調</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Smooth the selection boundary after each stroke</source>
        <translation>ストロークごとに選択範囲の境界を滑らかにします</translation>
    </message>
    <message>
        <location line="-30"/>
        <source>Quick Select brush size — press [ or ]</source>
        <translation>クイック選択のブラシサイズ — [ または ] キーで変更</translation>
    </message>
    <message>
        <location line="+10503"/>
        <source>Size: %1 px | %2 | %3</source>
        <translation>サイズ: %1 px | %2 | %3</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>enhance edge</source>
        <translation>エッジ強調</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>raw edge</source>
        <translation>エッジ強調なし</translation>
    </message>
    <message>
        <location line="-11548"/>
        <source>Marquee</source>
        <translation>長方形選択</translation>
    </message>
    <message>
        <location line="-971"/>
        <source>Place &amp;Embedded...</source>
        <translation>埋め込みを配置(&amp;E)...</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>Warp Transform</source>
        <translation>ワープ変形</translation>
    </message>
    <message>
        <location line="+144"/>
        <location line="+9418"/>
        <source>Convert to Smart Object</source>
        <translation>スマートオブジェクトに変換</translation>
    </message>
    <message>
        <location line="-9417"/>
        <location line="+8808"/>
        <source>Edit Smart Object Contents</source>
        <translation>スマートオブジェクトの内容を編集</translation>
    </message>
    <message>
        <location line="-8807"/>
        <source>Replace Smart Object Contents...</source>
        <translation>スマートオブジェクトの内容を置き換え...</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+9534"/>
        <source>New Smart Object via Copy</source>
        <translation>コピーして新しいスマートオブジェクトを作成</translation>
    </message>
    <message>
        <location line="-9533"/>
        <location line="+8944"/>
        <location line="+50"/>
        <source>Update Smart Object Content</source>
        <translation>スマートオブジェクトのコンテンツを更新</translation>
    </message>
    <message>
        <location line="-8993"/>
        <source>Relink to File...</source>
        <translation>ファイルに再リンク...</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+9186"/>
        <source>Embed Linked Smart Object</source>
        <translation>リンクされたスマートオブジェクトを埋め込み</translation>
    </message>
    <message>
        <location line="-9183"/>
        <source>Convert to Normal Layer (Rasterize)</source>
        <translation>通常レイヤーに変換（ラスタライズ）</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+10021"/>
        <source>Smart Objects</source>
        <translation>スマートオブジェクト</translation>
    </message>
    <message>
        <location line="-9412"/>
        <source>Float in &amp;Window</source>
        <translation>ウィンドウに分離(&amp;W)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>&amp;Dock to Tabs</source>
        <translation>タブにドッキング(&amp;D)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Float A&amp;ll in Windows</source>
        <translation>すべてをウィンドウに分離(&amp;L)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>&amp;Consolidate All to Tabs</source>
        <translation>すべてをタブに統合(&amp;C)</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>&amp;Tile</source>
        <translation>並べて表示(&amp;T)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Ca&amp;scade</source>
        <translation>重ねて表示(&amp;S)</translation>
    </message>
    <message>
        <location line="+78"/>
        <source>Elliptical Marquee</source>
        <translation>楕円形選択</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Lasso</source>
        <translation>投げ縄</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Magic Wand</source>
        <translation>自動選択</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Brush</source>
        <translation>ブラシ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Clone</source>
        <translation>クローン</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_actions.cpp" line="1799"/>
        <source>Healing Brush</source>
        <translation>修復ブラシ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Smudge</source>
        <translation>指先</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Eraser</source>
        <translation>消しゴム</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Gradient</source>
        <translation>グラデーション</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+1119"/>
        <location line="+8464"/>
        <source>Fill</source>
        <translation>塗りつぶし</translation>
    </message>
    <message>
        <location line="-9582"/>
        <source>Shape Tools</source>
        <translation>図形ツール</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Line</source>
        <translation>直線</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+11479"/>
        <source>Rect</source>
        <translation>長方形</translation>
    </message>
    <message>
        <location line="-11477"/>
        <source>Ellipse</source>
        <translation>楕円</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Pick</source>
        <translation>スポイト</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+5648"/>
        <location line="+91"/>
        <location line="+22"/>
        <location line="+448"/>
        <source>Type</source>
        <translation>文字</translation>
    </message>
    <message>
        <location line="-6208"/>
        <source>Hand</source>
        <translation>手のひら</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Zoom</source>
        <translation>ズーム</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Actual Pixels</source>
        <translation>実際のピクセル</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Default Colors</source>
        <translation>既定の色</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Swap Colors</source>
        <translation>色を入れ替え</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+11532"/>
        <source>FG</source>
        <translation>前</translation>
    </message>
    <message>
        <location line="-11531"/>
        <location line="+11536"/>
        <source>BG</source>
        <translation>背</translation>
    </message>
    <message>
        <location line="-11533"/>
        <source>Foreground color</source>
        <translation>描画色</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Background color</source>
        <translation>背景色</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Options</source>
        <translation>オプション</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Auto-Select</source>
        <translation>自動選択</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Automatically select the clicked layer while using Move</source>
        <translation>移動ツール使用時にクリックしたレイヤーを自動選択します</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Show Transform Controls</source>
        <translation>変形コントロールを表示</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Show transform controls when selecting a layer with Move</source>
        <translation>移動ツールでレイヤーを選択したときに変形コントロールを表示します</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Reference point</source>
        <translation>基準点</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Top Left</source>
        <translation>左上</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Top</source>
        <translation>上</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Top Right</source>
        <translation>右上</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Left</source>
        <translation>左</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Center</source>
        <translation>中央</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Right</source>
        <translation>右</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bottom Left</source>
        <translation>左下</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bottom</source>
        <translation>下</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bottom Right</source>
        <translation>右下</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Reference X position</source>
        <translation>基準点の X 位置</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Reference Y position</source>
        <translation>基準点の Y 位置</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Horizontal scale</source>
        <translation>水平方向の拡大率</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Link horizontal and vertical scale</source>
        <translation>水平方向と垂直方向の拡大率をリンクします</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Vertical scale</source>
        <translation>垂直方向の拡大率</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Rotation angle</source>
        <translation>回転角度</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Interpolation</source>
        <translation>補間</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Nearest Neighbor</source>
        <translation>ニアレストネイバー</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Bilinear</source>
        <translation>バイリニア</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Bicubic</source>
        <translation>バイキュービック</translation>
    </message>
    <message>
        <location line="+67"/>
        <source>Arc Lower</source>
        <translation>下弦</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Arc Upper</source>
        <translation>上弦</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Shell Lower</source>
        <translation>貝殻（下向き）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shell Upper</source>
        <translation>貝殻（上向き）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Fish</source>
        <translation>魚形</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Fisheye</source>
        <translation>魚眼レンズ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Inflate</source>
        <translation>膨張</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Squeeze</source>
        <translation>絞り込み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Twist</source>
        <translation>旋回</translation>
    </message>
    <message>
        <location line="+60"/>
        <source>Apply transform</source>
        <translation>変形を適用</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Cancel transform</source>
        <translation>変形をキャンセル</translation>
    </message>
    <message>
        <location line="+934"/>
        <source>Apply text edit</source>
        <translation>テキスト編集を適用</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Cancel text edit</source>
        <translation>テキスト編集をキャンセル</translation>
    </message>
    <message>
        <location line="-1036"/>
        <source>Warp style</source>
        <translation>ワープスタイル</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Custom</source>
        <translation>カスタム</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Arc</source>
        <translation>円弧</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Arch</source>
        <translation>アーチ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bulge</source>
        <translation>でこぼこ</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Flag</source>
        <translation>旗</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Wave</source>
        <translation>波形</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rise</source>
        <translation>上昇</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Warp bend</source>
        <translation>ワープのカーブ</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Switch between free transform and warp</source>
        <translation>自由変形とワープを切り替え</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>New Selection</source>
        <translation>新規選択</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Add to Selection</source>
        <translation>選択範囲に追加</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Subtract from Selection</source>
        <translation>選択範囲から削除</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Intersect Selection</source>
        <translation>選択範囲との共通範囲</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Feather:</source>
        <translation>ぼかし:</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Anti-alias</source>
        <translation>アンチエイリアス</translation>
    </message>
    <message>
        <location line="+36"/>
        <location line="+550"/>
        <source>Style:</source>
        <translation>スタイル:</translation>
    </message>
    <message>
        <location line="-547"/>
        <location line="+1"/>
        <location line="+549"/>
        <location line="+1"/>
        <source>Normal</source>
        <translation>通常</translation>
    </message>
    <message>
        <location line="-551"/>
        <location line="+550"/>
        <source>Fixed Ratio</source>
        <translation>縦横比固定</translation>
    </message>
    <message>
        <location line="-550"/>
        <location line="+550"/>
        <source>Fixed Size</source>
        <translation>サイズ固定</translation>
    </message>
    <message>
        <location line="-536"/>
        <location line="+372"/>
        <location line="+178"/>
        <source>Width:</source>
        <translation>幅:</translation>
    </message>
    <message>
        <location line="-542"/>
        <location line="+550"/>
        <source>Height:</source>
        <translation>高さ:</translation>
    </message>
    <message>
        <location line="-511"/>
        <source>Preset:</source>
        <translation>プリセット:</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+258"/>
        <location line="+329"/>
        <source>Size:</source>
        <translation>サイズ:</translation>
    </message>
    <message>
        <location line="-572"/>
        <source>Brush size — press [ or ], or Alt+Right-drag on the canvas</source>
        <translation>ブラシサイズ — [ / ] キー、またはキャンバス上で Alt+右ドラッグ</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Brush opacity — press number keys (5 = 50%, 0 = 100%)</source>
        <translation>ブラシの不透明度 — 数字キーで設定 (5 = 50%、0 = 100%)</translation>
    </message>
    <message>
        <source>Flow:</source>
        <translation>フロー:</translation>
    </message>
    <message>
        <source>Brush flow - Shift+number keys (number keys with Airbrush)</source>
        <translation>ブラシのフロー — Shift+数字キーで設定 (エアブラシ時は数字キー)</translation>
    </message>
    <message>
        <source>Wet:</source>
        <translation>ウェット:</translation>
    </message>
    <message>
        <source>Load:</source>
        <translation>絵の具量:</translation>
    </message>
    <message>
        <source>Mix:</source>
        <translation>ミックス:</translation>
    </message>
    <message>
        <source>Airbrush</source>
        <translation>エアブラシ</translation>
    </message>
    <message>
        <source>Build paint while the pointer is held still</source>
        <translation>ポインターを静止したまま押し続けると塗料が蓄積します</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Brush edge softness — Alt+Right-drag up or down on the canvas</source>
        <translation>ブラシエッジの柔らかさ — キャンバス上で Alt+右ドラッグ（上下）</translation>
    </message>
    <message>
        <location line="-36"/>
        <location line="+169"/>
        <location line="+346"/>
        <source>Opacity:</source>
        <translation>不透明度:</translation>
    </message>
    <message>
        <location line="-363"/>
        <source>Method:</source>
        <translation>方式:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Linear</source>
        <translation>線形</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Radial</source>
        <translation>放射状</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Gradient opacity</source>
        <translation>グラデーションの不透明度</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Reverse</source>
        <translation>反転</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+10804"/>
        <source>Gradient preview</source>
        <translation>グラデーションのプレビュー</translation>
    </message>
    <message>
        <location line="-10801"/>
        <source>Edit Stops...</source>
        <translation>ストップを編集...</translation>
    </message>
    <message>
        <location line="-174"/>
        <location line="+510"/>
        <source>Soft:</source>
        <translation>柔らかさ:</translation>
    </message>
    <message>
        <location line="-643"/>
        <location line="+546"/>
        <source>Radius:</source>
        <translation>半径:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Rounded-corner radius for the rectangle tool (0 = sharp corners)</source>
        <translation>長方形ツールの角丸半径（0 = 角を丸めない）</translation>
    </message>
    <message>
        <location line="-546"/>
        <source>Rounded-corner radius for the rectangular marquee (0 = sharp corners)</source>
        <translation>長方形選択ツールの角丸半径（0 = 角を丸めない）</translation>
    </message>
    <message>
        <location line="+635"/>
        <source>Fill opacity for the Fill tool and Fill shortcut</source>
        <translation>塗りつぶしツールと塗りつぶしショートカットの不透明度</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Soft edge feather for the Fill tool and Fill shortcut</source>
        <translation>塗りつぶしツールと塗りつぶしショートカットのソフトエッジのぼかし</translation>
    </message>
    <message>
        <location line="-460"/>
        <source>Brush preset: %1</source>
        <translation>ブラシプリセット: %1</translation>
    </message>
    <message>
        <location line="+144"/>
        <source>Aligned</source>
        <translation>整列</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Keep sample source offset aligned across strokes</source>
        <translation>ストローク間でサンプル元のオフセットを維持します</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_actions.cpp" line="2754"/>
        <source>Diffusion:</source>
        <translation>拡散:</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_actions.cpp" line="2760"/>
        <source>Lower values preserve fine texture; higher values adapt more quickly</source>
        <translation>値を小さくすると細かいテクスチャを保持し、大きくすると周囲により速くなじみます</translation>
    </message>
    <message>
        <location line="+112"/>
        <source>Brush Smaller</source>
        <translation>ブラシを小さく</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Brush Larger</source>
        <translation>ブラシを大きく</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Brush Much Smaller</source>
        <translation>ブラシを大幅に小さく</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Brush Much Larger</source>
        <translation>ブラシを大幅に大きく</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Tol:</source>
        <translation>許容値:</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Contiguous</source>
        <translation>隣接</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Limit Magic Wand selection to connected pixels</source>
        <translation>自動選択をつながったピクセルに限定します</translation>
    </message>
    <message>
        <location line="-135"/>
        <location line="+145"/>
        <source>Sample All Layers</source>
        <translation>すべてのレイヤーを対象</translation>
    </message>
    <message>
        <location line="-142"/>
        <location line="+145"/>
        <source>Sample the merged document instead of the active layer</source>
        <translation>アクティブレイヤーではなく結合表示されたドキュメントをサンプルします</translation>
    </message>
    <message>
        <location line="+149"/>
        <source>Font:</source>
        <translation>フォント:</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>B</source>
        <translation>B</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Bold</source>
        <translation>太字</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>I</source>
        <translation>I</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Italic</source>
        <translation>斜体</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Smoothing:</source>
        <translation>スムージング:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Text smoothing</source>
        <translation>テキストのスムージング</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+3069"/>
        <source>None</source>
        <translation>なし</translation>
    </message>
    <message>
        <location line="-3068"/>
        <source>Sharp</source>
        <translation>シャープ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Crisp</source>
        <translation>くっきり</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Strong</source>
        <translation>強く</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Smooth</source>
        <translation>スムーズ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Windows LCD</source>
        <translation>Windows LCD</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Windows</source>
        <translation>Windows</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Color:</source>
        <translation>色:</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+10337"/>
        <source>T</source>
        <translation>T</translation>
    </message>
    <message>
        <location line="-10335"/>
        <source>Text color</source>
        <translation>テキスト色</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Align:</source>
        <translation>整列:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>L</source>
        <translation>左</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Align Left</source>
        <translation>左揃え</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>C</source>
        <translation>中</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Align Center</source>
        <translation>中央揃え</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>R</source>
        <translation>右</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Align Right</source>
        <translation>右揃え</translation>
    </message>
    <message>
        <location line="+224"/>
        <source>Layers</source>
        <translation>レイヤー</translation>
    </message>
    <message>
        <location line="+85"/>
        <location line="+5514"/>
        <location line="+38"/>
        <source>Editing layer mask</source>
        <translation>レイヤーマスクを編集中</translation>
    </message>
    <message>
        <location line="-37"/>
        <source>Editing layer pixels</source>
        <translation>レイヤーピクセルを編集中</translation>
    </message>
    <message>
        <location line="-5451"/>
        <source>Mode</source>
        <translation>モード</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+8640"/>
        <source>Opacity</source>
        <translation>不透明度</translation>
    </message>
    <message>
        <location line="-8543"/>
        <location line="+7358"/>
        <source>New Layer</source>
        <translation>新規レイヤー</translation>
    </message>
    <message>
        <location line="-7357"/>
        <location line="+7359"/>
        <source>New Folder</source>
        <translation>新規フォルダー</translation>
    </message>
    <message>
        <location line="-7358"/>
        <location line="+7360"/>
        <source>New Adjustment Layer</source>
        <translation>新規調整レイヤー</translation>
    </message>
    <message>
        <location line="-7359"/>
        <location line="+7361"/>
        <source>Duplicate Layer</source>
        <translation>レイヤーを複製</translation>
    </message>
    <message>
        <location line="-7360"/>
        <location line="+5545"/>
        <source>Rename Layer</source>
        <translation>レイヤー名を変更</translation>
    </message>
    <message>
        <location line="-5544"/>
        <location line="+7361"/>
        <source>Delete Layer</source>
        <translation>レイヤーを削除</translation>
    </message>
    <message>
        <location line="-7414"/>
        <location line="+9379"/>
        <location line="+111"/>
        <source>Lock transparent pixels</source>
        <translation>透明ピクセルをロック</translation>
    </message>
    <message>
        <location line="-9321"/>
        <source>History</source>
        <translation>履歴</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Properties</source>
        <translation>プロパティ</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Info</source>
        <translation>情報</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+9355"/>
        <source>X: -
Y: -
RGB: -
Rect: -</source>
        <translation>X: -
Y: -
RGB: -
範囲: -</translation>
    </message>
    <message>
        <source>Swatches</source>
        <translation type="vanished">スウォッチ</translation>
    </message>
    <message>
        <source>Set foreground color</source>
        <translation type="vanished">描画色を設定</translation>
    </message>
    <message>
        <location line="+234"/>
        <source>Foreground color changed</source>
        <translation>描画色を変更しました</translation>
    </message>
    <message>
        <location line="-9434"/>
        <source>Picked color %1, %2, %3 (%4)</source>
        <translation>色を取得しました %1, %2, %3 (%4)</translation>
    </message>
    <message>
        <location line="+386"/>
        <location line="+613"/>
        <location line="+33"/>
        <location line="+38"/>
        <location line="+608"/>
        <location line="+234"/>
        <location line="+3783"/>
        <location line="+31"/>
        <location line="+247"/>
        <source>Untitled</source>
        <translation>無題</translation>
    </message>
    <message>
        <location line="-4973"/>
        <source>Save changes?</source>
        <translation>変更を保存しますか?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Save changes to %1 before closing?</source>
        <translation>閉じる前に %1 への変更を保存しますか?</translation>
    </message>
    <message>
        <location line="+205"/>
        <location line="+23"/>
        <source>Untitled-%1</source>
        <translation>無題-%1</translation>
    </message>
    <message>
        <location line="-12"/>
        <location line="+23"/>
        <source>Created %1 x %2 document</source>
        <translation>%1 x %2 のドキュメントを作成しました</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Print resolution</source>
        <translation>印刷解像度</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Image size unchanged; print resolution set to %1 ppi</source>
        <translation>画像サイズは変更せず、印刷解像度を %1 ppi に設定しました</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Image size</source>
        <translation>画像サイズ</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Image %1 x %2 px (%3 x %4 in) at %5 ppi</source>
        <translation>画像 %1 x %2 px (%3 x %4 in)、%5 ppi</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Canvas size</source>
        <translation>キャンバスサイズ</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Canvas %1 x %2</source>
        <translation>キャンバス %1 x %2</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+147"/>
        <location line="+10164"/>
        <source>Open</source>
        <translation>開く</translation>
    </message>
    <message>
        <location line="-10203"/>
        <source>Opening %1...</source>
        <translation>%1 を開いています...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Opening %1</source>
        <translation>%1 を開いています</translation>
    </message>
    <message>
        <location line="-63"/>
        <source>Drop a supported image or Photoshop document</source>
        <translation>対応画像または Photoshop ドキュメントをドロップしてください</translation>
    </message>
    <message>
        <location line="+109"/>
        <source>Opened %1</source>
        <translation>%1 を開きました</translation>
    </message>
    <message>
        <source>Reopen Document</source>
        <translation>ドキュメントを開き直す</translation>
    </message>
    <message>
        <source>Reveal in Explorer</source>
        <translation>エクスプローラーで表示</translation>
    </message>
    <message>
        <source>Reveal in Finder</source>
        <translation>Finder で表示</translation>
    </message>
    <message>
        <source>Show in File Manager</source>
        <translation>ファイルマネージャーで表示</translation>
    </message>
    <message>
        <source>Reopen document?</source>
        <translation>ドキュメントを開き直しますか?</translation>
    </message>
    <message>
        <source>%1 has unsaved changes. Reopen the file from disk and discard them?</source>
        <translation>%1 には保存されていない変更があります。ディスクからファイルを開き直して変更を破棄しますか?</translation>
    </message>
    <message>
        <source>Reopen</source>
        <translation>開き直す</translation>
    </message>
    <message>
        <source>Reopened %1</source>
        <translation>%1 を開き直しました</translation>
    </message>
    <message>
        <source>Reopened %1. %2</source>
        <translation>%1 を開き直しました。%2</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Open failed</source>
        <translation>開けませんでした</translation>
    </message>
    <message>
        <location line="+253"/>
        <source>Untitled.psd</source>
        <translation>無題.psd</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Save As</source>
        <translation>名前を付けて保存</translation>
    </message>
    <message>
        <location line="+91"/>
        <location line="+20"/>
        <source>Save</source>
        <translation>保存</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Saved %1</source>
        <translation>%1 を保存しました</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+3863"/>
        <location line="+18"/>
        <location line="+21"/>
        <source>Save failed</source>
        <translation>保存に失敗しました</translation>
    </message>
    <message>
        <location line="-3887"/>
        <source>Export Flat Image</source>
        <translation>統合画像を書き出し</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Export flat image</source>
        <translation>統合画像を書き出し</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Exported %1</source>
        <translation>%1 を書き出しました</translation>
    </message>
    <message>
        <location line="-245"/>
        <location line="+247"/>
        <location line="+3692"/>
        <source>Export failed</source>
        <translation>書き出しに失敗しました</translation>
    </message>
    <message>
        <location line="-3673"/>
        <source>Print output created</source>
        <translation>印刷出力を作成しました</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>Preferences</source>
        <translation>環境設定</translation>
    </message>
    <message>
        <location line="+141"/>
        <source>Application</source>
        <translation>アプリケーション</translation>
    </message>
    <message>
        <location line="-94"/>
        <source>English</source>
        <translation>英語</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Language:</source>
        <translation>言語:</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Interface scale:</source>
        <translation>インターフェースの拡大率:</translation>
    </message>
    <message>
        <location line="+449"/>
        <source>Interface Scale</source>
        <translation>インターフェースの拡大率</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Restart Patchy for the new interface scale to take effect.</source>
        <translation>新しいインターフェースの拡大率を反映するには Patchy を再起動してください。</translation>
    </message>
    <message>
        <location line="-552"/>
        <source>Update Available</source>
        <translation>更新があります</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>Patchy %1 is available. You are using version %2.

Save your work and close Patchy before running the installer.</source>
        <translation>Patchy %1 が利用可能です。現在のバージョンは %2 です。

インストーラーを実行する前に作業を保存して Patchy を閉じてください。</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Patchy %1 is available. You are using version %2.

Download the DMG, quit Patchy, and drag the new Patchy into Applications.</source>
        <translation>Patchy %1 が利用可能です。現在のバージョンは %2 です。

DMG をダウンロードし、Patchy を終了してから新しい Patchy を Applications にドラッグしてください。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Patchy %1 is available. You are using version %2.

To update, paste this into a terminal:

%3</source>
        <translation>Patchy %1 が利用可能です。現在のバージョンは %2 です。

更新するには、次のコマンドをターミナルに貼り付けてください:

%3</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Copy Command</source>
        <translation>コマンドをコピー</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Install command copied to the clipboard</source>
        <translation>インストールコマンドをクリップボードにコピーしました</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Download</source>
        <translation>ダウンロード</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not Now</source>
        <translation>今はしない</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Could not open the download link</source>
        <translation>ダウンロードリンクを開けませんでした</translation>
    </message>
    <message>
        <location line="+75"/>
        <source>Check for updates on startup</source>
        <translation>起動時に更新を確認する</translation>
    </message>
    <message>
        <location line="+90"/>
        <source>Enable pen and tablet input</source>
        <translation>ペンとタブレット入力を有効にする</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Pressure controls brush size</source>
        <translation>筆圧でブラシサイズを制御する</translation>
    </message>
    <message>
        <location line="+83"/>
        <source>Minimum size:</source>
        <translation>最小サイズ:</translation>
    </message>
    <message>
        <location line="-75"/>
        <source>Pressure controls opacity</source>
        <translation>筆圧で不透明度を制御する</translation>
    </message>
    <message>
        <location line="+77"/>
        <source>Minimum opacity:</source>
        <translation>最小不透明度:</translation>
    </message>
    <message>
        <location line="-69"/>
        <source>Use eraser tip as Eraser</source>
        <translation>消しゴム側のペン先を消しゴムとして使用する</translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Lower pen button:</source>
        <translation>ペンの下ボタン:</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Upper pen button:</source>
        <translation>ペンの上ボタン:</translation>
    </message>
    <message>
        <location line="-72"/>
        <source>Pan canvas</source>
        <translation>キャンバスをパンする</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Zoom canvas (drag)</source>
        <translation>キャンバスをズーム（ドラッグ）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Set clone source</source>
        <translation>クローンソースを設定する</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Swap colors</source>
        <translation>色を入れ替える</translation>
    </message>
    <message>
        <location line="+56"/>
        <source>Set the pen buttons to Right Mouse Click and Middle Mouse Click in your tablet driver: while the pen is over the canvas, a right click triggers the Upper action and a middle click the Lower one. Buttons set to Scroll or Pan are handled by the driver and cannot trigger these actions. Tablet pad buttons (express keys) are also driver-only: map them to keyboard shortcuts such as Undo, Redo, [ and ] for brush size, and E for the eraser.</source>
        <translation>タブレットのドライバーでペンボタンを「右ボタンクリック」と「中ボタンクリック」に設定してください。ペンがキャンバス上にあるとき、右クリックで「ペンの上ボタン」、中クリックで「ペンの下ボタン」のアクションが実行されます。スクロールやパンに設定されたボタンはドライバーが処理するため、これらのアクションを実行できません。タブレットのパッドボタン（エクスプレスキー）も同様にドライバー専用です。元に戻す、やり直し、ブラシサイズの [ と ]、消しゴムの E などのキーボードショートカットに割り当ててください。</translation>
    </message>
    <message>
        <location line="-68"/>
        <source>Scroll wheel zooms the canvas</source>
        <translation>スクロールホイールでキャンバスをズームする</translation>
    </message>
    <message>
        <location line="-1820"/>
        <source>Close smart object contents?</source>
        <translation>スマートオブジェクトの内容を閉じますか?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 has smart object contents open for editing. Close those tabs too?</source>
        <translation>%1 のスマートオブジェクトの内容が編集用に開いています。そのタブも閉じますか?</translation>
    </message>
    <message>
        <location line="+1823"/>
        <source>Also applies to a pen button set to Scroll. Hold Ctrl or Shift while scrolling to pan.</source>
        <translation>スクロールに設定されたペンボタンにも適用されます。スクロール中に Ctrl または Shift を押すとパンします。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Pick color</source>
        <translation>色を取得する</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Toggle eraser</source>
        <translation>消しゴムを切り替える</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Increase brush size</source>
        <translation>ブラシサイズを大きくする</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Decrease brush size</source>
        <translation>ブラシサイズを小さくする</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Tilt shapes brush dabs</source>
        <translation>傾きでブラシの形状を変える</translation>
    </message>
    <message>
        <location line="+53"/>
        <source>Minimum tilt roundness:</source>
        <translation>傾き時の最小真円率:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Pen</source>
        <translation>ペン</translation>
    </message>
    <message>
        <location line="+446"/>
        <source>Scan Legacy Photoshop Plug-ins</source>
        <translation>従来の Photoshop プラグインをスキャン</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Photoshop Plug-ins (*.8bf *.8bi *.8li);;All Files (*.*)</source>
        <translation>Photoshop プラグイン (*.8bf *.8bi *.8li);;すべてのファイル (*.*)</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>%1 plug-in action(s) available under Plug-ins &gt; Legacy Photoshop Plug-ins.

%2</source>
        <translation>%1 個のプラグイン操作が プラグイン &gt; 従来の Photoshop プラグイン で利用できます。

%2</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>%1: %2 (%3, %4)</source>
        <translation>%1: %2 (%3, %4)</translation>
    </message>
    <message>
        <location line="+66"/>
        <source>Select a pixel layer before running the plug-in</source>
        <translation>プラグインを実行する前にピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Select an editable 8-bit pixel layer before running the plug-in</source>
        <translation>プラグインを実行する前に編集可能な 8 ビットピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Legacy Photoshop Plug-in</source>
        <translation>従来の Photoshop プラグイン</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 was scanned and is available, but this build only has compatibility shims for the bundled Greyscale and White to Transparent test filters. A full 8BF host still needs the out-of-process Photoshop SDK adapter.</source>
        <translation>%1 はスキャンされ利用可能ですが、このビルドには同梱の Greyscale と White to Transparent テストフィルター用の互換シムしかありません。完全な 8BF ホストには、引き続き別プロセスの Photoshop SDK アダプターが必要です。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Legacy plug-in</source>
        <translation>従来のプラグイン</translation>
    </message>
    <message>
        <location line="+22"/>
        <location filename="../src/ui/main_window_adjustments.cpp" line="-33"/>
        <location line="+206"/>
        <location line="+187"/>
        <source>Applied %1</source>
        <translation>%1 を適用しました</translation>
    </message>
    <message>
        <location line="+47"/>
        <source>Select a layer to cut</source>
        <translation>切り取るレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Selected layers are hidden or not editable; nothing cut</source>
        <translation>選択したレイヤーは非表示または編集不可のため、切り取りませんでした</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Cut</source>
        <translation>切り取り</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Cut %1 layer(s)</source>
        <translation>%1 個のレイヤーを切り取りました</translation>
    </message>
    <message>
        <location line="+29"/>
        <location line="+6"/>
        <location line="+6"/>
        <source>Select a layer to copy</source>
        <translation>コピーするレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+18"/>
        <location line="+53"/>
        <source>Copy</source>
        <translation>コピー</translation>
    </message>
    <message>
        <location line="-52"/>
        <source>Copied %1 layer(s)</source>
        <translation>%1 個のレイヤーをコピーしました</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Selected layers are hidden or not editable; nothing copied</source>
        <translation>選択したレイヤーは非表示または編集不可のため、コピーしませんでした</translation>
    </message>
    <message>
        <location line="+16"/>
        <location line="+37"/>
        <source>Nothing to copy</source>
        <translation>コピーするものがありません</translation>
    </message>
    <message>
        <location line="-15"/>
        <source>Copied %1 layer(s), %2 x %3 px</source>
        <translation>%1 個のレイヤーをコピーしました、%2 x %3 px</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Copy merged</source>
        <translation>結合部分をコピー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copied merged %1 x %2 px</source>
        <translation>結合部分 %1 x %2 px をコピーしました</translation>
    </message>
    <message>
        <location line="+28"/>
        <location line="+34"/>
        <source>Paste</source>
        <translation>貼り付け</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Pasted %1 layer(s)</source>
        <translation>%1 個のレイヤーを貼り付けました</translation>
    </message>
    <message>
        <location line="-1946"/>
        <location line="+1958"/>
        <source>Clipboard does not contain an image</source>
        <translation>クリップボードに画像がありません</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Pasted as new layer</source>
        <translation>新規レイヤーとして貼り付けました</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+12"/>
        <source>Select a pixel layer to transform</source>
        <translation>変形するピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+142"/>
        <location line="+504"/>
        <source>Canceled text edit</source>
        <translation>テキスト編集をキャンセルしました</translation>
    </message>
    <message>
        <location line="+5414"/>
        <source>Text: %1</source>
        <translation>テキスト: %1</translation>
    </message>
    <message>
        <location line="+1387"/>
        <source>Text Preview</source>
        <translation>テキストプレビュー</translation>
    </message>
    <message>
        <location line="-6478"/>
        <source>Created text layer</source>
        <translation>テキストレイヤーを作成しました</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_adjustments.cpp" line="-565"/>
        <location line="+246"/>
        <location line="+184"/>
        <location line="+178"/>
        <location line="+129"/>
        <source>Select an editable RGB pixel layer</source>
        <translation>編集可能な RGB ピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="-657"/>
        <source>Filter preview failed: %1</source>
        <translation>フィルタープレビューに失敗しました: %1</translation>
    </message>
    <message>
        <location line="+28"/>
        <location line="+49"/>
        <source>Cancelled %1</source>
        <translation>%1 をキャンセルしました</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_adjustments.cpp" line="838"/>
        <source>Cancelled Filter Gallery</source>
        <translation>フィルターギャラリーをキャンセルしました</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_adjustments.cpp" line="842"/>
        <source>No visual filter applied</source>
        <translation>ビジュアルフィルターは適用されませんでした</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_adjustments.cpp" line="889"/>
        <source>Filter Stack</source>
        <translation>フィルタースタック</translation>
    </message>
    <message>
        <location line="-37"/>
        <location line="+221"/>
        <location line="+192"/>
        <source>Applying %1...</source>
        <translation>%1 を適用しています...</translation>
    </message>
    <message>
        <location line="-413"/>
        <location line="+221"/>
        <location line="+192"/>
        <location filename="../src/ui/main_window_stress_test.cpp" line="+267"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="-401"/>
        <location line="+216"/>
        <location line="+192"/>
        <source>Applying %1...
%2</source>
        <translation>%1 を適用しています...
%2</translation>
    </message>
    <message>
        <location line="-379"/>
        <location line="+207"/>
        <location line="+187"/>
        <source>%1 made no changes</source>
        <translation>%1 による変更はありません</translation>
    </message>
    <message>
        <location line="-390"/>
        <source>Filter: %1</source>
        <translation>フィルター: %1</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Filter failed</source>
        <translation>フィルターに失敗しました</translation>
    </message>
    <message>
        <location line="+38"/>
        <location line="+115"/>
        <location line="+36"/>
        <location line="+3"/>
        <location line="+7"/>
        <location line="+10"/>
        <source>Levels</source>
        <translation>レベル補正</translation>
    </message>
    <message>
        <location line="-156"/>
        <location line="+94"/>
        <location line="+37"/>
        <source>Cancelled Levels</source>
        <translation>レベル補正をキャンセルしました</translation>
    </message>
    <message>
        <location line="+36"/>
        <location line="+125"/>
        <location line="+31"/>
        <location line="+3"/>
        <location line="+7"/>
        <location line="+10"/>
        <source>Curves</source>
        <translation>トーンカーブ</translation>
    </message>
    <message>
        <location line="-159"/>
        <location line="+102"/>
        <location line="+32"/>
        <source>Cancelled Curves</source>
        <translation>トーンカーブをキャンセルしました</translation>
    </message>
    <message>
        <location line="+36"/>
        <location line="+116"/>
        <source>Hue/Saturation</source>
        <translation>色相・彩度</translation>
    </message>
    <message>
        <location line="-108"/>
        <location line="+95"/>
        <source>Cancelled Hue/Saturation</source>
        <translation>色相・彩度をキャンセルしました</translation>
    </message>
    <message>
        <location line="+26"/>
        <location line="+117"/>
        <source>Color Balance</source>
        <translation>カラーバランス</translation>
    </message>
    <message>
        <location line="-109"/>
        <location line="+94"/>
        <source>Cancelled Color Balance</source>
        <translation>カラーバランスをキャンセルしました</translation>
    </message>
    <message>
        <location line="+84"/>
        <source>%1 adjustment layer</source>
        <translation>%1 調整レイヤー</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Added %1 adjustment layer</source>
        <translation>%1 調整レイヤーを追加しました</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Select an adjustment layer to edit its settings</source>
        <translation>設定を編集する調整レイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>This adjustment layer has no editable settings</source>
        <translation>この調整レイヤーには編集可能な設定がありません</translation>
    </message>
    <message>
        <location line="+144"/>
        <source>Cancelled adjustment edit</source>
        <translation>調整の編集をキャンセルしました</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Edit %1 adjustment</source>
        <translation>%1 調整を編集</translation>
    </message>
    <message>
        <location line="-4"/>
        <location line="+9"/>
        <source>Updated adjustment layer</source>
        <translation>調整レイヤーを更新しました</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="+568"/>
        <source>New layer</source>
        <translation>新規レイヤー</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Folder %1</source>
        <translation>フォルダー %1</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>New folder</source>
        <translation>新規フォルダー</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Created folder</source>
        <translation>フォルダーを作成しました</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Nothing visible to copy to a new layer</source>
        <translation>新規レイヤーにコピーできる表示内容がありません</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Layer via copy</source>
        <translation>コピーしてレイヤー化</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Layer Via Copy</source>
        <translation>コピーしてレイヤー化</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Copied selection to a new layer</source>
        <translation>選択範囲を新規レイヤーにコピーしました</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Nothing visible to cut to a new layer</source>
        <translation>新規レイヤーにカットできる表示内容がありません</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Layer via cut</source>
        <translation>カットしてレイヤー化</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Layer Via Cut</source>
        <translation>カットしてレイヤー化</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Cut selection to a new layer</source>
        <translation>選択範囲を新規レイヤーにカットしました</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+5"/>
        <source>Select a pixel or adjustment layer before adding a mask</source>
        <translation>マスクを追加する前にピクセルレイヤーまたは調整レイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Add layer mask</source>
        <translation>レイヤーマスクを追加</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Added layer mask from selection</source>
        <translation>選択範囲からレイヤーマスクを追加しました</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Added layer mask. Paint with black to hide and white to reveal.</source>
        <translation>レイヤーマスクを追加しました。黒で塗ると非表示、白で塗ると表示されます。</translation>
    </message>
    <message>
        <location line="-21"/>
        <source>Layer already has a mask</source>
        <translation>レイヤーには既にマスクがあります</translation>
    </message>
    <message>
        <location line="+32"/>
        <location line="+103"/>
        <location line="+19"/>
        <location line="+30"/>
        <location line="+29"/>
        <source>Active layer has no mask</source>
        <translation>アクティブレイヤーにマスクがありません</translation>
    </message>
    <message>
        <location line="-173"/>
        <source>Delete layer mask</source>
        <translation>レイヤーマスクを削除</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Deleted layer mask</source>
        <translation>レイヤーマスクを削除しました</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Link layer mask</source>
        <translation>レイヤーマスクをリンク</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Unlink layer mask</source>
        <translation>レイヤーマスクのリンクを解除</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Layer and mask linked</source>
        <translation>レイヤーとマスクをリンクしました</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Layer and mask unlinked</source>
        <translation>レイヤーとマスクのリンクを解除しました</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>Disable layer mask</source>
        <translation>レイヤーマスクを無効化</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Enable layer mask</source>
        <translation>レイヤーマスクを有効化</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Layer mask disabled</source>
        <translation>レイヤーマスクを無効化しました</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Layer mask enabled</source>
        <translation>レイヤーマスクを有効化しました</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Invert layer mask</source>
        <translation>レイヤーマスクを反転</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Inverted layer mask</source>
        <translation>レイヤーマスクを反転しました</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Apply mask supports editable 8-bit pixel layers</source>
        <translation>マスクの適用は編集可能な 8 ビットピクセルレイヤーで使用できます</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Apply layer mask</source>
        <translation>レイヤーマスクを適用</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Applied layer mask</source>
        <translation>レイヤーマスクを適用しました</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Duplicate layer</source>
        <translation>レイヤーを複製</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Name</source>
        <translation>名前</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Rename layer</source>
        <translation>レイヤー名を変更</translation>
    </message>
    <message>
        <location line="+97"/>
        <source>Layer style</source>
        <translation>レイヤースタイル</translation>
    </message>
    <message>
        <location line="-8301"/>
        <source>Copy Layer Style</source>
        <translation>レイヤースタイルをコピー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste Layer Style</source>
        <translation>レイヤースタイルを貼り付け</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Delete Layer Style</source>
        <translation>レイヤースタイルを削除</translation>
    </message>
    <message>
        <location line="+8336"/>
        <source>Copy layer style</source>
        <translation>レイヤースタイルをコピー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copied layer style</source>
        <translation>レイヤースタイルをコピーしました</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Paste layer style</source>
        <translation>レイヤースタイルを貼り付け</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Pasted layer style to %1 layer(s)</source>
        <translation>%1 個のレイヤーにレイヤースタイルを貼り付けました</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Delete layer style</source>
        <translation>レイヤースタイルを削除</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Deleted layer style from %1 layer(s)</source>
        <translation>%1 個のレイヤーからレイヤースタイルを削除しました</translation>
    </message>
    <message>
        <location line="-125"/>
        <source>Updated layer style</source>
        <translation>レイヤースタイルを更新しました</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp"/>
        <source>&quot;%1&quot; will open as a new image when the Layer Style dialog closes</source>
        <translation>レイヤースタイルダイアログを閉じると「%1」が新しい画像として開きます</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp"/>
        <source>Opened pattern &quot;%1&quot; as a new image</source>
        <translation>パターン「%1」を新しい画像として開きました</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp"/>
        <source>Open pattern as image</source>
        <translation>パターンを画像として開く</translation>
    </message>
    <message>
        <location line="+193"/>
        <source>No rasterizable layers selected</source>
        <translation>ラスタライズできるレイヤーが選択されていません</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Rasterize layer</source>
        <translation>レイヤーをラスタライズ</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Rasterize layers</source>
        <translation>レイヤーをラスタライズ</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Rasterized layer</source>
        <translation>レイヤーをラスタライズしました</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Rasterized layers</source>
        <translation>レイヤーをラスタライズしました</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>No layer styles to rasterize</source>
        <translation>ラスタライズするレイヤースタイルがありません</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Rasterize layer style</source>
        <translation>レイヤースタイルをラスタライズ</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Rasterize layer styles</source>
        <translation>レイヤースタイルをラスタライズ</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Rasterized layer style</source>
        <translation>レイヤースタイルをラスタライズしました</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Rasterized layer styles</source>
        <translation>レイヤースタイルをラスタライズしました</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>All Files (*.*)</source>
        <translation>すべてのファイル (*.*)</translation>
    </message>
    <message>
        <location line="+1080"/>
        <source>Delete layer</source>
        <translation>レイヤーを削除</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Move layer</source>
        <translation>レイヤーを移動</translation>
    </message>
    <message>
        <location line="+47"/>
        <location line="+59"/>
        <source>Reorder layers</source>
        <translation>レイヤー順序を変更</translation>
    </message>
    <message>
        <location line="-48"/>
        <location line="+73"/>
        <source>Reordered layers</source>
        <translation>レイヤー順序を変更しました</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Folder expanded</source>
        <translation>フォルダーを展開しました</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Folder collapsed</source>
        <translation>フォルダーを折りたたみました</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Folder and nested folders expanded</source>
        <translation>フォルダーと中のフォルダーを展開しました</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Folder and nested folders collapsed</source>
        <translation>フォルダーと中のフォルダーを折りたたみました</translation>
    </message>
    <message>
        <location line="+76"/>
        <source>Layer shown</source>
        <translation>レイヤーを表示しました</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Layer hidden</source>
        <translation>レイヤーを非表示にしました</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Edit Adjustment...</source>
        <translation>調整を編集...</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Rename Layer...</source>
        <translation>レイヤー名を変更...</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Merge Down</source>
        <translation>下のレイヤーと結合</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Merge Visible to New Layer</source>
        <translation>表示レイヤーを新規レイヤーに結合</translation>
    </message>
    <message>
        <location line="-10016"/>
        <source>Rasterize</source>
        <translation>ラスタライズ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rasterize (including layer style)</source>
        <translation>ラスタライズ（レイヤースタイルを含む）</translation>
    </message>
    <message>
        <location line="+10075"/>
        <location line="+2151"/>
        <source>Visible</source>
        <translation>表示</translation>
    </message>
    <message>
        <location line="-2141"/>
        <source>Lock Transparent Pixels</source>
        <translation>透明ピクセルをロック</translation>
    </message>
    <message>
        <location line="-10132"/>
        <location line="+10145"/>
        <source>Load Layer Transparency</source>
        <translation>レイヤーの透明部分を読み込み</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Add Layer Mask</source>
        <translation>レイヤーマスクを追加</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Edit Layer Mask</source>
        <translation>レイヤーマスクを編集</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Delete Layer Mask</source>
        <translation>レイヤーマスクを削除</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Link Layer Mask</source>
        <translation>レイヤーマスクをリンク</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable Layer Mask</source>
        <translation>レイヤーマスクを無効化</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Invert Layer Mask</source>
        <translation>レイヤーマスクを反転</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Apply Layer Mask</source>
        <translation>レイヤーマスクを適用</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>Merge visible</source>
        <translation>表示レイヤーを結合</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Merged Visible</source>
        <translation>表示レイヤーを結合</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Merged visible layers to a new layer</source>
        <translation>表示レイヤーを新規レイヤーに結合しました</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+29"/>
        <location line="+47"/>
        <location line="+52"/>
        <source>Select a layer to merge down</source>
        <translation>下に結合するレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="-72"/>
        <source>No layer below to merge</source>
        <translation>下に結合できるレイヤーがありません</translation>
    </message>
    <message>
        <location line="+44"/>
        <location line="+11"/>
        <location line="+7"/>
        <source>Nothing to merge down</source>
        <translation>下に結合する内容がありません</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Merge down</source>
        <translation>下のレイヤーと結合</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Merged layer down</source>
        <translation>レイヤーを下のレイヤーと結合しました</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Merged layers down</source>
        <translation>レイヤーを結合しました</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Filled layer mask</source>
        <translation>レイヤーマスクを塗りつぶしました</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Clear layer mask</source>
        <translation>レイヤーマスクを消去</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Cleared layer mask</source>
        <translation>レイヤーマスクを消去しました</translation>
    </message>
    <message>
        <location line="+119"/>
        <source>Clear</source>
        <translation>消去</translation>
    </message>
    <message>
        <location line="-48"/>
        <source>Clearing...</source>
        <translation>消去中...</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_palette.cpp" line="-253"/>
        <source>Converting to palette...</source>
        <translation>パレットに変換中...</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window.cpp" line="-4692"/>
        <location line="+64"/>
        <source>Copied color %1</source>
        <translation>色 %1 をコピーしました</translation>
    </message>
    <message>
        <location line="+138"/>
        <source>Pasted color %1</source>
        <translation>色 %1 を貼り付けました</translation>
    </message>
    <message>
        <location line="-203"/>
        <source>Cut custom color %1</source>
        <translation>カスタム色 %1 を切り取りました</translation>
    </message>
    <message>
        <location line="+205"/>
        <source>The clipboard does not contain a color</source>
        <translation>クリップボードに色が含まれていません</translation>
    </message>
    <message>
        <location line="-3192"/>
        <source>Palette index %1 set to %2</source>
        <translation>パレットのインデックス %1 を %2 に設定しました</translation>
    </message>
    <message>
        <location line="+7682"/>
        <source>Clearing</source>
        <translation>消去中</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Nothing to clear</source>
        <translation>消去するものがありません</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Text and smart object layers can&apos;t be cleared. Deselect first, then Delete removes the layer.</source>
        <translation>テキストレイヤーとスマートオブジェクトレイヤーは消去できません。選択を解除してから Delete キーでレイヤーを削除してください。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Deleted layer</source>
        <translation>レイヤーを削除しました</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Deleted %1 layers</source>
        <translation>%1 個のレイヤーを削除しました</translation>
    </message>
    <message>
        <location line="-10644"/>
        <source>Export Smart Object Contents...</source>
        <translation>スマートオブジェクトの内容を書き出し...</translation>
    </message>
    <message numerus="yes">
        <location line="+4477"/>
        <source> (+%n more import note(s))</source>
        <translation>
            <numerusform> (ほか %n 件のインポートノート)</numerusform>
        </translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Opened %1. %2</source>
        <translation>%1 を開きました。%2</translation>
    </message>
    <message>
        <location line="+598"/>
        <source>Show import warnings and notes in a popup (status bar otherwise)</source>
        <translation>インポートの警告とノートをポップアップで表示 (オフの場合はステータスバー)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>When enabled, opening a file shows the PSD compatibility report and an Import Notes popup. When disabled, import notes appear only in the status bar.</source>
        <translation>オンにすると、ファイルを開いたときに PSD 互換性レポートとインポートノートのポップアップを表示します。オフの場合、インポートノートはステータスバーにのみ表示されます。</translation>
    </message>
    <message>
        <source>Show the develop dialog when opening camera raw files</source>
        <translation>カメラ Raw ファイルを開くときに現像ダイアログを表示する</translation>
    </message>
    <message>
        <source>When disabled, camera raw files open immediately with neutral develop settings (as-shot white balance, no adjustments).</source>
        <translation>オフにすると、カメラ Raw ファイルはニュートラルな現像設定 (撮影時のホワイトバランス、調整なし) ですぐに開きます。</translation>
    </message>
    <message>
        <location line="+3515"/>
        <location line="+35"/>
        <location line="+603"/>
        <location line="+288"/>
        <source>Select a smart object layer first</source>
        <translation>先にスマートオブジェクトレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="-921"/>
        <source>This smart object has no embedded contents to export</source>
        <translation>このスマートオブジェクトには書き出せる埋め込みコンテンツがありません</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Export Smart Object Contents</source>
        <translation>スマートオブジェクトの内容を書き出し</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Could not write %1</source>
        <translation>%1 に書き込めませんでした</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Exported smart object contents to %1</source>
        <translation>スマートオブジェクトの内容を %1 に書き出しました</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>This smart object has Smart Filters; Patchy keeps Photoshop&apos;s preview (rasterize to edit pixels)</source>
        <translation>このスマートオブジェクトにはスマートフィルターが適用されています。Patchy は Photoshop のプレビューを保持します (ピクセルを編集するにはラスタライズしてください)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>This smart object has a warp or perspective transform; Patchy keeps Photoshop&apos;s preview</source>
        <translation>このスマートオブジェクトにはワープまたは遠近変形が適用されています。Patchy は Photoshop のプレビューを保持します</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+595"/>
        <location line="+294"/>
        <source>This smart object can only be preserved, not edited</source>
        <translation>このスマートオブジェクトは保持のみ可能で、編集はできません</translation>
    </message>
    <message>
        <source>Smart Filter cache data could not be duplicated safely</source>
        <translation>スマートフィルターのキャッシュデータを安全に複製できませんでした</translation>
    </message>
    <message>
        <location line="-871"/>
        <location line="+24"/>
        <location line="+560"/>
        <location line="+282"/>
        <source>This smart object&apos;s contents are not embedded in the document</source>
        <translation>このスマートオブジェクトの内容はドキュメントに埋め込まれていません</translation>
    </message>
    <message>
        <location line="-835"/>
        <source>Patchy can&apos;t re-encode %1 contents; use Export Smart Object Contents or rasterize the layer</source>
        <translation>Patchy は %1 の内容を再エンコードできません。「スマートオブジェクトの内容を書き出し」を使うか、レイヤーをラスタライズしてください</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Could not decode the embedded smart object contents</source>
        <translation>埋め込まれたスマートオブジェクトの内容をデコードできませんでした</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>%1 (embedded in %2)</source>
        <translation>%1 (%2 に埋め込み)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Editing smart object contents. Save (Ctrl+S) applies them back to %1</source>
        <translation>スマートオブジェクトの内容を編集中です。保存 (Ctrl+S) で %1 に反映されます</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>The original document is closed; saving a copy instead</source>
        <translation>元のドキュメントが閉じられているため、コピーとして保存します</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The smart object no longer exists in %1</source>
        <translation>スマートオブジェクトは %1 にもう存在しません</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Could not re-encode the contents as %1</source>
        <translation>内容を %1 として再エンコードできませんでした</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>These contents can&apos;t be re-encoded</source>
        <translation>この内容は再エンコードできません</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Could not render the committed contents</source>
        <translation>適用した内容をレンダリングできませんでした</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Applied smart object contents to %1</source>
        <translation>スマートオブジェクトの内容を %1 に適用しました</translation>
    </message>
    <message>
        <location line="+438"/>
        <location line="+71"/>
        <source>Replace Smart Object Contents</source>
        <translation>スマートオブジェクトの内容を置き換え</translation>
    </message>
    <message>
        <location line="-296"/>
        <location line="+226"/>
        <location line="+321"/>
        <source>Embeddable Files (*.psd *.psb *.png *.jpg *.jpeg *.tif *.tiff *.bmp *.svg *.svgz);;All Files (*.*)</source>
        <translation>埋め込み可能なファイル (*.psd *.psb *.png *.jpg *.jpeg *.tif *.tiff *.bmp);;すべてのファイル (*.*)</translation>
    </message>
    <message>
        <location line="-295"/>
        <location line="+6"/>
        <location line="+21"/>
        <location line="+7"/>
        <source>Replace failed</source>
        <translation>置き換えに失敗しました</translation>
    </message>
    <message>
        <location line="-332"/>
        <location line="+67"/>
        <location line="+137"/>
        <location line="+94"/>
        <location line="+6"/>
        <location line="+303"/>
        <source>Could not read %1</source>
        <translation>%1 を読み込めませんでした</translation>
    </message>
    <message>
        <location line="-281"/>
        <source>%1 is not a file type Patchy can embed and edit</source>
        <translation>%1 は Patchy が埋め込んで編集できるファイル形式ではありません</translation>
    </message>
    <message>
        <location line="-316"/>
        <location line="+69"/>
        <location line="+6"/>
        <location line="+247"/>
        <location line="+294"/>
        <location line="+6"/>
        <location line="+39"/>
        <source>Could not decode %1</source>
        <translation>%1 をデコードできませんでした</translation>
    </message>
    <message>
        <location line="-5702"/>
        <source>Float in Window</source>
        <translation>ウィンドウに分離</translation>
    </message>
    <message>
        <location line="+2253"/>
        <source>Select a pixel layer to warp</source>
        <translation>ワープするピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+1455"/>
        <source>Select a text layer to warp.</source>
        <translation>ワープするテキストレイヤーを選択してください。</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Warp Text</source>
        <translation>ワープテキスト</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Could not warp the text layer.</source>
        <translation>テキストレイヤーをワープできませんでした。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Removed text warp</source>
        <translation>テキストワープを削除しました</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Warped text layer</source>
        <translation>テキストレイヤーをワープしました</translation>
    </message>
    <message>
        <location line="+952"/>
        <location line="+305"/>
        <location line="+204"/>
        <source>Linked file %1 was not found. Use Relink to File... to point it at a new location</source>
        <translation>リンクファイル %1 が見つかりません。「ファイルに再リンク...」で新しい場所を指定してください</translation>
    </message>
    <message>
        <location line="-497"/>
        <source>Editing linked file. Save (Ctrl+S) writes %1 and updates %2</source>
        <translation>リンクファイルを編集中です。保存 (Ctrl+S) で %1 に書き込み、%2 を更新します</translation>
    </message>
    <message>
        <location line="+268"/>
        <source>Saved %1 and updated %2</source>
        <translation>%1 を保存し、%2 を更新しました</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+6"/>
        <location line="+73"/>
        <location line="+125"/>
        <location line="+6"/>
        <source>Select a linked smart object layer first</source>
        <translation>先にリンクされたスマートオブジェクトレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="-155"/>
        <source>Updated smart object content from %1</source>
        <translation>%1 からスマートオブジェクトのコンテンツを更新しました</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Relink to File</source>
        <translation>ファイルに再リンク</translation>
    </message>
    <message>
        <location line="+22"/>
        <location line="+12"/>
        <location line="+6"/>
        <source>Relink failed</source>
        <translation>再リンクに失敗しました</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Relink Smart Object</source>
        <translation>スマートオブジェクトを再リンク</translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Relinked smart object to %1</source>
        <translation>スマートオブジェクトを %1 に再リンクしました</translation>
    </message>
    <message>
        <location line="+69"/>
        <source>Embedded linked smart object %1</source>
        <translation>リンクされたスマートオブジェクト %1 を埋め込みました</translation>
    </message>
    <message>
        <location line="+145"/>
        <source>Replaced smart object contents with %1</source>
        <translation>スマートオブジェクトの内容を %1 に置き換えました</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Select layers to convert to a smart object</source>
        <translation>スマートオブジェクトに変換するレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>The selected layers have no pixels to convert</source>
        <translation>選択したレイヤーには変換できるピクセルがありません</translation>
    </message>
    <message>
        <location line="+49"/>
        <source>Convert failed</source>
        <translation>変換に失敗しました</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Converted to a smart object; click its badge to edit its contents</source>
        <translation>スマートオブジェクトに変換しました。バッジをクリックすると内容を編集できます</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>Created an independent smart object copy</source>
        <translation>独立したスマートオブジェクトのコピーを作成しました</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+61"/>
        <source>Place Embedded</source>
        <translation>埋め込みを配置</translation>
    </message>
    <message>
        <location line="-46"/>
        <location line="+19"/>
        <location line="+6"/>
        <location line="+39"/>
        <source>Place failed</source>
        <translation>配置に失敗しました</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Placed %1 as a smart object</source>
        <translation>%1 をスマートオブジェクトとして配置しました</translation>
    </message>
    <message>
        <location line="+309"/>
        <source>Warp Text...</source>
        <translation>ワープテキスト...</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Layer Style</source>
        <translation>レイヤースタイル</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>New</source>
        <translation>新規</translation>
    </message>
    <message>
        <location line="+98"/>
        <source>Layer Mask</source>
        <translation>レイヤーマスク</translation>
    </message>
    <message>
        <location line="+324"/>
        <location line="+82"/>
        <source>This channel is read-only</source>
        <translation>このチャンネルは読み取り専用です</translation>
    </message>
    <message>
        <location line="-54"/>
        <source>Filled channel</source>
        <translation>チャンネルを塗りつぶしました</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Clear channel</source>
        <translation>チャンネルをクリア</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Cleared channel</source>
        <translation>チャンネルをクリアしました</translation>
    </message>
    <message>
        <location line="+145"/>
        <source>Make a selection before stroking</source>
        <translation>境界線を描く前に選択範囲を作成してください</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Select an editable pixel layer first</source>
        <translation>先に編集可能なピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Stroke selection</source>
        <translation>選択範囲の境界線を描く</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Stroked selection</source>
        <translation>選択範囲の境界線を描きました</translation>
    </message>
    <message>
        <location line="+189"/>
        <source>Make a selection before expanding</source>
        <translation>拡張する前に選択範囲を作成してください</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+2"/>
        <source>Expand Selection</source>
        <translation>選択範囲を拡張</translation>
    </message>
    <message>
        <location line="-2"/>
        <source>Expand by</source>
        <translation>拡張量</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Make a selection before contracting</source>
        <translation>縮小する前に選択範囲を作成してください</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+2"/>
        <source>Contract Selection</source>
        <translation>選択範囲を縮小</translation>
    </message>
    <message>
        <location line="-2"/>
        <source>Contract by</source>
        <translation>縮小量</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Make a selection before selecting a border</source>
        <translation>境界を選択する前に選択範囲を作成してください</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+2"/>
        <source>Border Selection</source>
        <translation>選択範囲の境界</translation>
    </message>
    <message>
        <location line="-2"/>
        <source>Width</source>
        <translation>幅</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Flip horizontal</source>
        <translation>水平方向に反転</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Flip vertical</source>
        <translation>垂直方向に反転</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Make a rectangular selection before cropping</source>
        <translation>切り抜く前に長方形の選択範囲を作成してください</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Crop</source>
        <translation>切り抜き</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Cropped to selection</source>
        <translation>選択範囲で切り抜きました</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+17"/>
        <source>Rotate canvas</source>
        <translation>キャンバスを回転</translation>
    </message>
    <message>
        <location line="-5"/>
        <source>Rotated canvas clockwise</source>
        <translation>キャンバスを時計回りに回転しました</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Rotated canvas counterclockwise</source>
        <translation>キャンバスを反時計回りに回転しました</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_actions.cpp"/>
        <source>Shift &amp;Seams to Center</source>
        <translation>シームを中央へ移動(&amp;S)</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_actions.cpp"/>
        <source>Wrap the image by half its size so tiling seams land in the middle; press again to shift back</source>
        <translation>画像を半分ずらしてタイルのシームを中央に移動します。もう一度実行すると元に戻ります</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_actions.cpp"/>
        <source>Seamless Tiling in &amp;Window</source>
        <translation>ウィンドウ内でシームレスタイリング(&amp;W)</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_actions.cpp"/>
        <source>Repeat the document around itself in the window so tile seams are visible while painting</source>
        <translation>ウィンドウ内でドキュメントを周囲に繰り返し表示し、描画中にタイルのシームを確認できます</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_layer_ops.cpp"/>
        <source>Shift seams</source>
        <translation>シームの移動</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_layer_ops.cpp"/>
        <source>Shifted seams to the center</source>
        <translation>シームを中央へ移動しました</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_layer_ops.cpp"/>
        <source>Shifted seams back to the edges</source>
        <translation>シームを端に戻しました</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_layer_ops.cpp"/>
        <source>Document too small to shift seams</source>
        <translation>ドキュメントが小さすぎてシームを移動できません</translation>
    </message>
    <message>
        <location line="+209"/>
        <source>Blend mode</source>
        <translation>描画モード</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Visibility</source>
        <translation>表示</translation>
    </message>
    <message>
        <location line="+30"/>
        <location line="+25"/>
        <location line="+106"/>
        <source>Lock layer</source>
        <translation>レイヤーをロック</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Layer locked</source>
        <translation>レイヤーをロックしました</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Layer unlocked</source>
        <translation>レイヤーのロックを解除しました</translation>
    </message>
    <message>
        <location line="+2886"/>
        <source>Finish the open dialog before editing the document</source>
        <translation>開いているダイアログを閉じてからドキュメントを編集してください</translation>
    </message>
    <message>
        <location line="-9143"/>
        <location line="+6393"/>
        <location line="+1"/>
        <source>Undo</source>
        <translation>取り消し</translation>
    </message>
    <message>
        <location line="-6393"/>
        <location line="+6423"/>
        <location line="+1"/>
        <source>Redo</source>
        <translation>やり直し</translation>
    </message>
    <message>
        <location line="+147"/>
        <source>
Folder with %1 layers%2</source>
        <translation>
%1 レイヤーのフォルダー%2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>
Collapsed</source>
        <translation>
折りたたみ</translation>
    </message>
    <message>
        <source>%1
%2% opacity%3%4%5%6%7</source>
        <translation type="vanished">%1
不透明度 %2%%3%4%5%6%7</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>
Hidden by parent folder</source>
        <translation>
親フォルダーで非表示</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>
Locked</source>
        <translation>
ロック済み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>
Transparent pixels locked</source>
        <translation>
透明ピクセルをロック</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>
Layer mask</source>
        <translation>
レイヤーマスク</translation>
    </message>
    <message>
        <location line="+404"/>
        <source>Document: %1 x %2 px | %3 x %4 %5 | %6 ppi | %7 | %8 layers | Zoom %9% | %10</source>
        <translation>ドキュメント: %1 x %2 px | %3 x %4 %5 | %6 ppi | %7 | %8 レイヤー | ズーム %9% | %10</translation>
    </message>
    <message>
        <location line="-8753"/>
        <location line="+1195"/>
        <location line="+79"/>
        <location line="+36"/>
        <location line="+141"/>
        <location line="+51"/>
        <location line="+7235"/>
        <source>No document</source>
        <translation>ドキュメントがありません</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Unsaved changes</source>
        <translation>未保存の変更</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Saved</source>
        <translation>保存済み</translation>
    </message>
    <message>
        <location line="-22"/>
        <location line="+27"/>
        <source>Layer: No active layer</source>
        <translation>レイヤー: アクティブレイヤーなし</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Hidden</source>
        <translation>非表示</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Geometry: Bounds: %1</source>
        <translation>ジオメトリ: 境界: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source> | Pixels: %1</source>
        <translation> | ピクセル: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source> | Contents: %1 layers</source>
        <translation> | 内容: %1 レイヤー</translation>
    </message>
    <message>
        <location line="+2"/>
        <source> | Effects: %1</source>
        <translation> | 効果: %1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Target: Mask</source>
        <translation>対象: マスク</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Target: Pixels</source>
        <translation>対象: ピクセル</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Mask: %1 | %2 | %3 | Bounds %4 | Default %5</source>
        <translation>マスク: %1 | %2 | %3 | 境界 %4 | 既定 %5</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Disabled</source>
        <translation>無効</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Enabled</source>
        <translation>有効</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Linked</source>
        <translation>リンク済み</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Unlinked</source>
        <translation>未リンク</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Adjustment: %1</source>
        <translation>調整: %1</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Tool: %1</source>
        <translation>ツール: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Size: %1 px</source>
        <translation>サイズ: %1 px</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Opacity: %1%</source>
        <translation>不透明度: %1%</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Softness: %1%</source>
        <translation>柔らかさ: %1%</translation>
    </message>
    <message>
        <source>Flow: %1%</source>
        <translation>フロー: %1%</translation>
    </message>
    <message>
        <source>Airbrush: on</source>
        <translation>エアブラシ: オン</translation>
    </message>
    <message>
        <source>Airbrush: off</source>
        <translation>エアブラシ: オフ</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Tolerance: %1 | %2 | %3</source>
        <translation>許容値: %1 | %2 | %3</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>contiguous</source>
        <translation>隣接</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>non-contiguous</source>
        <translation>非隣接</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+4"/>
        <source>sample all layers</source>
        <translation>すべてのレイヤーを対象</translation>
    </message>
    <message>
        <location line="-4"/>
        <location line="+4"/>
        <source>active layer</source>
        <translation>アクティブレイヤー</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+5"/>
        <source>Selection: feather %1 px, %2</source>
        <translation>選択範囲: ぼかし %1 px、%2</translation>
    </message>
    <message>
        <location line="-3"/>
        <location line="+5"/>
        <source>anti-aliased</source>
        <translation>アンチエイリアスあり</translation>
    </message>
    <message>
        <location line="-5"/>
        <location line="+5"/>
        <source>hard edge</source>
        <translation>ハードエッジ</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Text size: %1 pt</source>
        <translation>テキストサイズ: %1 pt</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>RGB: %1, %2, %3  #%4%5%6</source>
        <translation>RGB: %1, %2, %3  #%4%5%6</translation>
    </message>
    <message>
        <location line="-5"/>
        <source>RGB: -</source>
        <translation>RGB: -</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Rect: -</source>
        <translation>範囲: -</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1: %2 x %3  at %4, %5</source>
        <translation>%1: %2 x %3  位置 %4, %5</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>X: %1
Y: %2
%3
%4</source>
        <translation>X: %1
Y: %2
%3
%4</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Text Color</source>
        <translation>テキスト色</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Text color changed</source>
        <translation>テキスト色を変更しました</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Foreground Color</source>
        <translation>描画色</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Background Color</source>
        <translation>背景色</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Background color changed</source>
        <translation>背景色を変更しました</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Swapped foreground/background</source>
        <translation>描画色と背景色を入れ替えました</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Default colors</source>
        <translation>既定の色</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Foreground color %1</source>
        <translation>描画色 %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Background color %1</source>
        <translation>背景色 %1</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Text color %1</source>
        <translation>テキスト色 %1</translation>
    </message>
    <message>
        <location line="+2063"/>
        <location line="+78"/>
        <source>&amp;%1 %2</source>
        <translation>&amp;%1 %2</translation>
    </message>
    <message>
        <location line="+96"/>
        <source>Copy File Path</source>
        <translation>ファイルパスをコピー</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>File path copied</source>
        <translation>ファイルパスをコピーしました</translation>
    </message>
    <message>
        <location line="-162"/>
        <source>Recent Files %1-%2</source>
        <translation>最近使ったファイル %1～%2</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Clear Recent Files</source>
        <translation>最近使ったファイルをクリア</translation>
    </message>
    <message>
        <location line="+194"/>
        <source>Recent file is missing</source>
        <translation>最近使ったファイルが見つかりません</translation>
    </message>
    <message>
        <location line="-15014"/>
        <source>Open Recent &amp;Folder</source>
        <translation>最近使ったフォルダー(&amp;F)</translation>
    </message>
    <message>
        <location line="+14898"/>
        <source>Recent Folders %1-%2</source>
        <translation>最近使ったフォルダー %1～%2</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Clear Recent Folders</source>
        <translation>最近使ったフォルダーをクリア</translation>
    </message>
    <message>
        <location line="+56"/>
        <source>Copy Folder Path</source>
        <translation>フォルダーパスをコピー</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Folder path copied</source>
        <translation>フォルダーパスをコピーしました</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Open in File Explorer</source>
        <translation>エクスプローラーで開く</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>File is missing</source>
        <translation>ファイルが見つかりません</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Folder is missing</source>
        <translation>フォルダーが見つかりません</translation>
    </message>
    <message>
        <location line="-14315"/>
        <source>&amp;Rulers</source>
        <translation>定規(&amp;R)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Grid</source>
        <translation>グリッド(&amp;G)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Guides</source>
        <translation>ガイド(&amp;G)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Snap</source>
        <translation>スナップ(&amp;S)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lock Guides</source>
        <translation>ガイドをロック</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Snap &amp;To</source>
        <translation>スナップ先(&amp;T)</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+4931"/>
        <source>Guides</source>
        <translation>ガイド</translation>
    </message>
    <message>
        <location line="-4930"/>
        <location line="+4933"/>
        <source>Grid</source>
        <translation>グリッド</translation>
    </message>
    <message>
        <location line="-4932"/>
        <source>Document Bounds and Center</source>
        <translation>ドキュメントの境界と中心</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Layer Bounds and Centers</source>
        <translation>レイヤーの境界と中心</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Selection Bounds and Center</source>
        <translation>選択範囲の境界と中心</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Guide Operations</source>
        <translation>ガイド操作</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>New Guide...</source>
        <translation>新規ガイド...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>New Guide Layout...</source>
        <translation>新規ガイドレイアウト...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Clear Selected Guides</source>
        <translation>選択中のガイドを消去</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Clear Guides</source>
        <translation>ガイドを消去</translation>
    </message>
    <message>
        <location line="+4835"/>
        <source>Pixels</source>
        <translation>ピクセル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Inches</source>
        <translation>インチ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Centimeters</source>
        <translation>センチメートル</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Show rulers</source>
        <translation>定規を表示</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show grid</source>
        <translation>グリッドを表示</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show guides</source>
        <translation>ガイドを表示</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Lock guides</source>
        <translation>ガイドをロック</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable snapping</source>
        <translation>スナップを有効化</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+313"/>
        <source> px</source>
        <translation> px</translation>
    </message>
    <message>
        <location line="-6007"/>
        <location line="+2482"/>
        <location line="+5361"/>
        <source> pt</source>
        <translation> pt</translation>
    </message>
    <message>
        <location line="-2141"/>
        <source>Lines</source>
        <translation>線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Dots</source>
        <translation>点</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Grid Color</source>
        <translation>グリッド色</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Guide Color</source>
        <translation>ガイド色</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Document bounds and center</source>
        <translation>ドキュメントの境界と中心</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Layer bounds and centers</source>
        <translation>レイヤーの境界と中心</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Selection bounds and center</source>
        <translation>選択範囲の境界と中心</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Ruler units:</source>
        <translation>定規の単位:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Default visibility:</source>
        <translation>既定の表示:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Grid spacing:</source>
        <translation>グリッド間隔:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Grid subdivisions:</source>
        <translation>グリッド分割数:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Grid style:</source>
        <translation>グリッドスタイル:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Grid color:</source>
        <translation>グリッド色:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Guide color:</source>
        <translation>ガイド色:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Overlay preview:</source>
        <translation>オーバーレイプレビュー:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Grid and Guides</source>
        <translation>グリッドとガイド</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Snap:</source>
        <translation>スナップ:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Snap targets:</source>
        <translation>スナップ対象:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Snapping</source>
        <translation>スナップ</translation>
    </message>
    <message>
        <location line="+180"/>
        <source>New Guide</source>
        <translation>新規ガイド</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Vertical</source>
        <translation>垂直</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Horizontal</source>
        <translation>水平</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Orientation:</source>
        <translation>方向:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Position:</source>
        <translation>位置:</translation>
    </message>
    <message>
        <location line="+25"/>
        <location line="+32"/>
        <location line="+3"/>
        <source>New Guide Layout</source>
        <translation>新規ガイドレイアウト</translation>
    </message>
    <message>
        <location line="-22"/>
        <source>Clear existing guides</source>
        <translation>既存のガイドを消去</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Columns:</source>
        <translation>列:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rows:</source>
        <translation>行:</translation>
    </message>
    <message>
        <location line="-113"/>
        <source>Grid Preferences</source>
        <translation>グリッド環境設定</translation>
    </message>
    <message>
        <location line="-2986"/>
        <location line="+7506"/>
        <source>Lock</source>
        <translation>ロック</translation>
    </message>
    <message>
        <location line="-7478"/>
        <location line="+9377"/>
        <location line="+111"/>
        <source>Lock image pixels</source>
        <translation>画像ピクセルをロック</translation>
    </message>
    <message>
        <location line="-9485"/>
        <location line="+9375"/>
        <location line="+111"/>
        <source>Lock position</source>
        <translation>位置をロック</translation>
    </message>
    <message>
        <location line="-9478"/>
        <location line="+9368"/>
        <location line="+111"/>
        <source>Lock all</source>
        <translation>すべてロック</translation>
    </message>
    <message>
        <location line="-1995"/>
        <source>Lock All</source>
        <translation>すべてロック</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Lock Image Pixels</source>
        <translation>画像ピクセルをロック</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Lock Position</source>
        <translation>位置をロック</translation>
    </message>
    <message>
        <location line="+1230"/>
        <location line="+31"/>
        <source>Layer lock enabled</source>
        <translation>レイヤーロックを有効にしました</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+31"/>
        <source>Layer lock disabled</source>
        <translation>レイヤーロックを無効にしました</translation>
    </message>
    <message>
        <location line="-5514"/>
        <location line="+100"/>
        <location line="+343"/>
        <location line="+789"/>
        <location line="+593"/>
        <location line="+167"/>
        <location line="+44"/>
        <location line="+44"/>
        <location line="+26"/>
        <location line="+97"/>
        <location line="+29"/>
        <location line="+29"/>
        <location line="+395"/>
        <location line="+64"/>
        <location line="+1872"/>
        <location line="+82"/>
        <location line="+175"/>
        <location line="+416"/>
        <location filename="../src/ui/main_window_adjustments.cpp" line="-1091"/>
        <location line="+246"/>
        <location line="+184"/>
        <location line="+494"/>
        <source>Layer pixels are locked.</source>
        <translation>レイヤーの画像ピクセルはロックされています。</translation>
    </message>
    <message>
        <location line="-4909"/>
        <location line="+18"/>
        <source>Layer position is locked.</source>
        <translation>レイヤーの位置はロックされています。</translation>
    </message>
    <message>
        <location line="+4136"/>
        <source>Target layer pixels are locked. Unlock image pixels to merge down.</source>
        <translation>結合先レイヤーの画像ピクセルはロックされています。下のレイヤーと結合するには画像ピクセルのロックを解除してください。</translation>
    </message>
    <message>
        <location line="+1862"/>
        <source>transparent</source>
        <translation>透明</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>image pixels</source>
        <translation>画像ピクセル</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>position</source>
        <translation>位置</translation>
    </message>
    <message>
        <location line="+2"/>
        <source> | Locks: %1</source>
        <translation> | ロック: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Layer: %1 | %2 | Mode: %3 | Opacity: %4% | %5%6</source>
        <translation>レイヤー: %1 | %2 | モード: %3 | 不透明度: %4% | %5%6</translation>
    </message>
    <message>
        <location line="-10608"/>
        <source>Tip:</source>
        <translation>先端:</translation>
    </message>
    <message>
        <location line="-1714"/>
        <source>Define Brush Tip from Selection</source>
        <translation>選択範囲からブラシ先端を定義</translation>
    </message>
    <message>
        <location line="+10896"/>
        <source>Brush tip: %1</source>
        <translation>ブラシ先端: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Brush tip: Round</source>
        <translation>ブラシ先端: 丸</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Import Photoshop Brushes</source>
        <translation>Photoshop ブラシを読み込む</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Photoshop Brushes (*.abr)</source>
        <translation>Photoshop ブラシ (*.abr)</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+5"/>
        <source>Import Brushes</source>
        <translation>ブラシを読み込む</translation>
    </message>
    <message>
        <location line="+64"/>
        <location line="+12"/>
        <source>The selection is empty or too large to use as a brush tip (max 4096px)</source>
        <translation>選択範囲が空か、ブラシ先端として使用するには大きすぎます(最大 4096px)</translation>
    </message>
    <message>
        <location line="-8"/>
        <source>Define Brush Tip</source>
        <translation>ブラシ先端を定義</translation>
    </message>
    <message>
        <location line="+0"/>
        <location filename="../src/ui/main_window_channels.cpp" line="+0"/>
        <source>Name:</source>
        <translation>名前:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Brush %1</source>
        <translation>ブラシ %1</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Defined brush tip: %1</source>
        <translation>ブラシ先端を定義しました: %1</translation>
    </message>
    <message>
        <location line="-10903"/>
        <location line="+11382"/>
        <source>Create Clipping Mask</source>
        <translation>クリッピングマスクを作成</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Release Clipping Mask</source>
        <translation>クリッピングマスクを解除</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Create clipping mask</source>
        <translation>クリッピングマスクを作成</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Release clipping mask</source>
        <translation>クリッピングマスクを解除</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Clipping mask created</source>
        <translation>クリッピングマスクを作成しました</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Clipping mask released</source>
        <translation>クリッピングマスクを解除しました</translation>
    </message>
    <message>
        <location line="-27"/>
        <source>Create Clipping Mask needs a pixel layer below</source>
        <translation>クリッピングマスクの作成には下にピクセルレイヤーが必要です</translation>
    </message>
    <message>
        <location line="-11416"/>
        <location line="+10129"/>
        <source>View Layer Mask</source>
        <translation>レイヤーマスクを表示</translation>
    </message>
    <message>
        <location line="+1662"/>
        <source>%1
%2% opacity%3%4%5%6%7%8</source>
        <translation>%1
不透明度 %2%%3%4%5%6%7%8</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>
Clipped to the layer below</source>
        <translation>
下のレイヤーにクリップされています</translation>
    </message>
    <message>
        <location filename="../src/ui/main_window_channels.cpp" line="-275"/>
        <location line="+244"/>
        <source>Composite</source>
        <translation>コンポジット</translation>
    </message>
    <message>
        <location line="-242"/>
        <location line="+245"/>
        <location line="+125"/>
        <source>Red</source>
        <translation>赤</translation>
    </message>
    <message>
        <location line="-368"/>
        <location line="+246"/>
        <location line="+125"/>
        <source>Green</source>
        <translation>緑</translation>
    </message>
    <message>
        <location line="-369"/>
        <location line="+247"/>
        <location line="+125"/>
        <source>Blue</source>
        <translation>青</translation>
    </message>
    <message>
        <location line="-306"/>
        <source>Showing composite</source>
        <translation>コンポジットを表示しています</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Previewing Red channel (read-only)</source>
        <translation>赤チャンネルをプレビューしています（読み取り専用）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Previewing Green channel (read-only)</source>
        <translation>緑チャンネルをプレビューしています（読み取り専用）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Previewing Blue channel (read-only)</source>
        <translation>青チャンネルをプレビューしています（読み取り専用）</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Showing channel overlay: %1</source>
        <translation>チャンネルオーバーレイを表示しています: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Previewing spot channel (read-only): %1</source>
        <translation>スポットカラーチャンネルをプレビューしています（読み取り専用）: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Editing channel: %1</source>
        <translation>チャンネルを編集中: %1</translation>
    </message>
    <message>
        <location line="+44"/>
        <location line="+23"/>
        <source>This document has reached the Photoshop channel limit</source>
        <translation>このドキュメントは Photoshop のチャンネル数上限に達しています</translation>
    </message>
    <message>
        <location line="-15"/>
        <source>New channel</source>
        <translation>新規チャンネル</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Created channel %1</source>
        <translation>チャンネル %1 を作成しました</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Save selection as channel</source>
        <translation>選択範囲をチャンネルとして保存</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Saved selection as %1</source>
        <translation>選択範囲を %1 として保存しました</translation>
    </message>
    <message>
        <location line="+89"/>
        <source>Load channel as selection</source>
        <translation>チャンネルを選択範囲として読み込み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Loaded %1 as selection</source>
        <translation>%1 を選択範囲として読み込みました</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Rename channel</source>
        <translation>チャンネル名を変更</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Renamed channel to %1</source>
        <translation>チャンネル名を %1 に変更しました</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Invert channel</source>
        <translation>チャンネルを反転</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Inverted channel</source>
        <translation>チャンネルを反転しました</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Delete channel</source>
        <translation>チャンネルを削除</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Deleted channel %1</source>
        <translation>チャンネル %1 を削除しました</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Reorder channels</source>
        <translation>チャンネルを並べ替え</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Reordered channels</source>
        <translation>チャンネルを並べ替えました</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Editing layer mask (click to exit)</source>
        <translation>レイヤーマスクを編集中（クリックして終了）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Paint tools are editing the layer mask. Click to edit the layer pixels again.</source>
        <translation>ペイントツールはレイヤーマスクを編集しています。クリックするとレイヤーピクセルの編集に戻ります。</translation>
    </message>
    <message>
        <location line="+21"/>
        <location line="+12"/>
        <source>Viewing channel: %1 (click to exit)</source>
        <translation>チャンネルを表示中: %1（クリックして終了）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Editing channel: %1 (click to exit)</source>
        <translation>チャンネルを編集中: %1（クリックして終了）</translation>
    </message>
    <message>
        <location line="-12"/>
        <location line="+13"/>
        <source>Click to return to the composite image.</source>
        <translation>クリックするとコンポジット画像に戻ります。</translation>
    </message>
    <message>
        <source>Edit in &amp;Quick Mask Mode</source>
        <translation>クイックマスクモードで編集(&amp;Q)</translation>
    </message>
    <message>
        <source>Edit in Quick Mask Mode (Q)</source>
        <translation>クイックマスクモードで編集 (Q)</translation>
    </message>
    <message>
        <source>Filters are unavailable in Quick Mask mode</source>
        <translation>フィルターはクイックマスクモードでは使用できません</translation>
    </message>
    <message>
        <source>Clear Quick Mask</source>
        <translation>クイックマスクを消去</translation>
    </message>
    <message>
        <source>Quick Mask</source>
        <translation>クイックマスク</translation>
    </message>
    <message>
        <source>Editing Quick Mask</source>
        <translation>クイックマスクを編集中</translation>
    </message>
    <message>
        <source>Editing Quick Mask (click to exit)</source>
        <translation>クイックマスクを編集中（クリックして終了）</translation>
    </message>
    <message>
        <source>Paint white to select, black to mask, or gray for partial selection.</source>
        <translation>白で選択、黒でマスク、グレーで部分選択を描画します。</translation>
    </message>
    <message>
        <source>Entered Quick Mask mode</source>
        <translation>クイックマスクモードに入りました</translation>
    </message>
    <message>
        <source>Exited Quick Mask mode</source>
        <translation>クイックマスクモードを終了しました</translation>
    </message>
    <message>
        <source>Convert for Smart Filters</source>
        <translation>スマートフィルター用に変換</translation>
    </message>
    <message>
        <source>Convert the active layer to a Smart Object for editable filters</source>
        <translation>アクティブなレイヤーをスマートオブジェクトに変換してフィルターを編集可能にします</translation>
    </message>
    <message>
        <source>Select a normal pixel layer to convert for Smart Filters</source>
        <translation>スマートフィルター用に変換する通常のピクセルレイヤーを選択してください</translation>
    </message>
    <message>
        <source>This Smart Object can only preserve its imported filters</source>
        <translation>このスマートオブジェクトでは読み込んだフィルターの保持のみ可能です</translation>
    </message>
    <message>
        <source>Multiple editable Smart Filters are not available yet</source>
        <translation>複数の編集可能なスマートフィルターはまだ利用できません</translation>
    </message>
    <message>
        <source>This Smart Filter can only be preserved, not edited</source>
        <translation>このスマートフィルターは保持のみ可能で、編集できません</translation>
    </message>
    <message>
        <source>Could not render this Smart Object</source>
        <translation>このスマートオブジェクトを描画できませんでした</translation>
    </message>
    <message>
        <source>Smart Filter preview failed: %1</source>
        <translation>スマートフィルターのプレビューに失敗しました: %1</translation>
    </message>
    <message>
        <source>Cancelled Gaussian Blur</source>
        <translation>ガウスぼかしをキャンセルしました</translation>
    </message>
    <message>
        <source>Cancelled High Pass</source>
        <translation>ハイパスをキャンセルしました</translation>
    </message>
    <message>
        <source>Cancelled Median</source>
        <translation>中央値をキャンセルしました</translation>
    </message>
    <message>
        <source>Cancelled Dust &amp; Scratches</source>
        <translation>ダスト＆スクラッチをキャンセルしました</translation>
    </message>
    <message>
        <source>Cancelled Surface Blur</source>
        <translation>ぼかし（表面）をキャンセルしました</translation>
    </message>
    <message>
        <source>Cancelled Unsharp Mask</source>
        <translation>アンシャープマスクをキャンセルしました</translation>
    </message>
    <message>
        <source>Cancelled Motion Blur</source>
        <translation>モーションぼかしをキャンセルしました</translation>
    </message>
    <message>
        <source>Cancelled Plastic Wrap</source>
        <translation>ラップをキャンセルしました</translation>
    </message>
    <message>
        <source>Smart Filter failed</source>
        <translation>スマートフィルターの処理に失敗しました</translation>
    </message>
    <message>
        <source>This Smart Filter descriptor cannot be edited safely</source>
        <translation>このスマートフィルターのディスクリプターは安全に編集できません</translation>
    </message>
    <message>
        <source>Smart Filter cache data could not be written safely</source>
        <translation>スマートフィルターのキャッシュデータを安全に書き込めませんでした</translation>
    </message>
    <message>
        <source>Add Gaussian Blur Smart Filter</source>
        <translation>ガウスぼかしスマートフィルターを追加</translation>
    </message>
    <message>
        <source>Edit Gaussian Blur Smart Filter</source>
        <translation>ガウスぼかしスマートフィルターを編集</translation>
    </message>
    <message>
        <source>Added Gaussian Blur as a Smart Filter</source>
        <translation>ガウスぼかしをスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Added another Gaussian Blur Smart Filter</source>
        <translation>ガウスぼかしスマートフィルターをもう1つ追加しました</translation>
    </message>
    <message>
        <source>Updated Gaussian Blur Smart Filter</source>
        <translation>ガウスぼかしスマートフィルターを更新しました</translation>
    </message>
    <message>
        <source>Add High Pass Smart Filter</source>
        <translation>ハイパススマートフィルターを追加</translation>
    </message>
    <message>
        <source>Edit High Pass Smart Filter</source>
        <translation>ハイパススマートフィルターを編集</translation>
    </message>
    <message>
        <source>Added High Pass as a Smart Filter</source>
        <translation>ハイパスをスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Added another High Pass Smart Filter</source>
        <translation>ハイパススマートフィルターをもう1つ追加しました</translation>
    </message>
    <message>
        <source>Updated High Pass Smart Filter</source>
        <translation>ハイパススマートフィルターを更新しました</translation>
    </message>
    <message>
        <source>Add Median Smart Filter</source>
        <translation>中央値スマートフィルターを追加</translation>
    </message>
    <message>
        <source>Edit Median Smart Filter</source>
        <translation>中央値スマートフィルターを編集</translation>
    </message>
    <message>
        <source>Added Median as a Smart Filter</source>
        <translation>中央値をスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Added another Median Smart Filter</source>
        <translation>中央値スマートフィルターをもう1つ追加しました</translation>
    </message>
    <message>
        <source>Updated Median Smart Filter</source>
        <translation>中央値スマートフィルターを更新しました</translation>
    </message>
    <message>
        <source>Add Dust &amp; Scratches Smart Filter</source>
        <translation>ダスト＆スクラッチスマートフィルターを追加</translation>
    </message>
    <message>
        <source>Edit Dust &amp; Scratches Smart Filter</source>
        <translation>ダスト＆スクラッチスマートフィルターを編集</translation>
    </message>
    <message>
        <source>Added Dust &amp; Scratches as a Smart Filter</source>
        <translation>ダスト＆スクラッチをスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Added another Dust &amp; Scratches Smart Filter</source>
        <translation>ダスト＆スクラッチスマートフィルターをもう1つ追加しました</translation>
    </message>
    <message>
        <source>Updated Dust &amp; Scratches Smart Filter</source>
        <translation>ダスト＆スクラッチスマートフィルターを更新しました</translation>
    </message>
    <message>
        <source>Add Surface Blur Smart Filter</source>
        <translation>ぼかし（表面）スマートフィルターを追加</translation>
    </message>
    <message>
        <source>Edit Surface Blur Smart Filter</source>
        <translation>ぼかし（表面）スマートフィルターを編集</translation>
    </message>
    <message>
        <source>Added Surface Blur as a Smart Filter</source>
        <translation>ぼかし（表面）をスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Added another Surface Blur Smart Filter</source>
        <translation>別のぼかし（表面）スマートフィルターを追加しました</translation>
    </message>
    <message>
        <source>Updated Surface Blur Smart Filter</source>
        <translation>ぼかし（表面）スマートフィルターを更新しました</translation>
    </message>
    <message>
        <source>Add Plastic Wrap Smart Filter</source>
        <translation>ラップスマートフィルターを追加</translation>
    </message>
    <message>
        <source>Edit Plastic Wrap Smart Filter</source>
        <translation>ラップスマートフィルターを編集</translation>
    </message>
    <message>
        <source>Added Plastic Wrap as a Smart Filter</source>
        <translation>ラップをスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Added another Plastic Wrap Smart Filter</source>
        <translation>ラップスマートフィルターをもう1つ追加しました</translation>
    </message>
    <message>
        <source>Updated Plastic Wrap Smart Filter</source>
        <translation>ラップスマートフィルターを更新しました</translation>
    </message>
    <message>
        <source>Cancelled Mosaic</source>
        <translation>モザイクをキャンセルしました</translation>
    </message>
    <message>
        <source>Add Mosaic Smart Filter</source>
        <translation>モザイクスマートフィルターを追加</translation>
    </message>
    <message>
        <source>Edit Mosaic Smart Filter</source>
        <translation>モザイクスマートフィルターを編集</translation>
    </message>
    <message>
        <source>Added Mosaic as a Smart Filter</source>
        <translation>モザイクをスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Added another Mosaic Smart Filter</source>
        <translation>モザイクスマートフィルターをもう1つ追加しました</translation>
    </message>
    <message>
        <source>Updated Mosaic Smart Filter</source>
        <translation>モザイクスマートフィルターを更新しました</translation>
    </message>
    <message>
        <source>Cancelled Box Blur</source>
        <translation>ぼかし（ボックス）をキャンセルしました</translation>
    </message>
    <message>
        <source>Cancelled Radial Blur</source>
        <translation>放射状ぼかしをキャンセルしました</translation>
    </message>
    <message>
        <source>Cancelled Add Noise</source>
        <translation>ノイズを加えるをキャンセルしました</translation>
    </message>
    <message>
        <source>Add Radial Blur Smart Filter</source>
        <translation>放射状ぼかしスマートフィルターを追加</translation>
    </message>
    <message>
        <source>Edit Radial Blur Smart Filter</source>
        <translation>放射状ぼかしスマートフィルターを編集</translation>
    </message>
    <message>
        <source>Add Add Noise Smart Filter</source>
        <translation>ノイズを加えるスマートフィルターを追加</translation>
    </message>
    <message>
        <source>Edit Add Noise Smart Filter</source>
        <translation>ノイズを加えるスマートフィルターを編集</translation>
    </message>
    <message>
        <source>Added Radial Blur as a Smart Filter</source>
        <translation>放射状ぼかしをスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Added another Radial Blur Smart Filter</source>
        <translation>放射状ぼかしスマートフィルターをもう1つ追加しました</translation>
    </message>
    <message>
        <source>Updated Radial Blur Smart Filter</source>
        <translation>放射状ぼかしスマートフィルターを更新しました</translation>
    </message>
    <message>
        <source>Added Add Noise as a Smart Filter</source>
        <translation>ノイズを加えるをスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Added another Add Noise Smart Filter</source>
        <translation>ノイズを加えるスマートフィルターをもう1つ追加しました</translation>
    </message>
    <message>
        <source>Updated Add Noise Smart Filter</source>
        <translation>ノイズを加えるスマートフィルターを更新しました</translation>
    </message>
    <message>
        <source>Add Box Blur Smart Filter</source>
        <translation>ぼかし（ボックス）スマートフィルターを追加</translation>
    </message>
    <message>
        <source>Edit Box Blur Smart Filter</source>
        <translation>ぼかし（ボックス）スマートフィルターを編集</translation>
    </message>
    <message>
        <source>Added Box Blur as a Smart Filter</source>
        <translation>ぼかし（ボックス）をスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Added another Box Blur Smart Filter</source>
        <translation>ぼかし（ボックス）スマートフィルターをもう1つ追加しました</translation>
    </message>
    <message>
        <source>Updated Box Blur Smart Filter</source>
        <translation>ぼかし（ボックス）スマートフィルターを更新しました</translation>
    </message>
    <message>
        <source>Cancelled Emboss</source>
        <translation>エンボスをキャンセルしました</translation>
    </message>
    <message>
        <source>Add Emboss Smart Filter</source>
        <translation>エンボススマートフィルターを追加</translation>
    </message>
    <message>
        <source>Edit Emboss Smart Filter</source>
        <translation>エンボススマートフィルターを編集</translation>
    </message>
    <message>
        <source>Added Emboss as a Smart Filter</source>
        <translation>エンボスをスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Added another Emboss Smart Filter</source>
        <translation>エンボススマートフィルターをもう1つ追加しました</translation>
    </message>
    <message>
        <source>Updated Emboss Smart Filter</source>
        <translation>エンボススマートフィルターを更新しました</translation>
    </message>
    <message>
        <source>Add Unsharp Mask Smart Filter</source>
        <translation>アンシャープマスクスマートフィルターを追加</translation>
    </message>
    <message>
        <source>Edit Unsharp Mask Smart Filter</source>
        <translation>アンシャープマスクスマートフィルターを編集</translation>
    </message>
    <message>
        <source>Added Unsharp Mask as a Smart Filter</source>
        <translation>アンシャープマスクをスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Added another Unsharp Mask Smart Filter</source>
        <translation>アンシャープマスクスマートフィルターをもう1つ追加しました</translation>
    </message>
    <message>
        <source>Updated Unsharp Mask Smart Filter</source>
        <translation>アンシャープマスクスマートフィルターを更新しました</translation>
    </message>
    <message>
        <source>Add Motion Blur Smart Filter</source>
        <translation>モーションぼかしスマートフィルターを追加</translation>
    </message>
    <message>
        <source>Edit Motion Blur Smart Filter</source>
        <translation>モーションぼかしスマートフィルターを編集</translation>
    </message>
    <message>
        <source>Added Motion Blur as a Smart Filter</source>
        <translation>モーションぼかしをスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Added another Motion Blur Smart Filter</source>
        <translation>モーションぼかしスマートフィルターをもう1つ追加しました</translation>
    </message>
    <message>
        <source>Updated Motion Blur Smart Filter</source>
        <translation>モーションぼかしスマートフィルターを更新しました</translation>
    </message>
    <message>
        <source>Show Smart Filters</source>
        <translation>スマートフィルターを表示</translation>
    </message>
    <message>
        <source>Hide Smart Filters</source>
        <translation>スマートフィルターを非表示</translation>
    </message>
    <message>
        <source>Show Smart Filter</source>
        <translation>スマートフィルターを表示</translation>
    </message>
    <message>
        <source>Hide Smart Filter</source>
        <translation>スマートフィルターを非表示</translation>
    </message>
    <message>
        <source>Smart Filters shown</source>
        <translation>スマートフィルターを表示しました</translation>
    </message>
    <message>
        <source>Smart Filters hidden</source>
        <translation>スマートフィルターを非表示にしました</translation>
    </message>
    <message>
        <source>Smart Filter shown</source>
        <translation>スマートフィルターを表示しました</translation>
    </message>
    <message>
        <source>Smart Filter hidden</source>
        <translation>スマートフィルターを非表示にしました</translation>
    </message>
    <message>
        <source>Duplicate Smart Filter</source>
        <translation>スマートフィルターを複製</translation>
    </message>
    <message>
        <source>Duplicated Smart Filter</source>
        <translation>スマートフィルターを複製しました</translation>
    </message>
    <message>
        <source>Reorder Smart Filters</source>
        <translation>スマートフィルターを並べ替え</translation>
    </message>
    <message>
        <source>Reordered Smart Filters</source>
        <translation>スマートフィルターを並べ替えました</translation>
    </message>
    <message>
        <source>Editing multiple Smart Filters is not available yet</source>
        <translation>複数のスマートフィルターの編集はまだ利用できません</translation>
    </message>
    <message>
        <source>Smart Filter cache data could not be removed safely</source>
        <translation>スマートフィルターのキャッシュデータを安全に削除できませんでした</translation>
    </message>
    <message>
        <source>Delete Smart Filter</source>
        <translation>スマートフィルターを削除</translation>
    </message>
    <message>
        <source>Deleted Smart Filter</source>
        <translation>スマートフィルターを削除しました</translation>
    </message>
    <message>
        <source>This filter is not currently editable as a Smart Filter</source>
        <translation>このフィルターは現在、スマートフィルターとして編集できません</translation>
    </message>
    <message>
        <source>%1 has no editable Photoshop Smart Filter mapping. Rasterize the Smart Object and apply the filter destructively?</source>
        <translation>%1 には編集可能な Photoshop スマートフィルターの対応がありません。スマートオブジェクトをラスタライズして、フィルターを破壊的に適用しますか？</translation>
    </message>
    <message>
        <source>Add Smart Filter Stack</source>
        <translation>スマートフィルタースタックを追加</translation>
    </message>
    <message>
        <source>Added %1 as editable Smart Filters</source>
        <translation>%1 を編集可能なスマートフィルターとして追加しました</translation>
    </message>
    <message>
        <source>Rasterize Smart Object?</source>
        <translation>スマートオブジェクトをラスタライズしますか？</translation>
    </message>
    <message>
        <source>This Look includes effects without an editable Photoshop Smart Filter mapping. Rasterize the Smart Object and apply the complete Look destructively?</source>
        <translation>このルックには、編集可能な Photoshop スマートフィルターへの対応がないエフェクトが含まれています。スマートオブジェクトをラスタライズして、ルック全体を破壊的に適用しますか？</translation>
    </message>
    <message>
        <source>Rasterized Smart Object and applied %1</source>
        <translation>スマートオブジェクトをラスタライズして %1 を適用しました</translation>
    </message>
    <message>
        <source>This filter stack has effects without a verified native Smart Filter mapping. Rasterize the Smart Object to apply it destructively.</source>
        <translation>このフィルタースタックには、確認済みのネイティブスマートフィルター対応がないエフェクトが含まれています。破壊的に適用するにはスマートオブジェクトをラスタライズしてください。</translation>
    </message>
    <message>
        <source>Could not rebuild the Smart Filter preview and cache</source>
        <translation>スマートフィルターのプレビューとキャッシュを再構築できませんでした</translation>
    </message>
    <message>
        <source>Editable Smart Filters currently support documents up to 64 megapixels</source>
        <translation>編集可能なスマートフィルターは現在、最大64メガピクセルのドキュメントに対応しています</translation>
    </message>
    <message>
        <source>Smart Objects with Smart Filters cannot be wrapped in another Smart Object yet</source>
        <translation>スマートフィルター付きスマートオブジェクトは、まだ別のスマートオブジェクト内にまとめられません</translation>
    </message>
    <message>
        <source>Load Smart Filter mask selection</source>
        <translation>スマートフィルターマスクの選択範囲を読み込み</translation>
    </message>
    <message>
        <source>Loaded the Smart Filter mask as a selection</source>
        <translation>スマートフィルターマスクを選択範囲として読み込みました</translation>
    </message>
    <message>
        <source>Editing Smart Filter mask</source>
        <translation>スマートフィルターマスクを編集中</translation>
    </message>
    <message>
        <source>Showing the Smart Filter mask. Alt-click the mask thumbnail to return.</source>
        <translation>スマートフィルターマスクを表示中です。マスクサムネイルを Alt+クリックすると戻ります。</translation>
    </message>
    <message>
        <source>This Smart Filter mask can only be preserved, not edited</source>
        <translation>このスマートフィルターマスクは保持のみ可能で、編集できません</translation>
    </message>
    <message>
        <source>Could not prepare this Smart Filter mask</source>
        <translation>このスマートフィルターマスクを編集用に準備できませんでした</translation>
    </message>
    <message>
        <source>Could not edit this Smart Filter mask</source>
        <translation>このスマートフィルターマスクを編集できませんでした</translation>
    </message>
    <message>
        <source>Invert Smart Filter mask</source>
        <translation>スマートフィルターマスクを反転</translation>
    </message>
    <message>
        <source>Filled Smart Filter mask</source>
        <translation>スマートフィルターマスクを塗りつぶしました</translation>
    </message>
    <message>
        <source>Clear Smart Filter mask</source>
        <translation>スマートフィルターマスクを消去</translation>
    </message>
    <message>
        <source>Cleared Smart Filter mask</source>
        <translation>スマートフィルターマスクを消去しました</translation>
    </message>
    <message>
        <source>Enable Smart Filter Mask</source>
        <translation>スマートフィルターマスクを有効化</translation>
    </message>
    <message>
        <source>Disable Smart Filter Mask</source>
        <translation>スマートフィルターマスクを無効化</translation>
    </message>
    <message>
        <source>Smart Filter mask enabled</source>
        <translation>スマートフィルターマスクを有効にしました</translation>
    </message>
    <message>
        <source>Smart Filter mask disabled</source>
        <translation>スマートフィルターマスクを無効にしました</translation>
    </message>
    <message>
        <source>Updated Smart Filter mask</source>
        <translation>スマートフィルターマスクを更新しました</translation>
    </message>
    <message>
        <source>This Smart Filter mask cannot be edited safely</source>
        <translation>このスマートフィルターマスクは安全に編集できません</translation>
    </message>
    <message>
        <source>Editing Smart Filter mask (click to exit)</source>
        <translation>スマートフィルターマスクを編集中（クリックして終了）</translation>
    </message>
    <message>
        <source>Paint tools are editing the shared Smart Filter mask. Click to edit the layer pixels again.</source>
        <translation>ペイントツールは共有スマートフィルターマスクを編集しています。クリックするとレイヤーピクセルの編集に戻ります。</translation>
    </message>
    <message>
        <source>Rasterize Smart Objects before changing document geometry</source>
        <translation>ドキュメントのジオメトリを変更する前にスマートオブジェクトをラスタライズしてください</translation>
    </message>
    <message>
        <source>Use Free Transform or rasterize Smart Objects before flipping</source>
        <translation>スマートオブジェクトを反転するには、自由変形を使用するかラスタライズしてください</translation>
    </message>
    <message>
        <source>Rasterize the Smart Object before applying destructive filters or adjustments</source>
        <translation>破壊的なフィルターや色調補正を適用する前に、スマートオブジェクトをラスタライズしてください</translation>
    </message>
    <message>
        <source>Text, Smart Object, and Shape pixels cannot be filled. Rasterize the layer first.</source>
        <translation>テキスト、スマートオブジェクト、シェイプのピクセルは塗りつぶせません。先にレイヤーをラスタライズしてください。</translation>
    </message>
    <message>
        <source>Rasterize Text, Smart Object, and Shape layers before editing their pixels</source>
        <translation>ピクセルを編集する前に、テキスト、スマートオブジェクト、シェイプのレイヤーをラスタライズしてください</translation>
    </message>
    <message>
        <source>Rasterize Smart Objects before changing palette pixels</source>
        <translation>パレットのピクセルを変更する前に、スマートオブジェクトをラスタライズしてください</translation>
    </message>
    <message>
        <source>Fill Opacity</source>
        <translation>塗りの不透明度</translation>
    </message>
    <message>
        <source>Fill Opacity Will Be Discarded</source>
        <translation>塗りの不透明度は破棄されます</translation>
    </message>
    <message>
        <source>Aseprite files cannot store Photoshop Fill Opacity. Continue saving without Fill Opacity?</source>
        <translation>Aseprite ファイルには Photoshop の塗りの不透明度を保存できません。塗りの不透明度を破棄して保存を続けますか？</translation>
    </message>
    <message><source>Detail Tools</source><translation>ディテールツール</translation></message>
    <message><source>Mixer Brush</source><translation>ミキサーブラシ</translation></message>
    <message><source>Wet: %1% | Load: %2% | Mix: %3% | Flow: %4%</source><translation>ウェット: %1% | 絵の具量: %2% | ミックス: %3% | フロー: %4%</translation></message>
    <message><source>Toning Tools</source><translation>色調補正ツール</translation></message>
    <message><source>Dodge</source><translation>覆い焼き</translation></message>
    <message><source>Burn</source><translation>焼き込み</translation></message>
    <message><source>Sponge</source><translation>スポンジ</translation></message>
    <message><source>Blur</source><translation>ぼかし</translation></message>
    <message><source>Sharpen</source><translation>シャープ</translation></message>
    <message><source>Strength:</source><translation>強さ:</translation></message>
    <message><source>Strength: %1%</source><translation>強さ: %1%</translation></message>
    <message><source>Maximum adjustment applied during one stroke</source><translation>1回のストロークで適用する補正の最大強度</translation></message>
    <message><source>Range:</source><translation>範囲:</translation></message>
    <message><source>Shadows</source><translation>シャドウ</translation></message>
    <message><source>Midtones</source><translation>中間調</translation></message>
    <message><source>Highlights</source><translation>ハイライト</translation></message>
    <message><source>Protect Tones</source><translation>色調を保護</translation></message>
    <message><source>Preserve local color differences while lightening or darkening</source><translation>明るさを変更するときに局所的な色の差を保護します</translation></message>
    <message><source>Mode:</source><translation>モード:</translation></message>
    <message><source>Saturate</source><translation>彩度を上げる</translation></message>
    <message><source>Desaturate</source><translation>彩度を下げる</translation></message>
    <message><source>Vibrance</source><translation>自然な彩度</translation></message>
    <message><source>Reduce the adjustment on colors that are already strongly saturated</source><translation>すでに彩度が高い色への補正を弱めます</translation></message>
    <message><source>Pattern Stamp</source><translation>パターンスタンプ</translation></message>
    <message><source>Stamp Tools</source><translation>スタンプツール</translation></message>
    <message><source>Fill Tools</source><translation>塗りつぶしツール</translation></message>
    <message><source>Pattern:</source><translation>パターン:</translation></message>
    <message><source>Manage...</source><translation>管理...</translation></message>
    <message><source>Import or manage patterns</source><translation>パターンを読み込むか管理します</translation></message>
    <message><source>Keep pattern alignment continuous across strokes</source><translation>ストローク間でパターンの配置を連続させます</translation></message>
    <message><source>&amp;Liquify...</source><translation>ゆがみ(&amp;L)...</translation></message>
    <message><source>Push, pull, twist, pucker, or bloat pixels with a brush</source><translation>ブラシでピクセルを押す、引く、ねじる、収縮、膨張します</translation></message>
    <message><source>Liquify is unavailable in Quick Mask mode</source><translation>クイックマスクモードではゆがみを使用できません</translation></message>
    <message><source>Liquify is unavailable while viewing a document channel</source><translation>ドキュメントチャンネルの表示中はゆがみを使用できません</translation></message>
    <message><source>Rasterize the Smart Object before using Liquify</source><translation>ゆがみを使用する前にスマートオブジェクトをラスタライズしてください</translation></message>
    <message><source>Cancelled Liquify</source><translation>ゆがみをキャンセルしました</translation></message>
    <message><source>Liquify made no changes</source><translation>ゆがみによる変更はありません</translation></message>
    <message><source>Applying Liquify...</source><translation>ゆがみを適用しています...</translation></message>
    <message><source>Liquify</source><translation>ゆがみ</translation></message>
    <message><source>Applied Liquify</source><translation>ゆがみを適用しました</translation></message>
    <message><source>Shape</source><translation>シェイプ</translation></message>
    <message><source>Path</source><translation>パス</translation></message>
    <message><source>Pixels</source><translation>ピクセル</translation></message>
    <message><source>What the shape tools create: a shape layer, work-path subpaths, or raster pixels</source><translation>シェイプツールが作成する対象: シェイプレイヤー、作業用パスのサブパス、またはラスターピクセル</translation></message>
    <message><source>Fill:</source><translation>塗り:</translation></message>
    <message><source>Shape fill: none, solid color, gradient, or pattern</source><translation>シェイプの塗り: なし、ベタ塗り、グラデーション、またはパターン</translation></message>
    <message><source>Shape Fill Color</source><translation>シェイプの塗りの色</translation></message>
    <message><source>Stroke</source><translation>線</translation></message>
    <message><source>Stroke the shape outline</source><translation>シェイプの輪郭に線を描きます</translation></message>
    <message><source>Shape stroke: solid color, gradient, or pattern</source><translation>シェイプの線: ベタ塗り、グラデーション、またはパターン</translation></message>
    <message><source>Shape Stroke Color</source><translation>シェイプの線の色</translation></message>
    <message><source>Stroke width</source><translation>線の太さ</translation></message>
    <message><source>No Fill</source><translation>塗りなし</translation></message>
    <message><source>Solid Color...</source><translation>ベタ塗り...</translation></message>
    <message><source>Gradient...</source><translation>グラデーション...</translation></message>
    <message><source>Pattern...</source><translation>パターン...</translation></message>
    <message><source>Scale:</source><translation>比率:</translation></message>
    <message><source>Angle:</source><translation>角度:</translation></message>
    <message><source>Offset X:</source><translation>オフセット X:</translation></message>
    <message><source>Offset Y:</source><translation>オフセット Y:</translation></message>
    <message><source>Align with layer</source><translation>レイヤーに整列</translation></message>
    <message><source>Anchor the tile grid to the layer&apos;s position; unchecked anchors it to the document origin</source><translation>タイルの基準をレイヤーの位置に合わせます。オフの場合はドキュメントの原点に合わせます</translation></message>
    <message><source>Weight:</source><translation>太さ:</translation></message>
    <message><source>Line thickness</source><translation>ラインの太さ</translation></message>
    <message><source>Combine:</source><translation>結合:</translation></message>
    <message><source>How the next shape combines with the active shape layer or work path</source><translation>次のシェイプをアクティブなシェイプレイヤーまたは作業用パスとどのように結合するか</translation></message>
    <message><source>Add</source><translation>結合</translation></message>
    <message><source>Subtract</source><translation>型抜き</translation></message>
    <message><source>Intersect</source><translation>交差</translation></message>
    <message><source>Exclude</source><translation>中マド</translation></message>
    <message><source>Rectangle %1</source><translation>長方形 %1</translation></message>
    <message><source>Ellipse %1</source><translation>楕円形 %1</translation></message>
    <message><source>Line %1</source><translation>ライン %1</translation></message>
    <message><source>Edit shape</source><translation>シェイプを編集</translation></message>
    <message><source>New shape layer</source><translation>新規シェイプレイヤー</translation></message>
    <message><source>Add to work path</source><translation>作業用パスに追加</translation></message>
    <message><source>Work Path</source><translation>作業用パス</translation></message>
    <message><source>Created shape layer %1.</source><translation>シェイプレイヤー %1 を作成しました。</translation></message>
    <message><source>Shape %1</source><translation>シェイプ %1</translation></message>
    <message><source>Path Select</source><translation>パスコンポーネント選択</translation></message>
    <message><source>Direct Select</source><translation>ダイレクト選択</translation></message>
    <message><source>Path Tools</source><translation>パスツール</translation></message>
    <message><source>Added the path to the work path.</source><translation>パスを作業用パスに追加しました。</translation></message>
    <message><source>Added the shape to the work path.</source><translation>シェイプを作業用パスに追加しました。</translation></message>
    <message><source>Select a shape layer to edit its appearance</source><translation>外観を編集するシェイプレイヤーを選択してください</translation></message>
    <message><source>This shape layer&apos;s vector data is preserved but can&apos;t be edited.</source><translation>このシェイプレイヤーのベクトルデータは保持されていますが編集できません。</translation></message>
    <message><source>Cancelled shape appearance</source><translation>シェイプの外観をキャンセルしました</translation></message>
    <message><source>Shape appearance</source><translation>シェイプの外観</translation></message>
    <message><source>Updated the shape appearance</source><translation>シェイプの外観を更新しました</translation></message>
    <message><source>Color Fill %1</source><translation>カラー塗りつぶし %1</translation></message>
    <message><source>Gradient Fill %1</source><translation>グラデーション塗りつぶし %1</translation></message>
    <message><source>Pattern Fill %1</source><translation>パターン塗りつぶし %1</translation></message>
    <message><source>Created fill layer %1.</source><translation>塗りつぶしレイヤー %1 を作成しました。</translation></message>
    <message><source>New fill layer</source><translation>新規塗りつぶしレイヤー</translation></message>
    <message><source>Cancelled fill layer</source><translation>塗りつぶしレイヤーをキャンセルしました</translation></message>
    <message><source>New Fill Layer</source><translation>新規塗りつぶしレイヤー</translation></message>
    <message><source>No patterns are available.</source><translation>使用できるパターンがありません。</translation></message>
    <message><source>New F&amp;ill Layer</source><translation>新規塗りつぶしレイヤー(&amp;I)</translation></message>
    <message><source>&amp;Solid Color...</source><translation>ベタ塗り(&amp;S)...</translation></message>
    <message><source>&amp;Gradient...</source><translation>グラデーション(&amp;G)...</translation></message>
    <message><source>&amp;Pattern...</source><translation>パターン(&amp;P)...</translation></message>
    <message><source>Select a layer to work with vector masks</source><translation>ベクトルマスクを操作するレイヤーを選択してください</translation></message>
    <message><source>This layer&apos;s vector data is preserved but can&apos;t be edited.</source><translation>このレイヤーのベクトルデータは保持されていますが編集できません。</translation></message>
    <message><source>The active layer has no vector mask</source><translation>アクティブレイヤーにベクトルマスクがありません</translation></message>
    <message><source>The active layer already has a vector mask</source><translation>アクティブレイヤーには既にベクトルマスクがあります</translation></message>
    <message><source>Draw a work path first</source><translation>先に作業用パスを描いてください</translation></message>
    <message><source>Add vector mask</source><translation>ベクトルマスクを追加</translation></message>
    <message><source>Added a vector mask</source><translation>ベクトルマスクを追加しました</translation></message>
    <message><source>Delete vector mask</source><translation>ベクトルマスクを削除</translation></message>
    <message><source>Deleted the vector mask</source><translation>ベクトルマスクを削除しました</translation></message>
    <message><source>Disable vector mask</source><translation>ベクトルマスクを無効化</translation></message>
    <message><source>Enable vector mask</source><translation>ベクトルマスクを有効化</translation></message>
    <message><source>Disabled the vector mask</source><translation>ベクトルマスクを無効にしました</translation></message>
    <message><source>Enabled the vector mask</source><translation>ベクトルマスクを有効にしました</translation></message>
    <message><source>Rasterize vector mask</source><translation>ベクトルマスクをラスタライズ</translation></message>
    <message><source>Rasterized the vector mask into the layer mask</source><translation>ベクトルマスクをレイヤーマスクにラスタライズしました</translation></message>
    <message><source>&amp;Vector Mask</source><translation>ベクトルマスク(&amp;V)</translation></message>
    <message><source>&amp;Reveal All</source><translation>すべての領域を表示(&amp;R)</translation></message>
    <message><source>&amp;Hide All</source><translation>すべての領域を隠す(&amp;H)</translation></message>
    <message><source>&amp;Current Path</source><translation>現在のパス(&amp;C)</translation></message>
    <message><source>&amp;Delete Vector Mask</source><translation>ベクトルマスクを削除(&amp;D)</translation></message>
    <message><source>D&amp;isable Vector Mask</source><translation>ベクトルマスクを無効化(&amp;I)</translation></message>
    <message><source>Ras&amp;terize Vector Mask</source><translation>ベクトルマスクをラスタライズ(&amp;T)</translation></message>
    <message><source>Add to vector mask</source><translation>ベクトルマスクに追加</translation></message>
    <message><source>Load vector mask selection</source><translation>ベクトルマスクを選択範囲として読み込み</translation></message>
    <message><source>Loaded the vector mask as a selection</source><translation>ベクトルマスクを選択範囲として読み込みました</translation></message>
    <message><source>Editing vector mask</source><translation>ベクトルマスクを編集中</translation></message>
    <message><source>Showing the vector mask. Alt-click the thumbnail to return.</source><translation>ベクトルマスクを表示中です。サムネイルを Alt クリックすると戻ります。</translation></message>
    <message><source>Editing the vector mask path with the pen and path tools</source><translation>ペンツールとパスツールでベクトルマスクのパスを編集中</translation></message>
    <message><source>Paths</source><translation>パス</translation></message>
    <message><source>New Path</source><translation>新規パス</translation></message>
    <message><source>Fill Path</source><translation>パスを塗りつぶし</translation></message>
    <message><source>Stroke Path</source><translation>パスの境界線を描く</translation></message>
    <message><source>Make Selection</source><translation>選択範囲を作成</translation></message>
    <message><source>Make Work Path from Selection</source><translation>選択範囲から作業用パスを作成</translation></message>
    <message><source>Duplicate Path</source><translation>パスを複製</translation></message>
    <message><source>Clipping Path</source><translation>クリッピングパス</translation></message>
    <message><source>Select a saved path to use as the clipping path</source><translation>クリッピングパスにする保存済みパスを選択してください</translation></message>
    <message><source>Clipping path</source><translation>クリッピングパス</translation></message>
    <message><source>Set %1 as the clipping path.</source><translation>%1 をクリッピングパスに設定しました。</translation></message>
    <message><source>Cleared the clipping path.</source><translation>クリッピングパスを解除しました。</translation></message>
    <message><source>Delete Path</source><translation>パスを削除</translation></message>
    <message><source>%1 Shape Path</source><translation>%1 シェイプパス</translation></message>
    <message><source>%1 Vector Mask</source><translation>%1 ベクトルマスク</translation></message>
    <message><source>Rename path</source><translation>パス名を変更</translation></message>
    <message><source>Renamed the path to %1.</source><translation>パス名を %1 に変更しました。</translation></message>
    <message><source>Path %1</source><translation>パス %1</translation></message>
    <message><source>Save path</source><translation>パスを保存</translation></message>
    <message><source>Saved the work path as %1.</source><translation>作業用パスを %1 として保存しました。</translation></message>
    <message><source>New path</source><translation>新規パス</translation></message>
    <message><source>Created %1. Draw into it with the Pen tool.</source><translation>%1 を作成しました。ペンツールで描き込んでください。</translation></message>
    <message><source>Select a saved path or the work path to delete</source><translation>削除する保存済みパスまたは作業用パスを選択してください</translation></message>
    <message><source>Delete path</source><translation>パスを削除</translation></message>
    <message><source>Deleted the path</source><translation>パスを削除しました</translation></message>
    <message><source>The path is empty</source><translation>パスが空です</translation></message>
    <message><source>Load path as selection</source><translation>パスを選択範囲として読み込み</translation></message>
    <message><source>Loaded %1 as a selection.</source><translation>%1 を選択範囲として読み込みました。</translation></message>
    <message><source>Select a saved path or the work path to duplicate</source><translation>複製する保存済みパスまたは作業用パスを選択してください</translation></message>
    <message><source>Duplicate path</source><translation>パスを複製</translation></message>
    <message><source>Duplicated the path as %1.</source><translation>パスを %1 として複製しました。</translation></message>
    <message><source>Reorder paths</source><translation>パスを並べ替え</translation></message>
    <message><source>Reordered paths</source><translation>パスを並べ替えました</translation></message>
    <message><source>Make a selection first</source><translation>先に選択範囲を作成してください</translation></message>
    <message><source>Make Work Path</source><translation>作業用パスを作成</translation></message>
    <message><source>Tolerance:</source><translation>許容値:</translation></message>
    <message><source>The selection is too small to trace</source><translation>選択範囲が小さすぎてトレースできません</translation></message>
    <message><source>Show Target &amp;Path</source><translation>ターゲットパスを表示(&amp;P)</translation></message>
    <message><source>Contents:</source><translation>内容:</translation></message>
    <message><source>Pattern</source><translation>パターン</translation></message>
    <message><source>Choose a pattern to fill with</source><translation>塗りつぶすパターンを選択してください</translation></message>
    <message><source>Filled the path</source><translation>パスを塗りつぶしました</translation></message>
    <message><source>Filled the path with the pattern</source><translation>パスをパターンで塗りつぶしました</translation></message>
    <message><source>Strokes along the path with the current brush and the foreground color.</source><translation>現在のブラシと描画色でパスに沿って描画します。</translation></message>
    <message><source>Simulate pressure</source><translation>筆圧をシミュレート</translation></message>
    <message><source>Tapers the stroke from thin to full and back, as if drawn with a pressure pen.</source><translation>筆圧ペンで描いたように、細く始まり太くなって細く終わるストロークにします。</translation></message>
    <message><source>Stroked the path with the current brush</source><translation>現在のブラシでパスの境界線を描きました</translation></message>
    <message><source>Make work path</source><translation>作業用パスを作成</translation></message>
    <message><source>Made a work path from the selection.</source><translation>選択範囲から作業用パスを作成しました。</translation></message>
    <message><source>Select a path to fill</source><translation>塗りつぶすパスを選択してください</translation></message>
    <message><source>Fill path</source><translation>パスを塗りつぶし</translation></message>
    <message><source>Filled the path with the foreground color</source><translation>パスを描画色で塗りつぶしました</translation></message>
    <message><source>Select a path to stroke</source><translation>境界線を描くパスを選択してください</translation></message>
    <message><source>Stroke path</source><translation>パスの境界線を描く</translation></message>
    <message><source>Stroked the path with the foreground color</source><translation>パスの境界線を描画色で描きました</translation></message>
    <message><source>Select a path to convert</source><translation>変換するパスを選択してください</translation></message>
    <message><source>Operation:</source><translation>処理:</translation></message>
    <message><source>Intersect with Selection</source><translation>選択範囲と交差</translation></message>
    <message><source>Make selection from path</source><translation>パスから選択範囲を作成</translation></message>
    <message><source>Made a selection from the path</source><translation>パスから選択範囲を作成しました</translation></message>
    <message><source>Add to path</source><translation>パスに追加</translation></message>
    <message><source>Added the shape to %1.</source><translation>シェイプを %1 に追加しました。</translation></message>
    <message><source>Polygon</source><translation>多角形</translation></message>
    <message><source>Custom Shape</source><translation>カスタムシェイプ</translation></message>
    <message><source>Polygon %1</source><translation>多角形 %1</translation></message>
    <message><source>Custom Shape %1</source><translation>カスタムシェイプ %1</translation></message>
    <message><source>Sides:</source><translation>角数:</translation></message>
    <message><source>Star inset:</source><translation>星の切り込み:</translation></message>
    <message><source>0 makes a plain polygon; higher values pull in star points</source><translation>0 で通常の多角形、値を上げると星形の切り込みが深くなります</translation></message>
    <message><source>Shape:</source><translation>シェイプ:</translation></message>
    <message><source>Arrow start</source><translation>開始点に矢印</translation></message>
    <message><source>Arrow end</source><translation>終了点に矢印</translation></message>
    <message><source>Add an arrowhead at the line start</source><translation>ラインの開始点に矢じりを付けます</translation></message>
    <message><source>Add an arrowhead at the line end</source><translation>ラインの終了点に矢じりを付けます</translation></message>
    <message><source>Define Custom Shape from Path</source><translation>パスからカスタムシェイプを定義</translation></message>
    <message><source>Define Custom Shape</source><translation>カスタムシェイプを定義</translation></message>
    <message><source>Cancelled defining a custom shape</source><translation>カスタムシェイプの定義をキャンセルしました</translation></message>
    <message><source>Edit Shape Appearance...</source><translation>シェイプの外観を編集...</translation></message>
    <message><source>Select a path or shape layer to define a custom shape</source><translation>カスタムシェイプを定義するパスまたはシェイプレイヤーを選択してください</translation></message>
    <message><source>The path is too small to define a shape</source><translation>パスが小さすぎてシェイプを定義できません</translation></message>
    <message><source>Could not save the custom shape</source><translation>カスタムシェイプを保存できませんでした</translation></message>
    <message><source>Defined %1 from the path.</source><translation>パスから %1 を定義しました。</translation></message>
    <message><source>Scrip&amp;ts</source><translation>スクリプト(&amp;T)</translation></message>
    <message><source>Script &amp;Editor...</source><translation>スクリプトエディター(&amp;E)...</translation></message>
    <message><source>&amp;Browse Scripts Folder...</source><translation>スクリプトフォルダーを開く(&amp;B)...</translation></message>
    <message><source>A script is already running: %1</source><translation>スクリプトは既に実行中です: %1</translation></message>
    <message><source>Running script %1...</source><translation>スクリプト %1 を実行中...</translation></message>
</context>
<context>
    <name>patchy::ui::PalettePanel</name>
    <message>
        <location filename="../src/ui/palette_panel.cpp" line="-202"/>
        <source>Load a built-in palette</source>
        <translation>内蔵パレットを読み込みます</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Presets...</source>
        <translation>プリセット...</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Load</source>
        <translation>読み込み</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Load a palette file (.pal, .gpl, .hex, .act, .aco, .ase, indexed .bmp)</source>
        <translation>パレットファイルを読み込みます (.pal、.gpl、.hex、.act、.aco、.ase、インデックス .bmp)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Save</source>
        <translation>保存</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Save the palette to a file</source>
        <translation>パレットをファイルに保存します</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Extract</source>
        <translation>抽出</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Build the palette from the image&apos;s colors</source>
        <translation>画像の色からパレットを作成します</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add the foreground color</source>
        <translation>描画色を追加します</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remove the selected color</source>
        <translation>選択した色を削除します</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Copy</source>
        <translation>コピー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy the selected color&apos;s hex code to the clipboard</source>
        <translation>選択した色の 16 進コードをクリップボードにコピーします</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>No palette. Pick a preset, load a palette file, or extract one from the image.</source>
        <translation>パレットがありません。プリセットを選ぶか、パレットファイルを読み込むか、画像から抽出してください。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Convert to Indexed (Palette)...</source>
        <translation>インデックス (パレット) に変換...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Constrain painting to this palette</source>
        <translation>このパレットに描画を制限します</translation>
    </message>
    <message>
        <location line="+59"/>
        <source>Index %1: %2</source>
        <translation>インデックス %1: %2</translation>
    </message>
    <message numerus="yes">
        <location line="+6"/>
        <source>%n colors</source>
        <translation>
            <numerusform>%n 色</numerusform>
        </translation>
    </message>
    <message>
        <location line="+2"/>
        <source> (duplicates)</source>
        <translation> (重複あり)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Two or more entries share the same color. Identical colors cannot be told apart in the artwork: exports and palette remaps always use the first matching index. Nudge one channel by 1 (for example #000000 and #010101) to control indexes separately.</source>
        <translation>同じ色のエントリが複数あります。まったく同じ色はアートワーク上で区別できないため、書き出しやパレットの再マップは常に最初に一致したインデックスを使います。インデックスを個別に扱うには、1 チャンネルだけ 1 ずらして色を一意にしてください (例: #000000 と #010101)。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Edit Color...</source>
        <translation>色を編集...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy Hex Code</source>
        <translation>16 進コードをコピー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove Color</source>
        <translation>色を削除</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Add Foreground Color</source>
        <translation>描画色を追加</translation>
    </message>
</context>
<context>
    <name>patchy::ui::PatchyColorPicker</name>
    <message>
        <location filename="../src/ui/color_panel.cpp" line="+1567"/>
        <source>Basic colors</source>
        <translation>基本色</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Current palette</source>
        <translation>現在のパレット</translation>
    </message>
    <message>
        <location line="-434"/>
        <source>Choose which palette the swatches below show</source>
        <translation>下のスウォッチに表示するパレットを選びます</translation>
    </message>
    <message>
        <location line="+439"/>
        <source>Load Palette File...</source>
        <translation>パレットファイルを読み込み...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Save Palette As...</source>
        <translation>パレットを名前を付けて保存...</translation>
    </message>
    <message>
        <location line="+96"/>
        <source>File: %1</source>
        <translation>ファイル: %1</translation>
    </message>
    <message>
        <location line="-1035"/>
        <source>Current color: drag it to a custom color slot, or drop a color here</source>
        <translation>現在の色: カスタム色スロットへドラッグ、またはここに色をドロップできます</translation>
    </message>
    <message>
        <location line="+518"/>
        <source>Pick Screen Color</source>
        <translation>画面から色を取得</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Custom colors</source>
        <translation>カスタム色</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Set Custom Color</source>
        <translation>カスタム色を設定</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Set the selected custom color box to the current color (click a box to select it)</source>
        <translation>選択したカスタム色ボックスを現在の色に設定します (ボックスをクリックして選択)</translation>
    </message>
    <message>
        <location line="+126"/>
        <source>Hue:</source>
        <translation>色相:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Sat:</source>
        <translation>彩度:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Val:</source>
        <translation>明度:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>HTML:</source>
        <translation>HTML:</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Red:</source>
        <translation>赤:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Green:</source>
        <translation>緑:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Blue:</source>
        <translation>青:</translation>
    </message>
    <message>
        <location line="-110"/>
        <source>Square</source>
        <translation>スクエア</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Wheel</source>
        <translation>ホイール</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Sliders</source>
        <translation>スライダー</translation>
    </message>
    <message>
        <location line="-17"/>
        <source>Hue</source>
        <translation>色相</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sat</source>
        <translation>彩度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Val</source>
        <translation>明度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Red</source>
        <translation>赤</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Green</source>
        <translation>緑</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Blue</source>
        <translation>青</translation>
    </message>
</context>
<context>
    <name>patchy::ui::TilePreviewWindow</name>
    <message>
        <location filename="../src/ui/tile_preview_window.cpp" line="+153"/>
        <source>Seamless Tile Preview</source>
        <translation>シームレスタイル プレビュー</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Fit</source>
        <translation>全体表示</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Refresh</source>
        <translation>更新</translation>
    </message>
    <message>
        <location line="+57"/>
        <source>No document</source>
        <translation>ドキュメントがありません</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Large document: use Refresh to update</source>
        <translation>大きなドキュメント: 更新ボタンで再描画します</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 x %2, live</source>
        <translation>%1 x %2、ライブ更新</translation>
    </message>
    <message>
        <location line="-55"/>
        <source>Drag to pan. Mouse wheel zooms. Double-click to recenter.</source>
        <translation>ドラッグでパン、マウスホイールでズーム、ダブルクリックで中央に戻します。</translation>
    </message>
    <message>
        <location filename="../src/ui/tile_preview_window.cpp"/>
        <source>Shift Seams to Center</source>
        <translation>シームを中央へ移動</translation>
    </message>
    <message>
        <location filename="../src/ui/tile_preview_window.cpp"/>
        <source>Shift Seams Back</source>
        <translation>シームを元に戻す</translation>
    </message>
    <message>
        <location filename="../src/ui/tile_preview_window.cpp"/>
        <source>Wrap the image by half its size so the seams land in the middle for painting over. Press again to shift them back to the edges.</source>
        <translation>画像を半分ずらして巻き込み、シームを中央に移動させて塗りつぶしやすくします。もう一度押すと端に戻ります。</translation>
    </message>
</context>
<context>
    <!-- tr() contexts are the fully qualified class name; the bare name never
         matched at runtime, so these translations were dead until renamed. -->
    <name>patchy::ui::PatternLibrary</name>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>Untitled Pattern</source>
        <translation>無題のパターン</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>Could not open &quot;%1&quot;.</source>
        <translation>「%1」を開けませんでした。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>Could not read &quot;%1&quot;.</source>
        <translation>「%1」を読み込めませんでした。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>&quot;%1&quot; is too large to import safely.</source>
        <translation>「%1」は安全に読み込めるサイズを超えています。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>Could not import patterns from &quot;%1&quot;. The file is not a supported Photoshop PAT file or is damaged.</source>
        <translation>「%1」からパターンを読み込めませんでした。このファイルは対応する Photoshop PAT ファイルではないか、破損しています。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>Some pattern data was skipped or repaired because it is unsupported or damaged.</source>
        <translation>未対応または破損したデータが含まれているため、一部のパターンデータをスキップまたは修復しました。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>Could not read &quot;%1&quot; as an image.</source>
        <translation>「%1」を画像として読み込めませんでした。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>&quot;%1&quot; is too large to use as a pattern (over 8 million pixels).</source>
        <translation>「%1」は大きすぎてパターンとして使用できません (800 万ピクセル超)。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>&quot;%1&quot; is too large to use as a pattern (over 30,000 pixels wide or tall).</source>
        <translation>「%1」は大きすぎてパターンとして使用できません (幅または高さが 30,000 ピクセル超)。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>Animated image &quot;%1&quot;: imported the first frame only.</source>
        <translation>アニメーション画像「%1」: 最初のフレームのみ読み込みました。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>Imported Patterns</source>
        <translation>読み込んだパターン</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>Pattern %1</source>
        <translation>パターン %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>Skipped pattern &quot;%1&quot; because its pixels could not be decoded.</source>
        <translation>ピクセルをデコードできなかったため、パターン「%1」をスキップしました。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>Pattern &quot;%1&quot; used an id already assigned to different pixels; it was imported with a new id.</source>
        <translation>パターン「%1」の ID は別のピクセルに割り当て済みだったため、新しい ID で読み込みました。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>Could not save pattern &quot;%1&quot;.</source>
        <translation>パターン「%1」を保存できませんでした。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>No patterns could be imported from &quot;%1&quot;.</source>
        <translation>「%1」からパターンを読み込めませんでした。</translation>
    </message>
    <message>
        <location filename="../src/ui/pattern_library.cpp"/>
        <source>%1 Copy</source>
        <translation>%1 のコピー</translation>
    </message>
</context>
<context>
    <name>patchy::ui::StartPanel</name>
    <message>
        <source>Open source photo editing. Free forever, no subscriptions.</source>
        <translation>オープンソースの写真編集。永久無料、サブスクリプション不要。</translation>
    </message>
    <message>
        <source>Version %1</source>
        <translation>バージョン %1</translation>
    </message>
    <message>
        <source>Created by Seth A. Robinson</source>
        <translation>作成: Seth A. Robinson</translation>
    </message>
    <message>
        <source>Code contributions from %1</source>
        <translation>コード貢献者: %1</translation>
    </message>
    <message>
        <source>GitHub: %1</source>
        <translation>GitHub: %1</translation>
    </message>
    <message>
        <source>Seth&apos;s site: %1</source>
        <translation>Seth のサイト: %1</translation>
    </message>
    <message>
        <source>New Document...</source>
        <translation>新規ドキュメント...</translation>
    </message>
    <message>
        <source>Open...</source>
        <translation>開く...</translation>
    </message>
    <message>
        <source>Recent Files</source>
        <translation>最近使用したファイル</translation>
    </message>
    <message>
        <source>You can also drop image files anywhere in the window</source>
        <translation>画像ファイルをウィンドウにドロップして開くこともできます</translation>
    </message>
</context>
<context>
    <name>patchy::ui::StyleBrowserWidget</name>
    <message>
        <location filename="../src/ui/style_browser.cpp"/>
        <source>No Style (remove all effects)</source>
        <translation>スタイルなし(すべての効果を削除)</translation>
    </message>
    <message>
        <location filename="../src/ui/style_browser.cpp"/>
        <source>Remove every effect from the layer</source>
        <translation>レイヤーからすべての効果を削除します</translation>
    </message>
    <message>
        <location filename="../src/ui/style_browser.cpp"/>
        <source>%1 (%2)</source>
        <translation>%1 (%2)</translation>
    </message>
    <message>
        <location filename="../src/ui/style_browser.cpp"/>
        <source>Styles</source>
        <translation>スタイル</translation>
    </message>
    <message>
        <location filename="../src/ui/style_browser.cpp"/>
        <source>Export Styles</source>
        <translation>スタイルの書き出し</translation>
    </message>
    <message>
        <location filename="../src/ui/style_browser.cpp"/>
        <source>Photoshop Styles (*.asl)</source>
        <translation>Photoshop スタイル (*.asl)</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ui/style_browser.cpp"/>
        <source>Exported %n style(s) to &quot;%1&quot;.</source>
        <translation>
            <numerusform>%n 個のスタイルを「%1」に書き出しました。</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ui/style_browser.cpp"/>
        <source>Export Folder to .asl…</source>
        <translation>フォルダーを .asl に書き出し…</translation>
    </message>
    <message>
        <location filename="../src/ui/style_browser.cpp"/>
        <source>Export to .asl…</source>
        <translation>.asl に書き出し…</translation>
    </message>
</context>
<context>
    <name>patchy::ui::StyleLibrary</name>
    <message>
        <location filename="../src/ui/style_library.cpp"/>
        <source>Untitled Style</source>
        <translation>無題のスタイル</translation>
    </message>
    <message>
        <location filename="../src/ui/style_library.cpp"/>
        <source>Could not open &quot;%1&quot;.</source>
        <translation>「%1」を開けませんでした。</translation>
    </message>
    <message>
        <location filename="../src/ui/style_library.cpp"/>
        <source>Could not read &quot;%1&quot;.</source>
        <translation>「%1」を読み込めませんでした。</translation>
    </message>
    <message>
        <location filename="../src/ui/style_library.cpp"/>
        <source>&quot;%1&quot; is too large to import safely.</source>
        <translation>「%1」は大きすぎるため安全に読み込めません。</translation>
    </message>
    <message>
        <location filename="../src/ui/style_library.cpp"/>
        <source>Could not import styles from &quot;%1&quot;. The file is not a supported Photoshop ASL file or is damaged.</source>
        <translation>「%1」からスタイルを読み込めませんでした。サポートされていない Photoshop ASL ファイルか、破損しています。</translation>
    </message>
    <message>
        <location filename="../src/ui/style_library.cpp"/>
        <source>Imported Styles</source>
        <translation>読み込んだスタイル</translation>
    </message>
    <message>
        <location filename="../src/ui/style_library.cpp"/>
        <source>Style %1</source>
        <translation>スタイル %1</translation>
    </message>
    <message>
        <location filename="../src/ui/style_library.cpp"/>
        <source>Style &quot;%1&quot; used an id already assigned to a different style; it was imported with a new id.</source>
        <translation>スタイル「%1」の ID は別のスタイルに割り当て済みだったため、新しい ID で読み込みました。</translation>
    </message>
    <message>
        <location filename="../src/ui/style_library.cpp"/>
        <source>Could not save style &quot;%1&quot;.</source>
        <translation>スタイル「%1」を保存できませんでした。</translation>
    </message>
    <message>
        <location filename="../src/ui/style_library.cpp"/>
        <source>No styles could be imported from &quot;%1&quot;.</source>
        <translation>「%1」から読み込めるスタイルがありませんでした。</translation>
    </message>
    <message>
        <location filename="../src/ui/style_library.cpp"/>
        <source>There are no styles to export.</source>
        <translation>書き出すスタイルがありません。</translation>
    </message>
    <message>
        <location filename="../src/ui/style_library.cpp"/>
        <source>Could not write &quot;%1&quot;.</source>
        <translation>「%1」に書き込めませんでした。</translation>
    </message>
    <message>
        <location filename="../src/ui/style_library.cpp"/>
        <source>%1 Copy</source>
        <translation>%1 のコピー</translation>
    </message>
</context>
<context>
    <name>patchy::ui::GradientLibrary</name>
    <message><source>Untitled Gradient</source><translation>無題のグラデーション</translation></message>
    <message><source>%1 Copy</source><translation>%1 のコピー</translation></message>
    <message><source>Could not read the GRD file.</source><translation>GRD ファイルを読み込めませんでした。</translation></message>
    <message><source>Select at least one gradient to export.</source><translation>書き出すグラデーションを1つ以上選択してください。</translation></message>
    <message><source>Could not write the GRD file.</source><translation>GRD ファイルを書き込めませんでした。</translation></message>
</context>
<context>
    <name>QObject</name>
    <message><source>Liquify</source><translation>ゆがみ</translation></message>
    <message><source>Warp</source><translation>ワープ</translation></message>
    <message><source>Reconstruct</source><translation>再構築</translation></message>
    <message><source>Twirl clockwise; hold Alt or Option to reverse</source><translation>時計回りに渦巻きを適用します。Alt または Option キーを押すと逆方向になります</translation></message>
    <message><source>Pucker</source><translation>収縮</translation></message>
    <message><source>Bloat</source><translation>膨張</translation></message>
    <message><source>Freeze</source><translation>固定</translation></message>
    <message><source>Thaw</source><translation>固定解除</translation></message>
    <message><source>Brush Controls</source><translation>ブラシコントロール</translation></message>
    <message><source>Pressure:</source><translation>筆圧:</translation></message>
    <message><source>Density:</source><translation>密度:</translation></message>
    <message><source>Show Freeze Mask</source><translation>固定マスクを表示</translation></message>
    <message><source>Restore All</source><translation>すべてを復元</translation></message>
    <message><source>Liquify edits pixels directly. Rasterize a Smart Object before using it.</source><translation>ゆがみはピクセルを直接編集します。使用する前にスマートオブジェクトをラスタライズしてください。</translation></message>
    <message><source>Gradient Manager</source><translation>グラデーションマネージャー</translation></message>
    <message><source>New from Current</source><translation>現在の設定から新規作成</translation></message>
    <message><source>Import .grd...</source><translation>.grd を読み込み...</translation></message>
    <message><source>Restore Default Gradients</source><translation>初期グラデーションを復元</translation></message>
    <message><source>Use Gradient</source><translation>グラデーションを使用</translation></message>
    <message><source>New Gradient</source><translation>新規グラデーション</translation></message>
    <message><source>Import Photoshop Gradients</source><translation>Photoshop グラデーションを読み込み</translation></message>
    <message><source>Export Photoshop Gradients</source><translation>Photoshop グラデーションを書き出し</translation></message>
    <message><source>Photoshop Gradients (*.grd)</source><translation>Photoshop グラデーション (*.grd)</translation></message>
    <message><source>Import Gradients</source><translation>グラデーションを読み込み</translation></message>
    <message><source>Export Gradients</source><translation>グラデーションを書き出し</translation></message>
    <message><source>Imported gradients with warnings.</source><translation>警告付きでグラデーションを読み込みました。</translation></message>
    <message><source>Manage Gradients...</source><translation>グラデーションを管理...</translation></message>
    <message><source>Preset...</source><translation>プリセット...</translation></message>
    <message><source>Gradient Type</source><translation>グラデーションタイプ</translation></message>
    <message><source>Solid</source><translation>ソリッド</translation></message>
    <message><source>Noise</source><translation>ノイズ</translation></message>
    <message><source>Roughness</source><translation>粗さ</translation></message>
    <message><source>Color Model</source><translation>カラーモデル</translation></message>
    <message><source>Channel %1 Range</source><translation>チャンネル %1 の範囲</translation></message>
    <message><source>Add Transparency</source><translation>透明部分を追加</translation></message>
    <message><source>Restrict Colors</source><translation>カラーを制限</translation></message>
    <message><source>Dither</source><translation>ディザ</translation></message>
    <message><source>Align with Layer</source><translation>レイヤーに整列</translation></message>
    <message><source>Classic</source><translation>クラシック</translation></message>
    <message><source>Perceptual</source><translation>知覚的</translation></message>
    <message><source>Horizontal Offset</source><translation>水平オフセット</translation></message>
    <message><source>Vertical Offset</source><translation>垂直オフセット</translation></message>
    <message><source>Reset Alignment</source><translation>整列をリセット</translation></message>
    <message><source>Essentials</source><translation>基本</translation></message>
    <message><source>Photo Toning</source><translation>写真の色調</translation></message>
    <message><source>Light &amp; Atmosphere</source><translation>光と空気感</translation></message>
    <message><source>Illustration</source><translation>イラスト</translation></message>
    <message><source>Foreground to Background</source><translation>描画色から背景色へ</translation></message>
    <message><source>Foreground to Transparent</source><translation>描画色から透明へ</translation></message>
    <message><source>Black to White</source><translation>黒から白へ</translation></message>
    <message><source>White to Transparent</source><translation>白から透明へ</translation></message>
    <message><source>Neutral Shine</source><translation>ニュートラルシャイン</translation></message>
    <message><source>Teal Shadows, Warm Highlights</source><translation>ティールシャドウ、暖色ハイライト</translation></message>
    <message><source>Blue Shadows, Gold Highlights</source><translation>ブルーシャドウ、ゴールドハイライト</translation></message>
    <message><source>Sepia Wash</source><translation>セピアウォッシュ</translation></message>
    <message><source>Cyanotype</source><translation>サイアノタイプ</translation></message>
    <message><source>Faded Film</source><translation>フェードフィルム</translation></message>
    <message><source>Golden Hour</source><translation>ゴールデンアワー</translation></message>
    <message><source>Sunset Sky</source><translation>夕焼け空</translation></message>
    <message><source>Dawn Mist</source><translation>夜明けの霧</translation></message>
    <message><source>Night Sky</source><translation>夜空</translation></message>
    <message><source>Firelight</source><translation>炎の光</translation></message>
    <message><source>Comic Pop</source><translation>コミックポップ</translation></message>
    <message><source>Neon Spectrum</source><translation>ネオンスペクトラム</translation></message>
    <message><source>Metal Shine</source><translation>メタルシャイン</translation></message>
    <message><source>Cel Shade</source><translation>セルシェード</translation></message>
    <message><source>Jewel Tone</source><translation>ジュエルトーン</translation></message>
    <message><source>This Smart Object is preview-locked. Its Smart Filters are preserved unchanged.</source><translation>このスマートオブジェクトはプレビュー専用です。スマートフィルターは変更せずに保持されます。</translation></message>
    <message><source>This Smart Filter stack contains unsupported Photoshop data. Patchy preserves it unchanged, so the controls are disabled.</source><translation>このスマートフィルタースタックには未対応の Photoshop データが含まれています。Patchy はデータを変更せずに保持するため、コントロールは無効です。</translation></message>
    <message><source>Smart Filters visible. Click to hide.</source><translation>スマートフィルターは表示中です。クリックで非表示にします。</translation></message>
    <message><source>Smart Filters hidden. Click to show.</source><translation>スマートフィルターは非表示です。クリックで表示します。</translation></message>
    <message><source>Smart Filters</source><translation>スマートフィルター</translation></message>
    <message><source>Shared Smart Filter mask</source><translation>共有スマートフィルターマスク</translation></message>
    <message><source>Smart Filter visible. Click to hide.</source><translation>スマートフィルターは表示中です。クリックで非表示にします。</translation></message>
    <message><source>Smart Filter hidden. Click to show.</source><translation>スマートフィルターは非表示です。クリックで表示します。</translation></message>
    <message><source> (%1 px)</source><translation> (%1 px)</translation></message>
    <message><source> (Radius %1 px, Threshold %2)</source><translation> (半径 %1 px、しきい値 %2)</translation></message>
    <message><source> (Amount %1%, Radius %2 px, Threshold %3)</source><translation> (量 %1%、半径 %2 px、しきい値 %3)</translation></message>
    <message><source> (Angle %1 degrees, Distance %2 px)</source><translation> (角度 %1 度、距離 %2 px)</translation></message>
    <message><source>Unsupported Smart Filter</source><translation>未対応のスマートフィルター</translation></message>
    <message><source>Edit Smart Filter</source><translation>スマートフィルターを編集</translation></message>
    <message><source>Smart Filter actions</source><translation>スマートフィルターの操作</translation></message>
    <message><source>Shared Smart Filter mask. Click to edit it, Ctrl-click to load it as a selection, Alt-click to view it, or Shift-click to disable it.</source><translation>共有スマートフィルターマスク。クリックで編集、Ctrl+クリックで選択範囲として読み込み、Alt+クリックで表示、Shift+クリックで無効にします。</translation></message>
    <message><source>Blending</source><translation>描画オプション</translation></message>
    <message><source>Mode:</source><translation>描画モード:</translation></message>
    <message><source>Opacity:</source><translation>不透明度:</translation></message>
    <message><source>%</source><translation>%</translation></message>
    <message><source> degrees</source><translation> 度</translation></message>
    <message><source>Duplicate Smart Filter</source><translation>スマートフィルターを複製</translation></message>
    <message><source>Move Smart Filter up</source><translation>スマートフィルターを上へ移動</translation></message>
    <message><source>Move Smart Filter down</source><translation>スマートフィルターを下へ移動</translation></message>
    <message><source>Delete Smart Filter</source><translation>スマートフィルターを削除</translation></message>
    <message><source> px</source><translation> px</translation></message>
    <message><source>Plastic Wrap</source><translation>ラップ</translation></message>
    <message><source>Artistic</source><translation>アーティスティック</translation></message>
    <message><source>Highlight Strength</source><translation>ハイライトの強さ</translation></message>
    <message><source> (Highlight %1, Detail %2, Smoothness %3)</source><translation> (ハイライト %1、ディテール %2、滑らかさ %3)</translation></message>
    <message><source>Mosaic</source><translation>モザイク</translation></message>
    <message><source> (Cell Size %1 px)</source><translation> (セルサイズ %1 px)</translation></message>
    <message><source> (Angle %1 degrees, Height %2 px, Amount %3%)</source><translation> (角度 %1 度、高さ %2 px、量 %3%)</translation></message>
    <message><source>Shape Appearance</source><translation>シェイプの外観</translation></message>
    <message><source>Geometry</source><translation>ジオメトリ</translation></message>
    <message><source>Start X:</source><translation>開始 X:</translation></message>
    <message><source>Start Y:</source><translation>開始 Y:</translation></message>
    <message><source>End X:</source><translation>終了 X:</translation></message>
    <message><source>End Y:</source><translation>終了 Y:</translation></message>
    <message><source>Weight:</source><translation>太さ:</translation></message>
    <message><source>X:</source><translation>X:</translation></message>
    <message><source>Y:</source><translation>Y:</translation></message>
    <message><source>Width:</source><translation>幅:</translation></message>
    <message><source>Height:</source><translation>高さ:</translation></message>
    <message><source>Top left radius:</source><translation>左上の角丸半径:</translation></message>
    <message><source>Top right radius:</source><translation>右上の角丸半径:</translation></message>
    <message><source>Bottom right radius:</source><translation>右下の角丸半径:</translation></message>
    <message><source>Bottom left radius:</source><translation>左下の角丸半径:</translation></message>
    <message><source>Type:</source><translation>種類:</translation></message>
    <message><source>No Fill</source><translation>塗りなし</translation></message>
    <message><source>Solid Color</source><translation>ベタ塗り</translation></message>
    <message><source>Color...</source><translation>カラー...</translation></message>
    <message><source>Color:</source><translation>カラー:</translation></message>
    <message><source>Gradient:</source><translation>グラデーション:</translation></message>
    <message><source>Angle:</source><translation>角度:</translation></message>
    <message><source>Pattern:</source><translation>パターン:</translation></message>
    <message><source>Embedded pattern</source><translation>埋め込みパターン</translation></message>
    <message><source>Offset X:</source><translation>オフセット X:</translation></message>
    <message><source>Offset Y:</source><translation>オフセット Y:</translation></message>
    <message><source>Align with layer</source><translation>レイヤーに整列</translation></message>
    <message><source>Anchor the tile grid to the layer&apos;s position; unchecked anchors it to the document origin</source><translation>タイルの基準をレイヤーの位置に合わせます。オフの場合はドキュメントの原点に合わせます</translation></message>
    <message><source>Paint:</source><translation>ペイント:</translation></message>
    <message><source>Stroke the shape outline</source><translation>シェイプの輪郭に線を描く</translation></message>
    <message><source>Align:</source><translation>位置:</translation></message>
    <message><source>Caps:</source><translation>先端:</translation></message>
    <message><source>Butt</source><translation>バット</translation></message>
    <message><source>Corners:</source><translation>角:</translation></message>
    <message><source>Miter</source><translation>マイター</translation></message>
    <message><source>Dashes:</source><translation>破線:</translation></message>
    <message><source>Dashed</source><translation>破線</translation></message>
    <message><source>Dotted</source><translation>点線</translation></message>
    <message><source>Shape Fill Color</source><translation>シェイプの塗りの色</translation></message>
    <message><source>Shape Stroke Color</source><translation>シェイプの線の色</translation></message>
    <message><source>Square</source><comment>stroke cap</comment><translation>スクエア</translation></message>
    <message><source>Arrows</source><translation>矢印</translation></message>
    <message><source>Symbols</source><translation>シンボル</translation></message>
    <message><source>Arrow Right</source><translation>右矢印</translation></message>
    <message><source>Arrow Left</source><translation>左矢印</translation></message>
    <message><source>Arrow Up</source><translation>上矢印</translation></message>
    <message><source>Arrow Down</source><translation>下矢印</translation></message>
    <message><source>Arrow Double</source><translation>両方向矢印</translation></message>
    <message><source>Chevron</source><translation>シェブロン</translation></message>
    <message><source>Arrow Curved</source><translation>曲線矢印</translation></message>
    <message><source>Check Mark</source><translation>チェックマーク</translation></message>
    <message><source>Cross</source><translation>クロス</translation></message>
    <message><source>Plus</source><translation>プラス</translation></message>
    <message><source>Triangle</source><translation>三角形</translation></message>
    <message><source>Speech Bubble</source><translation>吹き出し</translation></message>
    <message><source>Lightning Bolt</source><translation>稲妻</translation></message>
    <message><source>Vector mask. Click to edit its path with the pen and path tools, Ctrl-click to load it as a selection, Alt-click to view it, Shift-click to disable it.</source><translation>ベクトルマスク。クリックでペン/パスツールでのパス編集、Ctrl クリックで選択範囲として読み込み、Alt クリックで表示、Shift クリックで無効化します。</translation></message>

</context>
<context>
    <name>FilterGalleryControls</name>
    <message>
        <location filename="../src/ui/filter_gallery_controls.cpp" line="60"/>
        <source>Angle</source>
        <translation>角度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Drag around the dial to set the angle. Use the arrow keys for precise changes; hold Shift for larger steps.</source>
        <translation>ダイヤルをドラッグして角度を設定します。矢印キーで細かく調整し、Shift キーを押しながら操作すると大きく変更できます。</translation>
    </message>
    <message>
        <location line="+283"/>
        <source>Waveform</source>
        <translation>波形</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Drag horizontally to change phase and vertically to change amplitude. Use the mouse wheel to change wavelength.</source>
        <translation>横にドラッグすると位相、縦にドラッグすると振幅が変わります。マウスホイールで波長を変更できます。</translation>
    </message>
</context>
<context>
    <name>patchy::ui::ScriptEngineHost</name>
    <message><source>Could not read script file: %1</source><translation>スクリプトファイルを読み込めませんでした: %1</translation></message>
    <message><source>A script is already running: %1</source><translation>スクリプトは既に実行中です: %1</translation></message>
    <message><source>Untitled Script</source><translation>無題のスクリプト</translation></message>
    <message><source>Script stopped.</source><translation>スクリプトを停止しました。</translation></message>
    <message><source>Script stopped: it exceeded the time limit.</source><translation>スクリプトを停止しました: 制限時間を超えました。</translation></message>
    <message><source>Script stopped: a callback exceeded the time limit.</source><translation>スクリプトを停止しました: コールバックが制限時間を超えました。</translation></message>
    <message><source>setTimeout/setInterval needs a function.</source><translation>setTimeout/setInterval には関数が必要です。</translation></message>
    <message><source>include: could not read %1</source><translation>include: %1 を読み込めませんでした</translation></message>
    <message><source>Background</source><translation>背景</translation></message>
    <message><source>Untitled</source><translation>無題</translation></message>
    <message><source>Script: %1</source><translation>スクリプト: %1</translation></message>
    <message><source>Script</source><translation>スクリプト</translation></message>
    <message><source>[alert] %1</source><translation>[alert] %1</translation></message>
    <message><source>The document is no longer open.</source><translation>ドキュメントは既に閉じられています。</translation></message>
    <message><source>Unknown filter id: %1</source><translation>不明なフィルター ID: %1</translation></message>
    <message><source>Filter %1 has no parameter named %2</source><translation>フィルター %1 に %2 という名前のパラメーターはありません</translation></message>
    <message><source>Filter %1 rejected those parameters.</source><translation>フィルター %1 はそのパラメーターを受け付けませんでした。</translation></message>
    <message><source>The layer no longer exists.</source><translation>レイヤーは既に存在しません。</translation></message>
    <message><source>applyFilter needs a pixel layer.</source><translation>applyFilter にはピクセルレイヤーが必要です。</translation></message>
    <message><source>Invalid color: %1 (use &quot;#rrggbb&quot; or a named color)</source><translation>無効な色です: %1 (&quot;#rrggbb&quot; または色名を使用してください)</translation></message>
    <message><source>This layer is not a text layer.</source><translation>このレイヤーはテキストレイヤーではありません。</translation></message>
    <message><source>Could not edit the text layer.</source><translation>テキストレイヤーを編集できませんでした。</translation></message>
    <message><source>fill needs a pixel layer, not a group.</source><translation>fill にはグループではなくピクセルレイヤーが必要です。</translation></message>
    <message><source>fill supports 8-bit RGBA layers only.</source><translation>fill は 8 ビット RGBA レイヤーのみ対応です。</translation></message>
    <message><source>getPixels supports 8-bit RGBA layers only.</source><translation>getPixels は 8 ビット RGBA レイヤーのみ対応です。</translation></message>
    <message><source>setPixels needs a {width, height, data} object.</source><translation>setPixels には {width, height, data} オブジェクトが必要です。</translation></message>
    <message><source>setPixels: data must hold width * height * 4 RGBA bytes.</source><translation>setPixels: data は width * height * 4 バイトの RGBA データが必要です。</translation></message>
    <message><source>setPixels needs a pixel layer, not a group.</source><translation>setPixels にはグループではなくピクセルレイヤーが必要です。</translation></message>
    <message><source>activeLayer needs a layer of this document.</source><translation>activeLayer にはこのドキュメントのレイヤーが必要です。</translation></message>
    <message><source>Layer</source><translation>レイヤー</translation></message>
    <message><source>Could not create the text layer.</source><translation>テキストレイヤーを作成できませんでした。</translation></message>
    <message><source>resizeImage needs a size between 1 and 30000.</source><translation>resizeImage のサイズは 1 から 30000 の範囲で指定してください。</translation></message>
    <message><source>resizeCanvas needs a size between 1 and 30000.</source><translation>resizeCanvas のサイズは 1 から 30000 の範囲で指定してください。</translation></message>
    <message><source>crop needs a positive size.</source><translation>crop には正のサイズが必要です。</translation></message>
    <message><source>crop rectangle is outside the canvas.</source><translation>crop の矩形がキャンバスの外にあります。</translation></message>
    <message><source>newDocument needs a size between 1 and 30000.</source><translation>newDocument のサイズは 1 から 30000 の範囲で指定してください。</translation></message>
    <message><source>Could not open %1</source><translation>%1 を開けませんでした</translation></message>
    <message><source>Could not read %1</source><translation>%1 を読み込めませんでした</translation></message>
    <message><source>Could not write %1</source><translation>%1 に書き込めませんでした</translation></message>
    <message><source>selectRect needs a positive size.</source><translation>selectRect には正のサイズが必要です。</translation></message>
    <message><source>selectEllipse needs a positive size.</source><translation>selectEllipse には正のサイズが必要です。</translation></message>
    <message><source>Script Window</source><translation>スクリプトウィンドウ</translation></message>
</context>
<context>
    <name>patchy::ui::ScriptEditorDialog</name>
    <message><source>Script Editor</source><translation>スクリプトエディター</translation></message>
    <message><source>Run</source><translation>実行</translation></message>
    <message><source>Stop</source><translation>停止</translation></message>
    <message><source>New</source><translation>新規</translation></message>
    <message><source>Save</source><translation>保存</translation></message>
    <message><source>Save As...</source><translation>名前を付けて保存...</translation></message>
    <message><source>Reload</source><translation>再読み込み</translation></message>
    <message><source>untitled.js</source><translation>untitled.js</translation></message>
    <message><source>Bundled</source><translation>同梱スクリプト</translation></message>
    <message><source>My Scripts</source><translation>マイスクリプト</translation></message>
    <message><source>Discard unsaved changes to %1?</source><translation>%1 の未保存の変更を破棄しますか?</translation></message>
    <message><source>Could not read %1</source><translation>%1 を読み込めませんでした</translation></message>
    <message><source>Could not write %1</source><translation>%1 に書き込めませんでした</translation></message>
    <message><source>Save Script</source><translation>スクリプトを保存</translation></message>
    <message><source>JavaScript files (*.js)</source><translation>JavaScript ファイル (*.js)</translation></message>
</context>
</TS>
