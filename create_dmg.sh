#!/bin/bash
set -e

# Configuration
VERSION="1.5.1"
PROJECT_PATH="$(pwd)"
XCODE_PROJ="$PROJECT_PATH/TransFrame.xcodeproj"
DMG_TEMP_DIR="$PROJECT_PATH/build_artifacts/dmg_temp"
DMG_NAME="TransFrame-v$VERSION.dmg"
DMG_PATH="$PROJECT_PATH/$DMG_NAME"

echo "🚀 开始制作 DMG 安装包..."

# 1. 清理旧的编译和打包产物
rm -rf "$PROJECT_PATH/build_artifacts/dmg"
rm -rf "$DMG_TEMP_DIR"
rm -f "$DMG_PATH"

# 2. 编译 Release 版本 (默认 arm64，可传入参数: ./create_dmg.sh x86_64)
TARGET_ARCH="${1:-arm64}"
echo "🏗️ 正在编译 Release 版本 ($TARGET_ARCH)..."
xcodebuild -project "$XCODE_PROJ" -scheme TransFrame -configuration Release -arch "$TARGET_ARCH" -derivedDataPath "$PROJECT_PATH/build_artifacts/dmg" -quiet

# 3. 创建临时文件夹结构
echo "📁 准备 DMG 目录结构..."
mkdir -p "$DMG_TEMP_DIR"

# 复制 .app 到临时目录
APP_PATH="$PROJECT_PATH/build_artifacts/dmg/Build/Products/Release/TransFrame.app"
if [ ! -d "$APP_PATH" ]; then
    APP_PATH="$PROJECT_PATH/build/Products/Release/TransFrame.app"
fi
if [ ! -d "$APP_PATH" ]; then
    APP_PATH="$PROJECT_PATH/Build/Products/Release/TransFrame.app"
fi

if [ ! -d "$APP_PATH" ]; then
    # 模糊查找
    APP_PATH=$(find "$PROJECT_PATH/build" "$PROJECT_PATH/Build" "$PROJECT_PATH/build_artifacts" -name "TransFrame.app" -type d | head -n 1)
fi

if [ -z "$APP_PATH" ] || [ ! -d "$APP_PATH" ]; then
    echo "❌ 错误: 未找到编译好的 TransFrame.app"
    exit 1
fi

echo "Found app at: $APP_PATH"
cp -R "$APP_PATH" "$DMG_TEMP_DIR/"

# 瘦身 Sparkle.framework (按单架构移除冗余切片并 strip)
SPARKLE_DIR="$DMG_TEMP_DIR/TransFrame.app/Contents/Frameworks/Sparkle.framework"
if [ -d "$SPARKLE_DIR" ]; then
    if [ "$TARGET_ARCH" = "arm64" ] || [ "$TARGET_ARCH" = "x86_64" ]; then
        echo "⚡ 正在精简 Sparkle.framework 架构 ($TARGET_ARCH)..."
        for b in "Versions/B/Sparkle" "Versions/B/Autoupdate" "Versions/B/Updater.app/Contents/MacOS/Updater" "Versions/B/XPCServices/Downloader.xpc/Contents/MacOS/Downloader" "Versions/B/XPCServices/Installer.xpc/Contents/MacOS/Installer"; do
            if [ -f "$SPARKLE_DIR/$b" ]; then
                lipo -thin "$TARGET_ARCH" "$SPARKLE_DIR/$b" -output "$SPARKLE_DIR/$b.thin" 2>/dev/null && mv "$SPARKLE_DIR/$b.thin" "$SPARKLE_DIR/$b" || true
                strip -x "$SPARKLE_DIR/$b" 2>/dev/null || true
            fi
        done
    fi
fi

# 优先使用本机钥匙串中的 Developer ID Application 证书签名（与 GitHub Release 保持一致）
SIGN_IDENTITY=$(security find-identity -v -p codesigning | grep -m1 "Developer ID Application" | awk -F '"' '{print $2}' || true)
if [ -n "$SIGN_IDENTITY" ]; then
    echo "🔐 使用 Developer ID 签名: $SIGN_IDENTITY"
    codesign --force --deep --options runtime --timestamp=none --sign "$SIGN_IDENTITY" "$DMG_TEMP_DIR/TransFrame.app"
else
    echo "⚠️ 未找到 Developer ID 证书，回退为 Ad-hoc 签名"
    codesign --force --deep --sign - "$DMG_TEMP_DIR/TransFrame.app"
fi

# 创建 Applications 软链接
ln -s /Applications "$DMG_TEMP_DIR/Applications"

# 4. 使用 hdiutil 生成 DMG
echo "📦 正在生成 DMG 镜像..."
# 创建临时可写 DMG
TEMP_DMG="$PROJECT_PATH/build_artifacts/temp.dmg"
rm -f "$TEMP_DMG"

hdiutil create -srcfolder "$DMG_TEMP_DIR" -volname "TransFrame" -fs HFS+ -fsargs "-c c=64,a=16,e=16" -format UDRW -size 300m "$TEMP_DMG"

# 挂载 DMG 调整布局 (可选，这里使用简单的默认挂载与静默转换即可，
# 若需要高级背景图和位置，通常需要 AppleScript，但由于 CLI 环境，我们提供一个标准且高兼容性的 DMG)
# 转换成压缩的、只读的 DMG
echo "💾 正在压缩 DMG..."
hdiutil convert "$TEMP_DMG" -format UDZO -imagekey zlib-level=9 -o "$DMG_PATH"

# 清理临时文件
rm -f "$TEMP_DMG"
rm -rf "$DMG_TEMP_DIR"

echo "✅ DMG 制作完成: $DMG_PATH"
