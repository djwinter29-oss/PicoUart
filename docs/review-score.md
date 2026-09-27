# 项目审查评分

评分卡对照当前 `main`。分数是代码质量，不是路线图完成度。权重与上一张卡相同：控制面正确性 25%，架构、数据面、测试各 15%，USB、安全、文档各 10%。综合分按加权和保留一位小数。

上一张卡在 PR #37，对照 `5b677e6`，修复 `CONTROL_PENDING` 之后综合 **8.6**（加权 8.55）。本卡对照 `7893283`（2026-09-27）。

| 维度 | 上次 | 本次 | 依据 |
| --- | ---: | ---: | --- |
| 架构与模块边界 | 9.0 | 9.0 | core 0 管 USB，core 1 管 UART；每口一个邮箱；HW/PIO 走同一套 adapter。PIO RX 使用独立分频，不改变这条边界 |
| 控制面正确性 | 8.6 | 8.8 | 邮箱确认和 worker 所有权在同一把 `status_lock` 里登记；apply 截止时间在碰后端之前检查。立即拒绝路径仍会先放开临时所有权再完成邮箱 |
| 数据面（ring / DMA / UART） | 8.5 | 8.6 | PIO RX 改为 22 拍、3 样本多数表决，RX 分频单独校验。50 µs settle 后仍会发布进度；硬件 UART `deinit` 仍不发布最后一段 RX |
| USB CDC/HID 契约 | 8.5 | 8.5 | `cafe:4010`、6×CDC + 1×HID、报告布局、LED 策略、复位默认关闭仍由测试锁住 |
| 测试与 CI | 8.5 | 8.6 | 仍是 18 个 Unity 套件和主机 pytest。控制面回归覆盖了交接原子性和过期截止。已有一份 RP2040 开发 HIL，但固件是 `5164be3`，早于当前 PIO RX |
| 安全与发布策略 | 7.5 | 7.5 | 实验室 USB ID 和默认关闭的 HID 复位仍是写明的策略 |
| 文档 | 9.0 | 9.0 | 交接时序、截止时间、PIO RX 周期和未记录的发布项都写进了设计说明。`uart.pio` 里的周期解释器不在仓库中 |
| **综合** | **8.6** | **8.6** | 加权和 8.63，四舍五入后仍是 8.6 |

加权：`0.15×9.0 + 0.25×8.8 + 0.15×8.6 + 0.10×8.5 + 0.15×8.6 + 0.10×7.5 + 0.10×9.0 = 8.63`。

## 相对上一张卡升高的部分

- **控制面。** `uart_control_plane_service()` 在拿走邮箱的同一临界区里把 `pending_controls[].pending` 设上。`test_control_plane_mailbox_ack_and_worker_ownership_are_atomic` 在 `spin_unlock` 时检查：并发的拒绝不能把端口看成无人持有。`service_pending` 在 TX 边界已排空之后、调用 `set_line_coding` 之前再查截止时间，过期的 apply 不能清掉 `CONTROL_ERROR`。
- **数据面。** PIO RX 与 TX 分频分开。`UART_LINE_CODING_PIO_RX_CLOCKS_PER_BIT` 是 22，TX 仍是 8。多数表决只影响 RX 状态机。在 125 MHz / 150 MHz 和 3 Mbaud 上限内，RX 分频仍落在 `[1, 65536)`。
- **测试记录。** `docs/tests/performance-test-results.md` 有 2026-09-24 的 RP2040 全矩阵开发记录（`5164be3`，四个功能阶段和 1 Mbaud 单链路通过）。这不是发布资格，也没有 Pico 2 记录。

## 仍压着分数的判断

- 临时所有权的释放和邮箱完成是两次加锁。`uart_control_plane_release_provisional_pending()` 先把 `pending` 清掉并解锁，然后 `finish_mailbox_control()` 再加锁写 `CONTROL_ERROR`。注释写的是同一把锁；代码不是。这条路径只出现在非法线格式、后端不接受、后端不可用，以及新请求已经匹配当前格式时。线格式不会在这次请求里改变，所以窗口里放行的 TX 仍是旧格式。原子性测试只探测拿走邮箱时的那次解锁。
- RX DMA 在暂停后 50 µs 即使采样不稳定也会发布进度。改成失败重试需要硬件在环。
- 硬件 UART `deinit` 不像 PIO 那样先发布最后一段 RX。只影响拆卸或初始化回滚时最多一段在途数据。
- USB 拔插取消 soft-pending，且不制造 `CONTROL_ERROR`。空的 deadline 不能事后变成超时错误。
- 新请求若已经匹配当前后端，会取消尚未应用的旧请求。线格式没有变，那条 TX 边界不再需要。
- `cafe:4010`、默认关闭的 HID 复位、未认证的 LED 切换是产品策略。
- PIO 多数表决的周期级检查写在 `uart.pio` 注释里，指向仓库外的解释器。当前 HIL 固件也不包含这次 RX 程序。

## 本次环境里核对过的结果

无板卡。

- Unity/CTest：18/18 通过。
- `host/python/tests`：135 通过。
- `pico` 与 `pico2` 固件构建成功。
- `verify-build.py`：两块板均为 `cafe:4010`、6×CDC/1×HID、HID reports `[1, 3, 4, 5]`。

硬件在环不在这台环境里。`5164be3` 的 RP2040 记录不能代替当前 PIO RX 程序的板级结果。
