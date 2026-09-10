#pragma once

// 日志前缀约定（热路径不要逐条打）：
// [app] [log] [ui] [timer] [capture] [pov] [registry] [pad] [bus] [db]
// 初始化 / 开停录 / 绑码用 info；失败 warn/error/critical。查表命中、逐帧 BLOB 不要打。
void initLogger();
void shutdownLogger();
