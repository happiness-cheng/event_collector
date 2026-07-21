# 告警规则

本目录包含 Prometheus AlertManager 告警规则。

## 生效方式

将规则文件挂载到 Prometheus 的 `rule_files` 配置中：

```yaml
# prometheus.yml
rule_files:
  - "alerts/*.yml"
```

## 规则清单

| 规则文件 | 告警名 | 触发条件 | 严重度 |
|---------|--------|---------|--------|
| `dead_letter.yml` | DeadLetterAccumulating | `event_store_dead_total > 0` 持续 5 分钟 | critical |
