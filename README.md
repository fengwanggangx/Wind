## HQMarket 行情客户端

`CBootLoader` 创建并运行 `CTcpClient`，作为 Wind 到 HQMarket 的 TCP 桥梁。
连接地址通过 `HQMARKET_HOST`、`HQMARKET_PORT` 配置，默认使用
`127.0.0.1:9901`。
Wind 自身的 TCP、HTTP 监听端口通过 `WIND_TCP_PORT`、`WIND_HTTP_PORT`
配置，默认使用 `9801`、`9802`，避免与本机 HQMarket 的 `9901/9902` 冲突。

`Wind/hqmarket/CHQMarket` 使用传入的 `CTcpClient` 管理认证、协议收发和响应回调，
不直接管理 libevent 对象或网络线程；业务请求由
`CHQRequest` 独立组装，再交给 `CHQMarket::SendRequest()`。请求工厂包括
`Subscribe()`、`Unsubscribe()`、`QueryQuote()`、`QueryBars()` 和
`Heartbeat()`，返回类型均为 `CRequest*`。`CHQMarket::SendRequest()` 会接管并
释放传入的请求指针，调用方不要再次释放。

HQMarket 认证 Token 从 `Wind/ini/system.ini` 读取：

```ini
[HQMarket]
token=replace-with-your-token
```

订阅股票当日实时行情时，代码和交易所分开传入：

```cpp
hqMarket.SubscribeQuote("600519", market::Exchange::sse);
```

响应仍以 `CRequest` 回调：`cmd` 是 HQMarket 消息类型，`ret["request_id"]`
用于关联请求。`CRequest::GetData()` 返回运行期 `CData` 指针，其中 `type` 标识具体
行情结构，`payload` 是对应 Protobuf 的序列化内容；在线路上分别存放于
`ret["data_type"]` 和 `ret["data"]`。错误响应另含 `error_code` 和 `error_message`。
