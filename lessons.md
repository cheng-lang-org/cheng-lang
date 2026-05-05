# Lessons

## Active

- CSG/lowering热路径：复用source context，勿重复建ctx；import edge用`ctx.lines`。
- 多行字符串rewrite：测长→一次分配→线性写入+hard-fail长度合同。禁O(n²)。
- Resolved call fact查找：按source建游标一次，路径归一化只做source级一次。
- Self-probe超30s：先`sample`再定优化点。
- 并行子代理改同批文件：扫冲突标记+语义断裂，不只看`git diff --check`。
- Runtime验证：锁`provider_object_count>0`+`standalone_no_runtime=0`+真实符号。不能只看exit 0。
- 短smoke：要求输出marker，host gate检查marker。`mov w0,#0; ret`是假绿。
- 函数级并行：必须接入active primary task path。库级executor不算完成。
- Work-stealing pop末项必须CAS；`head==nextTail`不用CAS会重复执行。
- pthread detach失败必须硬退出；ctx已飞入线程后返回错误=use-after-free。
- Provider executable：`emit=exe && providerModules.len>0`必须进provider compile+system link。
- 改CSG/lowering源码不加速已安装的旧`artifacts/backend_driver/cheng`。
- BodyIR data side table：writer未消费前必须给明确missing kind。禁静默fallthrough。
- BodyIR predicate/word count/reloc offset/emit必须消费同一op集合。

## Resolved

- `BackendDriverDispatchMinRunCommand` `cfg_no_top_if`：路由到BodyKindStatementSequence。
- `PathNormalize`逐fact：提升到函数级。
- 36个`firstMissing*`字段：收敛为`PrimaryObjectError`嵌套类型。
- Body kind膨胀：`PrimaryCallSequenceNeedsReloc`/`RequiredCallCount` helper，BodyIR优先。
- `BuildPrimaryObjectPlan` 650行单体：拆为8个阶段函数。
