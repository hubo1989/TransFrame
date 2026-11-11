// windows/UI/SettingsShell/SettingsUiKit.hpp
// FR-35 六页签共用控件工厂（纯代码构建 WinUI 控件，无 XAML 编译）。
//
// 视觉基线 = settings-spec-mac-parity.md §0：右侧 detail 为 ScrollView +
// VStack(spacing:20)，水平 padding 24 / 顶 24 / 底 36；分组卡片
// .macosGroupedCard() → 对应主题色 Card 边框（圆角 8 + 卡片填充色）。
// 主题资源（CardBackgroundFillColorDefaultBrush 等）由 shell 合并的
// XamlControlsResources 提供；缺失时回退到半透明灰，保证无资源也能渲染。
#ifndef TF_UI_SETTINGS_UI_KIT_HPP
#define TF_UI_SETTINGS_UI_KIT_HPP

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>

#include "tf/Localization.hpp"

#include <string>

namespace tf::ui::kit {

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

/// FR-37 localized text: Localization tables -> zh fallback, as hstring.
/// Pages use Lx("key", L"中文默认") everywhere they used string literals.
inline winrt::hstring Lx(const char* key, const wchar_t* fallback) {
    return winrt::to_hstring(
        core::loc::Localization::instance().t(key, core::loc::wideToUtf8(fallback).c_str()));
}

inline Controls::TextBlock MakeText(const std::wstring& text, bool bold = false,
                                    double size = 14.0) {
    Controls::TextBlock t;
    t.Text(text);
    if (bold) t.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    t.FontSize(size);
    t.TextWrapping(TextWrapping::Wrap);
    return t;
}

inline std::wstring ToWide(const std::string& utf8) {
    winrt::hstring h = winrt::to_hstring(utf8);
    return std::wstring(h.c_str());
}

inline std::string ToUtf8(winrt::hstring const& h) { return winrt::to_string(h); }

/// 主题资源画刷；查不到时回退（try/catch —— LoadResourceKey 会抛）。
inline Media::Brush ThemeBrush(const wchar_t* key, const Windows::UI::Color& fallback) {
    try {
        auto res = Application::Current().Resources().Lookup(winrt::box_value(key));
        if (auto brush = res.try_as<Media::Brush>()) return brush;
    } catch (...) {
    }
    Media::SolidColorBrush b;
    b.Color(fallback);
    return b;
}

/// 分组卡片：圆角 Border + 内部 StackPanel(Spacing 12, Padding 16)。
struct Card {
    Controls::Border border;
    Controls::StackPanel panel;
};
inline Card MakeCard() {
    Card c;
    c.border.CornerRadius({8, 8, 8, 8});
    c.border.Padding({16, 16, 16, 16});
    c.border.BorderThickness({1, 1, 1, 1});
    c.border.Background(ThemeBrush(L"CardBackgroundFillColorDefaultBrush",
                                    Windows::UI::Color{0xFF, 0x40, 0x40, 0x40}));
    c.border.BorderBrush(ThemeBrush(L"CardStrokeColorDefaultBrush",
                                     Windows::UI::Color{0xFF, 0x60, 0x60, 0x60}));
    c.panel.Spacing(12.0);
    c.border.Child(c.panel);
    return c;
}

/// 页面骨架：ScrollViewer(24,24,24,36) > StackPanel(Spacing 20, MaxWidth 720)。
/// 返回 StackPanel —— 页面把卡片依次 Append 进去。
/// Stretch + 禁用水平滚动：内容按视口宽度换行/收缩，绝不会横向溢出被裁
///（此前 Left 对齐 + 可滚动量测会按最宽子元素布局，窄窗口下右缘截断）。
inline Controls::StackPanel MakePageRoot(Controls::UserControl const& page) {
    Controls::StackPanel stack;
    stack.Spacing(20.0);
    stack.MaxWidth(720.0);
    stack.HorizontalAlignment(HorizontalAlignment::Stretch);
    Controls::ScrollViewer scroller;
    scroller.Padding({24, 24, 24, 36});
    scroller.VerticalScrollBarVisibility(Controls::ScrollBarVisibility::Auto);
    scroller.HorizontalScrollBarVisibility(Controls::ScrollBarVisibility::Disabled);
    scroller.HorizontalScrollMode(Controls::ScrollMode::Disabled);
    scroller.Content(stack);
    page.Content(scroller);
    return stack;
}

/// 小节标题（卡片区隔）。
inline Controls::TextBlock MakeSectionHeader(const wchar_t* text) {
    Controls::TextBlock t;
    t.Text(text);
    t.FontSize(16.0);
    t.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    t.Margin({0, 4, 0, 0});
    return t;
}

/// 行标题 + 说明的两行文本（卡片内一行控件的标签部分）。
inline Controls::StackPanel MakeLabelBlock(const wchar_t* title, const wchar_t* hint = nullptr) {
    Controls::StackPanel s;
    s.Spacing(2.0);
    s.Children().Append(MakeText(title, true));
    if (hint) {
        Controls::TextBlock h = MakeText(hint, false, 12.0);
        h.Opacity(0.7);
        s.Children().Append(h);
    }
    return s;
}

/// 四态健康点（#RRGGBB → 10px 圆点）。
inline Controls::Border MakeStatusDot(const std::string& colorHex) {
    winrt::Microsoft::UI::Xaml::Shapes::Ellipse dot;
    dot.Width(10.0);
    dot.Height(10.0);
    unsigned int v = 0x9E9E9E;
    if (colorHex.size() == 7 && colorHex[0] == '#') {
        try {
            v = static_cast<unsigned int>(std::stoul(colorHex.substr(1), nullptr, 16));
        } catch (...) {
        }
    }
    Media::SolidColorBrush b;
    b.Color(Windows::UI::Color{
        0xFF, static_cast<uint8_t>((v >> 16) & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF),
        static_cast<uint8_t>(v & 0xFF)});
    dot.Fill(b);
    Controls::Border wrap;
    wrap.VerticalAlignment(VerticalAlignment::Center);
    wrap.Child(dot);
    return wrap;
}

/// 副状态文本（灰、12px）。
inline Controls::TextBlock MakeCaption(const std::wstring& text) {
    Controls::TextBlock t = MakeText(text, false, 12.0);
    t.Opacity(0.7);
    t.TextWrapping(TextWrapping::Wrap);
    return t;
}

}  // namespace tf::ui::kit

#endif  // TF_UI_SETTINGS_UI_KIT_HPP
