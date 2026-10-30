# Codex StopWatch — 开发规格说明（Spec v1.0）

- 状态：Ready for implementation（待真机验证项已显式标注）
- 日期：2026-10-30
- 目标设备：M5Stack StopWatch（ESP32-S3R8、466×466 AMOLED、BMI270、两个可编程按键）
- 基础参考：[alexjc-tech/cc-island](https://github.com/alexjc-tech/cc-island) @ `f749756be8bfcaad65f5dbc7dc592f498db145e8`
- 原厂工程：[m5stack/M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo)
- 原则：**只展示一个 Codex 限额窗口；界面仅显示剩余额度百分比、重置倒计时、独立一行的毫秒数字、外围单段连续进度环。**

## 0. 最终决策

1. Mac mini 运行常驻 Python Bridge，StopWatch 作为 BLE peripheral，Mac 通过 Bleak 连接并写入简洁的 JSON；设备不存凭据。
2. 固件基于 M5StopWatch-UserDemo + ESP-IDF + LVGL + Mooncake；**不是直接修改当前已刷的小智固件**。如要与小智共存，需要单独做应用集成与资源冲突验证。
3. 默认显示 5H；右侧可编程键短按切换 5H / Weekly；左侧短按手动刷新；两个按键同时长按返回主页，遵守原厂 `KeyManager` 的行为。
4. 显示：5H 绿色 `#45E9A3`；Weekly 琥珀色 `#F6B953`。数字白色（整数时分秒），额度百分比及毫秒数字使用当前模式强调色。
5. 圆环长度 = **剩余额度比例**，与倒计时进度无关。服务端 `usedPercent` 应转成 `remainingPercent = 100 - usedPercent`，不允许当作剩余额度直接显示。
6. 本地通过 `esp_timer_get_time()` 驱动屏幕每 20–33 ms 计算与绘制，**不能把毫秒显示理解为官方重置时间具有毫秒精度**。
7. 重置动画只有在新周期被数据源证实后触发。倒计时到 0 但数据尚未确认，保持 0 并请求刷新，不得虚构 100%。
8. 5H 未提供时，支持一次可配置、用户明确启用的 `codex exec` 轻量探测后重试；仍不存在则显示**估算**倒计时，圆环灰色虚线且百分比未知；禁止不断执行自动探测。
9. BMI270 加速度计完成 0/90/180/270° 四向重力转正；面朝上/下、晃动或重力不可判定时锁定最近方向；**只旋转中央数字区域，圆环固定**。

## 1. cc-island 源码审计与复用结论

| 参考源文件 | 已确认实现 | 处理方式 |
|---|---|---|
| `bridge/codexisland_bridge.py` | `fetch_codex()` 从 `~/.codex/auth.json` 获取 access token，GET `https://chatgpt.com/backend-api/wham/usage`，解析 `rate_limit.primary_window`/`secondary_window` | 可作适配器；优先考虑 `codex app-server` 的结构化限额接口；未文档化 HTTP 实现为兼容回退 |
| 同上 `compact()` | 输出 `c/x` 双 provider JSON，字段 `h,d,r,$,t`；`r` 是剩余**分钟** | 必须改：只推 Codex，推送**两个窗口的绝对重置时间戳**和数据状态，不推 Claude/成本/token |
| 同上 `ble_loop()` | Bleak 中央端扫描、连接、NUS 写入、通知、断线重连；5 分钟自动刷新，手动刷新 5 秒节流 | 保留主循环，提取采集和通信模块；加序号、时钟锚点、限额有效性校验 |
| `firmware/app_codex/ble/ble_nus.cpp` | NimBLE NUS service，RX 写、TX notify，换行结尾，256 bytes 接收缓冲 | 基本复用。增加大小/格式/版本/序号校验，旧包丢弃；BLE 写的分片行为实测 |
| `firmware/app_codex/app_codex.cpp` | `AppCodex` Mooncake 生命周期；两个 provider bar；`lv_bar_set_value(..., LV_ANIM_OFF)` 无动画；G1/`GoNext` 发刷新通知 | 重写为 `QuotaWatchApp`，单圆环 + 数字，新增状态机与动画。`GoNext` 改切换模式，`GoPrevious` 改手动刷新 |
| `scripts/install_firmware.sh` | clone 官方 UserDemo，拷贝 app/ble/assets，注入 `apps.h`/`main.cpp`/`sdkconfig.defaults`，启用 NimBLE | 以它为基础制作新安装脚本，固定原厂 commit 后测试，改注册类与资源 |
| `scripts/setup_autostart.sh` | LaunchAgent 开机登录启动 Python bridge（默认 5min） | 复用并调整名称/日志，保证崩溃自动恢复，不输出 token |

参考源码：
- https://github.com/alexjc-tech/cc-island/blob/main/bridge/codexisland_bridge.py
- https://github.com/alexjc-tech/cc-island/blob/main/firmware/app_codex/app_codex.cpp
- https://github.com/alexjc-tech/cc-island/blob/main/firmware/app_codex/ble/ble_nus.cpp
- https://github.com/alexjc-tech/cc-island/blob/main/scripts/install_firmware.sh

**注意**：原项目 `fetch_codex().win()` 在窗口缺失时使用 `used_percent=0` 作为默认，`compact()` 缺失时也转成 0；这会把“未知”误表示为“0% 消耗/100% 剩余”。新适配器必须显式使用 `null / unavailable`，不能如此回退。

## 2. 产品需求与视觉规范

### 2.1 单表盘布局

- 页面尺寸以 **466×466** 逻辑画布为基准；布局全部相对圆心 `(233,233)`。
- AMOLED 黑背景 `#050907`，无标题、品牌名、`5 HOURS`、`WEEKLY`、小数点或解释性文字。
- 额度环：圆心 `(233,233)`，半径约 `206px`，宽 `12px`，固定 12 点起始，顺时针显示余量；深色轨道 `#26342C`。**只允许一条连续亮色弧线，不拆成两段**。
- 额度百分比：正上方偏内（约 y=135），约 `52px`，粗体，`75%`。一旦有效来源缺失，显示 `--%` 或按下述估算状态处理。
- 主倒计时：中央（约 y=226），5H 显示 `HH:MM:SS`，字体 65–74px（以实际字形宽度动态适配）；Weekly 显示 `DD:HH:MM:SS`，字号约 46–54px，保证圆形可用区域不裁切。
- 毫秒：主时间下方（约 y=319），只显示三位 `870`，不显示 `.870`，字号 76–86px；专用等宽数字字体，固定宽度。
- 主时间与毫秒采用单调倒数方式滚动；整体不得因数字宽度变化而水平抖动。
- 模式差异**只靠颜色与时间长度区分**；用户有右键切换操作，无需任何标题文案。可选两个不显眼的模式状态点，默认关闭。

### 2.2 数字与圆环

- `remaining_pct = clamp(100 - used_percent, 0, 100)`，百分比允许数据模型保留 1 位小数，UI 默认按四舍五入的整数显示。
- 圆环 sweep 角度 `360° × remaining_pct / 100`，0% 不绘亮色弧线，100% 整圆。
- 圆环**不随倒计时经过而减少**，只随 Codex 用量更新；切换模式时过渡到另一个窗口值。
- 未知数据禁止默认显示 100%。
- 低额度（≤15%）暂不强制变红，保留按模式配色；V1 不要叠加复杂告警。

### 2.3 动画规格

| 事件 | 动画 | 时长/缓动 | 触发约束 |
|---|---|---|---|
| 额度消耗 | 圆环末端平滑收缩，百分比滚数 | 500–650 ms ease-out | 仅有效数据值改变时触发 |
| 额度修正增长 | 圆环增长，数字同步 | 550 ms ease-out | 不误判为重置 |
| 右键模式切换 | 圆环长度和颜色联动；中央数字淡出/滑入 | 250–350 ms | 禁止多次按键排队，取最后状态 |
| 每秒变化 | 秒位数字短距离垂直翻滚 | 140–180 ms | 只动画变动数字位，避免整个字符串抖动 |
| 毫秒更新 | 三位整数直接刷新，不添加小数点 | 20–33 ms 更新一次 | 刷新显示区，不逐毫秒强制重绘整屏 |
| 重置确认 | 00:00:00 停留→轻微闪光→旧环清空→按新目标比例扫入→时间滚入 | 200 + 120 + 180 + 700–900 ms | **必须收到新周期权威数据**才能确认重置 |
| IMU 转正 | 中央内容最短方向旋转 | 200–250 ms | 角度稳定满足去抖后触发 |

所有动画可中断（例如中途接收新数据时重定目标值）。不使用大量粒子或整屏缩放。动画完成后要使 UI 值与权威数据完全一致。

## 3. 功能行为

### 3.1 两个模式

- 默认 `Mode::FiveHour`；右侧可编程键短按切换为 `Mode::Weekly`，再按切回。
- `FiveHour`: 主时间 `HH:MM:SS`，时不足两位补零；在正常窗口内不超过 5h。
- `Weekly`: 主时间 `DD:HH:MM:SS`；不省略天数，即使剩余不足一天也显示 `00`。
- 同一设备缓存两个窗口，切换无需重新读取 Mac 数据，直接显示本地计算值。
- 模式选择写入 NVS，重启后恢复；首次启动默认 5H。

### 3.2 按键映射

- 原厂 `KeyManager` 中 `btnA.wasClicked()` → `GoPrevious`；`btnB.wasClicked()` → `GoNext`；两键持续按住 → `GoHome`。
- **物理右键对应哪一个 GPIO/btnA/btnB 必须在真机确认**。默认按用户右键映射 `GoNext` 设计，若实测反向，则仅交换映射，不改 UI 逻辑。
- 右键短按：切换 5H/Weekly；轻振动（可配置）。
- 左键短按：通过 NUS TX notify 发送 `R` 请求 Mac 手动刷新；保留 5 秒节流（可视情况调大）。
- 两键长按：退出到原厂主页。电源键保持原厂语义，不截获。

### 3.3 姿态自动转正

- 用 BMI270 **加速度**判断重力方向，优先用官方 `GetHAL().updateImuData()`、`GetHAL().getImuData()`；原厂 `hal_imu.cpp` 当前采用 `BOSCH_ACCELEROMETER_ONLY`，所以不要假设现有固件中 gyro 读数一定有效。
- 每 50 ms 采样，对 X/Y 低通滤波、确定候选方向；角度分到 0/90/180/270 度，设置 20° 左右滞回 + 300 ms 稳态去抖。
- 若重力主要作用在 Z 轴（屏幕平放）、合加速度异常或剧烈运动，保持最后稳定方向，不翻转。
- `ContentLayer = {Percentage, Countdown, Milliseconds}` 作为一个整体绕圆心旋转；环形 `ProgressRing` 保持固定不旋转；按钮行为不变。
- 实现策略：先验证当前 LVGL 版本对文本容器旋转的支持。如不具备可靠的父容器含子对象旋转，使用**内容离屏画布 → LVGL image rotation**（`lv_image_set_rotation`、`lv_image_set_pivot`），或按照 4 向重新计算三组 label 位置；优先省内存的可靠方案。
- 若通过离屏画布旋转，确认 RGB565/ARGB8888 缓冲资源、PSRAM 占用、旋转抗锯齿和部分刷新性能后再定稿。

### 3.4 数据缺失/离线

- 五小时窗口缺失 ≠ 100% 剩余，必须标 `unavailable`。
- 允许可配置的**人工启用** 5H 引导探测：首次缺失→人工触发/明确启用→调用一次 `codex exec --skip-git-repo-check '只回复 OK'`→再查询窗口；这是真实请求，可能消耗使用量，不得静默持续执行。
- 若仍不存在官方 5H `reset_at`，以探测请求完成时刻 + 5h 作为**本地估算倒计时**（并明确其不代表服务端真实滚动窗口），圆环显示灰色虚线，百分比 `--%` 或不显示；不虚构官方额度。
- 任何时候发现官方数据，立即替换估算值并做短过渡。
- BLE 断线：保持最后可计算的 countdown；额度超过 15min 未更新，降低圆环亮度提示数据可能陈旧；超过 6h 或重置时间已过仍未确认新数据，进入 `stale`，不要自动回满。
- 设备重启而尚无有效时间锚点：显示 `--:--:--` 和 `---`，直到 Bridge 初次连接，避免假倒计时。
- 数据读取异常（401/403/429/5xx/网络失败）：Mac 记录日志并退避重试；不得用 0 或 100 替代错误窗口。

## 4. 技术架构

```text
Mac mini（本地常驻）
  ├─ CodexQuotaProvider
  │    ├─ app-server  account/rateLimits/read（优先）
  │    └─ wham/usage + ~/.codex/auth.json（cc-island 兼容层；非公开 API，可能变化）
  ├─ QuotaNormalizer（used->remaining，窗口识别、source、状态）
  ├─ QuotaCache / Poller（定时刷新，异常退避，探测冷却）
  └─ BLEBridge（Bleak central；NUS GATT RX 写入/TX 监听）
                 │  本地 BLE（无凭据，仅限额及时间）
                 ▼
M5Stack StopWatch（NimBLE peripheral）
  ├─ ble_nus（收消息、队列）
  ├─ QuotaStore（5H/Weekly 快照 + 数据有效性）
  ├─ CountdownClock（单调时钟锚点）
  ├─ ModeController（btnB / 右键切换）
  ├─ OrientationController（BMI270 四向转正）
  ├─ AnimationController（环形进度、重置、翻数）
  └─ QuotaWatchView（LVGL：Ring + Percentage + Time + Milliseconds）
```

推荐分层：Mac 只提供**数据真实性及传输**；ESP32 自行做所有短周期计时/姿态/动画，**不通过 BLE 发送每 10ms 的数字**。

### 4.1 Codex 数据适配

**主要接口**：Codex App Server `account/rateLimits/read`。解析 `rateLimitsByLimitId.codex`（若有），否则用 `rateLimits`；检查 `primary`、`secondary` 的 `windowDurationMins`，按 300 / 10080 分钟识别窗口，不可盲信 primary 永远是 5H、secondary 永远是周窗口。可能只返回一个窗口或不同窗口长度。参考：<https://learn.chatgpt.com/docs/app-server>。

**兼容接口**：参考 cc-island 的 `GET https://chatgpt.com/backend-api/wham/usage`，解析 `rate_limit.primary_window/secondary_window`，应核查 `limit_window_seconds` 后再归类；该 URL 属 Web 客户端内部服务，可能改变，不作为稳定官方公开 API。

**错误语义**：区分 `missing_window`、`no_auth`、`network_error`、`rate_limited`、`stale`、`estimated`、`ok`。`usedPercent` 可能为 0 但确实有效，判断依据应为字段存在+类型有效，而不是数值非零。重置时间 `null` 视为该窗口无法倒计时。

### 4.2 BLE 协议 v1

保留原项目 Nordic UART Service：

- NUS service：`6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- Mac → Watch（RX characteristic）：`6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- Watch → Mac（TX notify）：`6e400003-b5a3-f393-e0a9-e50e24dcca9e`
- UTF-8、每条单行 JSON、`\n` 结尾；总长必须小于 firmware 当前 `kLineMax=256` bytes。

正常示例（字段紧凑、示例值不是真实账户数据）：

```json
{"v":1,"q":42,"t":1793360000123,"h":{"u":25,"r":1793368000,"s":"api"},"w":{"u":65,"r":1793900000,"s":"api"}}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `v` | integer | 协议版本，当前为 1 |
| `q` | integer | 单调递增序列号，忽略旧序号 |
| `t` | int64 | Mac 发送时 Unix 毫秒戳，作为接收端锚点 |
| `h`,`w` | object or null | 对应 5H 和 Weekly。null 表示服务端没给该窗口 |
| `u` | float or null | **已使用百分比**；设备转换成剩余比例 100-u |
| `r` | int64 or null | 对应窗口重置 Unix 秒戳 |
| `s` | string | `api` / `estimated` / `stale` / `unavailable` |

缺失示例：

```json
{"v":1,"q":43,"t":1793360001123,"h":null,"w":{"u":65,"r":1793900000,"s":"api"}}
```

估算示例：

```json
{"v":1,"q":44,"t":1793360002123,"h":{"u":null,"r":1793378000,"s":"estimated"},"w":null}
```

**安全**：BLE 广播名称只用于发现，不作为认证；初版适用用户本地可信环境。若要抗附近设备伪造写入/长期部署，增加 BLE 配对/绑定或消息认证；`~/.codex/auth.json` 和 token 严禁推送到 Watch 或写入日志。

### 4.3 CountdownClock（单调时钟）

接收数据时取本机 `recv_mono_us = esp_timer_get_time()`；通过 `t`（Mac send epoch ms）和 `r`（reset epoch s）求：

```text
initial_remaining_ms = r * 1000 - t
elapsed_ms = (esp_timer_get_time() - recv_mono_us) / 1000
remaining_ms = max(0, initial_remaining_ms - elapsed_ms)
```

这样不会依赖设备 RTC 是否正确，也不会因为 LVGL 掉帧累积时间误差。BLE 传输延迟会引入小误差；可在连接时增加轻量时钟往返校准来修正。所有计时使用 `int64_t`。

每 20–33 ms 计算最新显示：

```text
T = floor(remaining_ms / 1000)
ms = remaining_ms % 1000  // 三位补零，无点
five:  HH = floor(T/3600), MM=(T/60)%60, SS=T%60
week:  DD=floor(T/86400), HH=floor(T/3600)%24, MM=(T/60)%60, SS=T%60
```

秒单位的服务端 `reset_at` **无法确认毫秒偏移**。毫秒显示是平滑视觉效果，0 点前后的真实服务端配额状态必须再次验证。设备从 deep sleep 唤醒后需要重新建立单调时钟锚点。

### 4.4 生命周期与状态机

- `BOOT`: 初始化 BLE、NVS、UI/默认模式。
- `WAIT_DATA`: 尚无有效快照，显示占位。
- `LIVE`: 至少当前模式有有效 API 窗口，正常计时和消费动画。
- `ESTIMATED`: 5H 无官方数据且用户已启用探测，估算倒计时+灰环。
- `STALE`: 最近成功数据已超过阈值；允许继续计时，但颜色降低，数据不充当最新额度。
- `RESET_PENDING`: 本地计时到 0，但未证实下一个周期；触发限流的手动刷新，不直接回满。
- `RESET_ANIMATING`: 新 `reset_at` 明确大于旧 `reset_at`（或其他可靠新周期信号）后，执行环形回满动画；若目标不是 100%，扫至新实际剩余比例。
- `DISCONNECTED`: BLE 断开，恢复扫描，UI 不阻塞。

在 `onRunning()` 里只轮询按键/消息、更新状态机；不要在 UI 线程执行网络请求、BLE 扫描或阻塞 sleep。所有 LVGL 操作在受保护的 GUI 上下文，遵循 `LvglLockGuard`。

## 5. 推荐文件组织

```text
codex-stopwatch/
├── bridge/
│   ├── main.py                # CLI 参数/守护进程入口
│   ├── quota_provider.py      # app-server + wham 适配、鉴权错误
│   ├── quota_normalizer.py    # 窗口识别、缺失/陈旧语义
│   ├── quota_probe.py         # opt-in codex exec，仅一次与冷却
│   ├── protocol.py            # v1 JSON 编解码
│   └── ble_bridge.py          # 迁移 cc-island 的 Bleak 逻辑
├── firmware/
│   └── app_codex_timer/
│       ├── app_codex_timer.{h,cpp} # Mooncake App 生命周期函数
│       ├── ble/ble_nus.{h,cpp}     # 基于 cc-island
│       ├── model/quota_state.{h,cpp}
│       ├── controller/countdown_clock.{h,cpp}
│       ├── controller/orientation.{h,cpp}
│       ├── controller/mode_controller.{h,cpp}
│       ├── view/watch_face.{h,cpp}
│       └── view/animations.{h,cpp}
├── scripts/
│   ├── install_firmware.sh
│   └── setup_autostart.sh
├── tests/
│   ├── protocol_vectors.json
│   ├── test_normalizer.py
│   └── test_quota_clock.cpp
└── SPEC.md
```

## 6. 交付阶段

### Phase 0 — 环境与参考工程

- 克隆/固定 `cc-island` 与 `M5StopWatch-UserDemo` 的具体 SHA。准备 ESP-IDF v5.5.4、NimBLE、LVGL、Mooncake 依赖。
- 真机确认两侧按键物理映射、屏幕逻辑分辨率和 BMI270 轴向。
- 备份原固件，确认刷机后的恢复路径；在基础工程中新建独立应用入口。

### Phase 1 — 数据与 BLE

- 验证 Plus 账号 `account/rateLimits/read` 实际返回窗口及时间戳；检查有无 5H。
- 将完整两窗口数据经 NUS 推送；设备用串口输出解析结果。
- BLE 断开重连、过期 token、窗口缺失、协议边界和设备恢复测试。

### Phase 2 — 静态表盘与计时

- 单圆环、百分比、主倒计时、独立毫秒行。
- `esp_timer_get_time()` 单调时钟倒数；右键切换两个窗口、左键请求刷新。
- 不因蓝牙是否在线导致 UI 读秒卡顿。

### Phase 3 — 动画、IMU

- 实现消费、重置、切换、翻数；真实新周期确认逻辑。
- 加速度四向姿态转正；静止/平放防抖、转向时显示正确。
- 实测 CPU、PSRAM、FPS、耗电与触控/按钮干扰。

### Phase 4 — 可靠性及部署

- 配置 LaunchAgent、自启动、带状态的本地缓存、日志轮转；保证 Mac mini 关屏仍在登录会话中运行。
- 异常情况下不误报 100%；低电量和长时间 BLE 断线条件稳定。
- 可选启用 5H 探测，确认只发一次真实 Codex 请求；不得循环探测。

## 7. 验收标准

| 场景 | 验收条件 |
|---|---|
| 单圆环 | 无第二条亮色弧段，进度长度与官方**剩余额度**一致（视觉误差 ≤ 1%） |
| 5H / Weekly 切换 | 右键每次切换成功；数值/颜色/圆环协调，切换动效约 300 ms |
| 毫秒显示 | 三位数字独立一行，无前导点；连续运行不因帧丢失累积时间误差 |
| 百分比 | 服务端 `used=25` 画出 `75%`；`used=0` 画出 `100%`；缺失时不画 100% |
| 消耗 | 75→70% 在约 650 ms 内平滑完成，不产生断弧 |
| 重置 | 倒计时到零后等待权威数据，确认新窗口后动画扫到新百分比，不假定 100% |
| BLE 断线 | 停止新额度同步，但本地倒数连续，重连后数据正常覆盖 |
| 姿态 | 四方向自动转正；水平平放/剧烈晃动稳定；圆环始终固定 |
| 原厂应用兼容 | 两键长按可回主页，其他预装应用可以继续打开 |
| 安全 | ESP32 无 access token、Mac 日志不含 token |
| 无 5H | 正确展示 `unknown`；可选单次探测失败后仍不伪造百分比 |

## 8. 待真机确认与风险

1. **可用的 5H 窗口**：Plus 账号不保证在 API 中同时返回 5H 与 Weekly。不能用推测值替代真实 quota。
2. **数据接口耐久性**：`wham/usage` 为未公开 Web 服务端接口，优先 `app-server`；两者都需要容错与版本适配。
3. **毫秒表象**：UI 可每 20–33ms 更新，但真实重置时间只到秒；需要避免对“毫秒准确”作保证。
4. **NUS 发送长度**：原 `kLineMax=256`，需要验证 BLE ATT MTU/长写支持与通知能力。不得只假设单次 200 字节写一定成功。
5. **IMU 原厂模式**：`hal_imu.cpp` 默认为 `BOSCH_ACCELEROMETER_ONLY`，四向转正够用；若后来用 gyro 融合必须同步修改初始化。
6. **LVGL 旋转能力**：不能直接假设父容器子 label 会整体跟随旋转；要用屏幕实际测试，必要时改画布或重新布局策略。
7. **屏幕规格差异**：官方销售规格 466×466；原厂 `hal_display.cpp` 使用 468×466 的 panel config（物理面板处理细节）。以 `lv_display_get_horizontal_resolution/vertical_resolution` 为准，不要硬编码物理 panel 参数。
8. **安装脚本依赖 HEAD**：cc-island 的 `install_firmware.sh` 拉原厂最新主分支，建议固定版本，以免锚点字符串和原厂布局变更导致自动插入失败。
9. **固件共存**：本计划基于原厂 UserDemo 扩展，不意味着能直接插入现有小智固件或同时保有它的所有功能。

## 9. 来源

1. cc-island README：<https://github.com/alexjc-tech/cc-island/blob/main/README.md>
2. cc-island bridge：<https://github.com/alexjc-tech/cc-island/blob/main/bridge/codexisland_bridge.py>
3. cc-island app：<https://github.com/alexjc-tech/cc-island/blob/main/firmware/app_codex/app_codex.cpp>
4. cc-island NimBLE：<https://github.com/alexjc-tech/cc-island/blob/main/firmware/app_codex/ble/ble_nus.cpp>
5. 原厂按键管理：<https://github.com/m5stack/M5StopWatch-UserDemo/blob/main/main/apps/common/key_manager/key_manager.cpp>
6. 原厂 IMU：<https://github.com/m5stack/M5StopWatch-UserDemo/blob/main/main/hal/hal_imu.cpp>
7. 原厂 LVGL 初始化：<https://github.com/m5stack/M5StopWatch-UserDemo/blob/main/main/hal/hal_display.cpp>
8. StopWatch 官方硬件：<https://docs.m5stack.com/en/core/StopWatch>
9. Codex App Server 接口：<https://learn.chatgpt.com/docs/app-server>
10. ESP-IDF `esp_timer`：<https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_timer.html>
11. LVGL image rotation：<https://lvgl.io/docs/open/9.0/widgets/image.html>
