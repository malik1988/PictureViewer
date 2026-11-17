#pragma once

#include "pv_common.h"

namespace pv {

// 在当前用户下注册/注销文件关联（不需要管理员权限）。
// ProgID 为 "PicViewer.Image.<版本>"。
bool RegisterAssociations();
bool UnregisterAssociations();

} // namespace pv
