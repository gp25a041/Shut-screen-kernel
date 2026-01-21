# Shut-screen-kernel
windows10-22H2 version

Windows kernel-level display affinity enforcement tool. Prevents screen capture by calling win32kfull internal functions.
（Windowsカーネルレベルのディスプレイアフィニティ強​​制ツール。win32kfull内部関数を呼び出すことで、画面キャプチャを防止します）

https://learn.microsoft.com/ja-jp/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity

<img width="382" height="162" alt="image" src="https://github.com/user-attachments/assets/5e5365ac-c725-4946-a83f-5e0819f37150" />
<img width="879" height="329" alt="image" src="https://github.com/user-attachments/assets/5475e07b-dd7f-455b-a364-4ea46fa335b3" />

通常、SetWindowDisplayAffinity はユーザーモードから呼び出せますが、自プロセス以外のウィンドウに対して自由に適用するには制限があります。 本プロジェクト Shut-screen-kernel は、カーネルレベルで win32kfull.sys の内部処理に直接介入することで、より強力かつ強制的な画面キャプチャ保護を実現することを目的としています。
win32kfull.sys のidaでの解析
調査の結果、Windows内部では SetDisplayAffinity が最終的に ChangeWindowTreeProtection という内部関数を呼び出していることが分かりました。
<img width="688" height="644" alt="image" src="https://github.com/user-attachments/assets/644ccfac-0b16-48b2-80f5-78d9076a3129" />

そこで私はChangeWindowTreeProtectionにシグネチャスキャンをし、動的に特定した ChangeWindowTreeProtection に対して、対象となる HWND（ウィンドウハンドル）を、win32kbase.sys のエクスポート関数である ValidateHwnd を用いて、カーネル内部のウィンドウ構造体（tagWND）へ変換します。

フラグの強制書き換え: 通常の SetWindowDisplayAffinity は内部で様々なバリデーションを行いますが、本ドライバは ChangeWindowTreeProtection(pWnd, 0x11) を直接呼び出すことで、OSの描画管理層（Composition Engine）に対して強制的に「キャプチャ対象外」の属性を付与します。

<img width="716" height="109" alt="image" src="https://github.com/user-attachments/assets/2090c0ff-31e8-440d-8f62-0e527a3cb0a9" />
-----------------------------------------------------------------------------------------------------------------------------------
免責事項
本プロジェクトは教育および研究目的のみを意図しています。本ソフトウェアの使用によって生じた損害について、作者は一切の責任を負いません。
