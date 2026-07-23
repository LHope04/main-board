# Upboard IoT 管理后台使用说明

本文档对应管理员后台：管理员可查看所有设备的实时数据和历史数据，并可对在线设备远程调节水泵占空比；普通用户绑定和 HTTPS/TLS 后续再做。

## 1. 访问后台

后台地址：

```text
http://38.76.206.42:18080/
```

管理员账号由服务器部署目录中的 `.env` 配置：

```text
ADMIN_USERNAME=<管理员用户名>
ADMIN_PASSWORD=<管理员密码>
```

不要把生产密码写入本文档或提交到版本库。

登录后可以看到：

- 设备总数、在线设备数、当前选中设备、最后上报时间
- 所有设备列表
- 单个设备最新数据：RSSI、GPS、24V、12V、输出状态、NTC Raw、原始 Payload
- 24V 电压历史
- GPS 历史点地图（Leaflet + CARTO dark/OSM 数据，显示最新点和轨迹）
- 最近事件
- 页面底部水泵流速控制：0% 关闭，1-100% 设置 100Hz 软件 PWM 占空比

在线判断规则：设备 `last_seen_at` 距当前时间小于 90 秒时显示在线。

## 2. 服务器服务

服务部署目录：

```bash
ssh hk-newapi
cd /opt/upboard-iot
```

查看运行状态：

```bash
docker compose ps
```

当前包含三个服务：

```text
upboard-iot-mqtt       MQTT broker，端口 1883
upboard-iot-postgres   PostgreSQL，长期保存设备数据
upboard-iot-web        FastAPI + 前端，端口 18080
```

健康检查：

```bash
curl -fsS http://127.0.0.1:18080/healthz
```

正常返回：

```json
{"ok":true,"service":"upboard-iot"}
```

## 3. MQTT 上报

设备发布主题：

```text
upboard/{sn}/telemetry
upboard/{sn}/status
upboard/{sn}/event
```

设备订阅主题：

```text
upboard/{sn}/command/pump
```

水泵命令 payload：

```json
{"duty_pct": 60}
```

`duty_pct=0` 表示关闭水泵，`1..100` 表示启用水泵并设置占空比。命令不 retain，设备离线时不会在下次启动自动执行旧命令。

第一版所有设备共用一个 MQTT 账号：

```text
用户名：upboard_device
密码：mriTbXbm0MsMTF7irNergKE8
```

推荐 telemetry payload：

```json
{
  "sn": "UPB-DEMO-001",
  "seq": 1,
  "uptime_ms": 1000,
  "rssi": 22,
  "net": {"operator": "CHN-CT", "ip": "10.56.122.255"},
  "gps": {"lat": 22.300001, "lon": 114.100002, "fix": true},
  "sampler": {"ntc_raw": [1891, 10, 9, 10, 2538, 2540, 2537, 2537]},
  "power": {"bat24_v": 24.1, "bat24_i": 0.3, "v12_v": 12.4},
  "outputs": {"boost": true, "load": true, "fan": false, "pump": false, "pump_duty_pct": 0, "compressor": false}
}
```

要求：

- `sn` 必须随 payload 一起发送；如果缺失，后端会用 topic 里的 `{sn}` 兜底
- `seq` 建议单调递增，方便判断漏包或重复上报
- GPS 长期保存到 SQL，可通过后台和 API 查询历史点
- 原始 JSON 会完整保存在 `telemetry.raw_json`，后续字段扩展不需要立即改库表

当前 STM32 固件已经自动使用芯片 UID 生成 SN，例如 `UPB-0B506761`，并默认每 5 秒上报一次 telemetry。

## 4. 水泵远程控制 API

登录后调用：

```text
POST /api/devices/{sn}/controls/pump
Content-Type: application/json

{"duty_pct":60}
```

接口只允许管理员会话，范围为 `0..100`。成功返回 MQTT topic、目标占空比和 broker message id；实际执行结果以随后 telemetry 中的 `outputs.pump` 与 `outputs.pump_duty_pct` 为准。

## 5. EC801E 上报方式

EC801E 已验证使用 `QMTPUBEX` 长度模式，避免 `QMTPUB` + Ctrl-Z 的交互结束符不稳定。

连接 MQTT：

```text
AT+QMTOPEN=0,"38.76.206.42",1883
AT+QMTCONN=0,"UPB-DEMO-001","upboard_device","mriTbXbm0MsMTF7irNergKE8"
```

发布短 payload 示例：

```text
AT+QMTPUBEX=0,0,0,0,"upboard/UPB-DEMO-001/telemetry",56
{"sn":"UPB-DEMO-001","seq":2,"uptime_ms":2000,"rssi":22}
```

注意：

- `56` 是后面 JSON 的精确字节数，不包含回车换行
- 收到 `>` 提示符后，发送精确长度的 JSON
- 不要追加 Ctrl-Z
- 成功回执应包含：

```text
+QMTPUBEX: 0,0,0
```

计算 JSON 长度可以用：

```bash
python3 - <<'PY'
p='{"sn":"UPB-DEMO-001","seq":2,"uptime_ms":2000,"rssi":22}'
print(len(p.encode("utf-8")))
PY
```

## 6. 手动模拟上报

在服务器上发布一条测试 telemetry：

```bash
ssh hk-newapi
cd /opt/upboard-iot
set -a
. ./.env
set +a

docker compose exec -T mqtt sh -lc \
'mosquitto_pub -h 127.0.0.1 -p 1883 \
  -t upboard/UPB-DEMO-001/telemetry \
  -u "$MQTT_DEVICE_USERNAME" -P "$MQTT_DEVICE_PASSWORD" \
  -m "{\"sn\":\"UPB-DEMO-001\",\"seq\":100,\"uptime_ms\":100000,\"rssi\":22,\"gps\":{\"lat\":22.3,\"lon\":114.1,\"fix\":true},\"power\":{\"bat24_v\":24.1,\"v12_v\":12.4}}"'
```

发布后刷新后台，或查询 API。

## 7. API 查询

登录并保存 cookie：

```bash
cd /opt/upboard-iot
set -a
. ./.env
set +a

curl -fsS -c /tmp/upboard_cookie.txt \
  -H "Content-Type: application/json" \
  -d "{\"username\":\"$ADMIN_USERNAME\",\"password\":\"$ADMIN_PASSWORD\"}" \
  http://127.0.0.1:18080/api/auth/login
```

查看所有设备：

```bash
curl -fsS -b /tmp/upboard_cookie.txt \
  http://127.0.0.1:18080/api/devices
```

查看设备最新数据：

```bash
curl -fsS -b /tmp/upboard_cookie.txt \
  http://127.0.0.1:18080/api/devices/UPB-DEMO-001/latest
```

查看 telemetry 历史：

```bash
curl -fsS -b /tmp/upboard_cookie.txt \
  'http://127.0.0.1:18080/api/devices/UPB-DEMO-001/telemetry?limit=500'
```

查看 GPS 历史：

```bash
curl -fsS -b /tmp/upboard_cookie.txt \
  'http://127.0.0.1:18080/api/devices/UPB-DEMO-001/gps?limit=500'
```

查看实时 SSE：

```bash
curl -N -b /tmp/upboard_cookie.txt \
  http://127.0.0.1:18080/api/stream
```

## 8. 数据库查询

进入 PostgreSQL 查询最近 telemetry：

```bash
cd /opt/upboard-iot
set -a
. ./.env
set +a

docker compose exec -T postgres psql \
  -U "$POSTGRES_USER" -d "$POSTGRES_DB" \
  -c "select sn, seq, received_at, rssi, latitude, longitude, bat24_v, v12_v from telemetry order by received_at desc limit 10;"
```

查询某个设备的 GPS 轨迹：

```bash
docker compose exec -T postgres psql \
  -U "$POSTGRES_USER" -d "$POSTGRES_DB" \
  -c "select received_at, latitude, longitude from telemetry where sn='UPB-DEMO-001' and latitude is not null and longitude is not null order by received_at desc limit 100;"
```

## 9. 常见问题

### 后台打不开

检查容器和端口：

```bash
ssh hk-newapi
cd /opt/upboard-iot
docker compose ps
curl -fsS http://127.0.0.1:18080/healthz
```

### 设备列表没有新增

先确认 broker 收到连接，再确认数据库是否入库：

```bash
docker compose logs --since=5m mqtt web | tail -200
docker compose exec -T postgres psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" \
  -c "select sn, seq, received_at from telemetry order by received_at desc limit 10;"
```

### EC801E 返回 `ERROR`

优先检查：

- `QMTPUBEX` 的长度是否等于 JSON 字节数
- topic 是否是 `upboard/{sn}/telemetry`
- MQTT 用户名和密码是否与服务器 `.env` 一致
- 是否已收到 `+QMTOPEN: 0,0` 和 `+QMTCONN: 0,0,0`
- JSON 后是否误加了 Ctrl-Z 或额外回车换行

### 前端不实时刷新

检查 SSE：

```bash
curl -N -b /tmp/upboard_cookie.txt http://127.0.0.1:18080/api/stream
```

正常连接会先返回：

```text
event: hello
data: {}
```

之后每次 MQTT 入库应出现：

```text
event: telemetry
data: {...}
```
