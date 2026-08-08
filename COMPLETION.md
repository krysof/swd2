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

## 更新要求

1. 先满足 `acceptance`，再补充可复查的 `evidence`；
2. 未完成门必须保留具体 `remaining`，禁止只写“继续完善”；
3. 新发现的原版行为若不属于现有门，必须新增门或扩充相应验收条件；
4. 删除或弱化验收条件也必须有新的逆向证据和回归测试支持。

