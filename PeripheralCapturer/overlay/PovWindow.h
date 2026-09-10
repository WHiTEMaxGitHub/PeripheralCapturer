#pragma once

#include <QtWebView/QWebView>

// POV：独立置顶 WebView2 壳。画面由 Vue 组件画；C++ 只做置顶和点击穿透。
// Vite 失败时用 loadHtml 读 qrc 提示页（不能 Navigate qrc:）。
class PovWindow : public QWebView {
    Q_OBJECT

public:
    explicit PovWindow(QWindow* parent = nullptr);

    void setClickThrough(bool enabled);
    bool clickThrough() const { return clickThrough_; }

protected:
    bool event(QEvent* event) override;

private:
    void applyClickThrough();
    void loadOfflineHtml();

    bool clickThrough_ = true;
    bool loadedOfflineHtml_ = false;
};
