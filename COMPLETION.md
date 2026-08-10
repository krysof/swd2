# 100% 完成门禁

`porting-status.json` 是本工程唯一的完成状态来源。编译成功、现有测试全部通过、生成
WASM 或发布 GitHub Pages 都只是阶段检查点，不等于移植完成。

## 状态规则

- `pending`：尚未开始，或还没有可验证产物；
- `in_progress`：已有实现，但该门的验收条件尚未全部满足；
- `verified`：验收条件全部满足，且 `evidence` 指向仓库内的实现、测试或逆向记录。

不使用各门平均值冒充“完成百分比”。只要还有一个门不是 `verified`，全项目状态就只能是
`NOT COMPLETE`；只有全部门通过并执行最终验证命令成功，才输出 `COMPLETE=100%`。

## 使用方法

检查清单结构和证据路径：

```sh
./scripts/audit-completion.py --validate
```

最终完成检查：

```sh
./scripts/audit-completion.py --require-complete
```

第二条命令当前会按预期失败，并列出所有未完成门。未来所有门变为 `verified` 后，它还会
依次执行原生构建/测试、WASM 构建、像素差分、存档往返、完整通关回放和长期运行检查；
任何一项失败都不能报告100%。中途版本仍可发布，但必须称为 checkpoint。

像素差分、完整通关和长期平台矩阵不能靠手工一句“已测试”放行。对应脚本要求仓库内存在
`verification/pixel_diffs/manifest.json`、`verification/playthrough/manifest.json` 和
`verification/long_run/manifest.json`；manifest 必须为 `status=verified`，并列出每个
证据文件的完整 SHA-256。文件缺失、摘要变化或空证据都会让最终命令失败。清单结构检查
也会提前拒绝不存在或没有执行权限的 `./scripts/...` 完成命令。

每个 artifact 还必须带可审计的 `role`。像素差分至少含 input/baseline/rewrite_output/
diff_report，通关至少含 input/original_trace/rewrite_trace/comparison，长期矩阵至少含
native_log/browser_log/matrix_report。通关脚本会进一步解析现代回放 trace，拒绝未消费
输入、隐式退出、空帧或没有实际进入 MEO/RPG/FIG/DEMO 四模块的伪“通关”证据。
随后它会现场重算 original_trace 与 rewrite_trace 的输入检查点、每帧、音频、逐次延时、
统一音画/输入时间线、最终 SAVE/MAPZ、停止状态和模块切换，并要求结果与带哈希的
comparison 报告完全一致。
像素差分也不是只校验一份手写 JSON：现代回放可把每次提交的 320×200 索引页、VGA
调色板和直接写页标志保存为带输入序列及完整结尾计数的 `SWD2FRM2` 流；门禁会现场严格解析原版/
重写捕获，逐帧要求索引像素、调色板、页类型乃至容器字节完全相同，并重算 diff_report。

长期测试允许先保存 `status=in_progress` 的检查点，例如
`verification/long_run/checkpoint-2026-08-10-macos/`；这类目录可证明已有循环并把 gate 从
`pending` 推进到 `in_progress`，但 `verify-long-run.sh` 仍只接受最终的
`verification/long_run/manifest.json`、`status=verified` 和完整平台角色，不能拿检查点
替代 Windows/Linux、品牌浏览器、物理手机/手柄及多小时活动游戏验收。
`scripts/capture-original-dosbox.py` 可以在固定 DOS 日期/时间和隔离的 `C:\\SWD2` 下生成
带哈希 manifest 的原版 RGB 录像/抽帧，用于定位场景和复核时序；但 DOSBox-X 录像已通过
VGA DAC 转色，只是 `reference_only`，不能代替上述索引像素及逐帧调色板证据。
当捕获需要小型入口 harness 时，manifest 必须同时记录 `program_sha256` 和独立的
`reference_program_sha256`；不允许用 harness 摘要替代被观察的原版 EXE 摘要。区域 RGB
抽查也必须明确登记 crop，不能把被排除的动态背景算作已验证。

`scripts/audit-runtime-sources.py` 另外以失败封闭方式检查生产源码：所有平台无关翻译单元
必须确实列入 `swd2_core`，四个原模块必须仍在单进程核心中，且源码不得重新出现显式
占位标记、`#if 0` 死代码、DOS 模拟器依赖或子进程逃生路径。这个检查只能排除可静态识别
的占位，不替代逐场景差分和完整通关证明。

## 更新要求

1. 先满足 `acceptance`，再补充可复查的 `evidence`；
2. 未完成门必须保留具体 `remaining`，禁止只写“继续完善”；
3. 新发现的原版行为若不属于现有门，必须新增门或扩充相应验收条件；
4. 删除或弱化验收条件也必须有新的逆向证据和回归测试支持。
