#pragma once

// 日志前缀约定（热路径不要逐条打）：
// [app] [log] [ui] [timer] [capture] [pov] [registry] [pad] [bus]
void initLogger();
void shutdownLogger();
