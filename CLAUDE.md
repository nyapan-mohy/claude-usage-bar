# CLAUDE.md - Claude Usage Bar (Windows)

## Project Overview
macOS専用のClaude Usage Barメニューバーアプリ（Swift/SwiftUI）を解析し、Win32 API + C++でWindows版として再実装するプロジェクト。

## Goals
- 既存のmacOS版（Swift/SwiftUI）のコードを解析・理解する
- Win32 API + C++でWindowsシステムトレイアプリとして再実装する
- Claude APIのOAuth認証・使用量取得機能をWindows環境で動作させる
- メニューバー（システムトレイ）アイコン + ポップアップUIを実装する

## Tech Stack
- C++ (C++17以上)
- Win32 API（システムトレイ、ウィンドウ管理）
- WinHTTP または libcurl（HTTP通信）
- nlohmann/json（JSON解析）
- CMake（ビルドシステム）

## Architecture Decisions
- macOS版の`macos/Sources/ClaudeUsageBar/`配下のSwiftコードを参考に、同等の機能をC++で実装
- UIはWin32 APIネイティブ（WPFやQtは使わない）
- システムトレイアイコンで常駐、クリックでポップアップ表示
- OAuth認証フローはブラウザ連携（ShellExecuteでブラウザ起動）

## Key Features（macOS版から移植対象）
- OAuth認証（ブラウザ経由サインイン）
- 5時間/7日間の使用量表示（プログレスバー）
- モデル別使用量（Opus/Sonnet）
- リセットタイマー
- Extra usage（USD表示）
- 使用履歴チャート
- ポーリング間隔設定（5m/15m/30m/1h）
- 通知（使用量閾値）

## macOS版コード構造（参考）
```
macos/Sources/ClaudeUsageBar/
├── ClaudeUsageBarApp.swift      # アプリエントリポイント
├── UsageService.swift           # OAuth、ポーリング、API呼び出し
├── UsageModel.swift             # APIレスポンス型
├── UsageHistoryModel.swift      # 履歴データ型
├── UsageHistoryService.swift    # 永続化、ダウンサンプリング
├── UsageChartView.swift         # チャートView
├── PopoverView.swift            # メインポップオーバーUI
├── SettingsView.swift           # 設定画面
├── NotificationService.swift    # 使用量閾値通知
├── MenuBarIconRenderer.swift    # メニューバーアイコン描画
├── PollingOptionFormatter.swift # ポーリング間隔表示
├── AppUpdater.swift             # Sparkle更新
└── StoredCredentials.swift      # 認証情報保存
```

## 開発プラクティス
- t-wadaさんのコーディングスタイルに準拠
- ハードコーディングしないこと
- 関数ベース開発（classは使わない）
  - ライブラリの都合上どうしてもclassで開発する必要がある場合、明確に分離すること
- TDDで開発

## ディレクトリ構成（Windows版）
```
claude-usage-bar/
├── macos/                    # 元のmacOS版コード（参照用）
├── windows/                  # Windows版実装
│   ├── src/                  # ソースコード
│   │   ├── main.cpp          # エントリポイント（WinMain）
│   │   ├── tray.cpp/.h       # システムトレイ管理
│   │   ├── popup.cpp/.h      # ポップアップウィンドウ
│   │   ├── usage_service.cpp/.h  # API通信・ポーリング
│   │   ├── usage_model.cpp/.h    # データモデル
│   │   ├── oauth.cpp/.h      # OAuth認証フロー
│   │   ├── history.cpp/.h    # 使用履歴管理
│   │   ├── notification.cpp/.h   # 通知
│   │   ├── settings.cpp/.h   # 設定管理
│   │   └── config.h          # 定数・設定
│   ├── resources/            # アイコン、リソースファイル
│   │   ├── app.rc            # リソーススクリプト
│   │   └── icons/            # アイコン群
│   ├── tests/                # テスト
│   │   ├── test_usage_model.cpp
│   │   ├── test_usage_service.cpp
│   │   └── test_history.cpp
│   └── CMakeLists.txt        # ビルド設定
├── scripts/                  # 共有スクリプト
├── CLAUDE.md
└── claude_message.md
```

## Development Commands
```bash
# ビルド（CMake）
cd windows
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

# テスト
cd windows/build
ctest --output-on-failure

# クリーン
cmake --build build --target clean
```

## C++コーディング規約
- C++17標準準拠
- ヘッダガードは `#pragma once`
- スマートポインタ使用（生ポインタ禁止）
- エラー処理は戻り値（`std::optional`/`std::expected`）を優先、例外は最小限
- 文字列は `std::wstring`（Win32 API互換）を基本とする
- namespace で機能を分離

## テスト
- Google Test（gtest）を使用
- TDDで開発：まずテストを書いてから実装
