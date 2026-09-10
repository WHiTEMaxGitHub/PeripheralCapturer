#pragma once

#include <QtWebView/QWebView>

// POV：独立置顶网页浮层（Windows = WebView2）。不要作为配置窗的子 HWND。
    Q_OBJECT

public:
    explicit PovWindow(QWindow* parent = nullptr);

    void setClickThrough(bool enabled);
    bool clickThrough() const { return clickThrough_; }

protected:
    bool event(QEvent* event) override;

private:
    void applyClickThrough();

    bool clickThrough_ = true;
};
