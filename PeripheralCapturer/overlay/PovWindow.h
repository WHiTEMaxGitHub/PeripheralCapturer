#pragma once

#include <QtWebView/QWebView>

// POV：独立置顶 WebView2 壳。画面由 Vue 组件画；C++ 只做置顶和点击穿透。
// Debug 等 Vite :5173 就绪再 Navigate；失败才 loadHtml 离线提示页。
class PovWindow : public QWebView {
    Q_OBJECT

public:
    explicit PovWindow(QWindow* parent = nullptr);

    void setClickThrough(bool enabled);
    bool clickThrough() const { return clickThrough_; }
    void loadDevPage();
    void loadOfflineHtml();

protected:
    bool event(QEvent* event) override;

private:
    void applyClickThrough();

    bool clickThrough_ = true;
    bool loadedOfflineHtml_ = false;
};
