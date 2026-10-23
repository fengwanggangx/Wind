# Simulated counter

## Boundary and API

`trading_counter` is a standalone C++20 library: it does not link Wind, Protobuf, Qt or any database singleton. `TradeTypes.h` is the environment-neutral trading contract. `ITradingGateway::Request` returns queue admission only; the reply and events may arrive before the caller returns. The caller must route by account/client/order IDs, never assume synchronous order acceptance. The core is single-writer; `CInProcessGateway` owns its worker, cache, market epoch and shutdown join.

`CTradeService` is the Wind routing/cache boundary. Each account has an injected `ITradingGateway`; it does not hold a concrete simulated service. The Wind-specific composition root is `WindCounterHost.cpp`. It adapts the DB pool, imports rules and independently subscribes to depth data. A future network adapter implements the same interface and maps broker order IDs internally. No real broker implementation or separate server is included.

## Storage and startup

The store creates `sim_counter_lease`, `sim_counter_checkpoint`, `sim_counter_accounts`, `sim_counter_positions`, `sim_counter_orders`, `sim_counter_trades`, `sim_counter_rules`, `sim_counter_calendar`, `sim_counter_daycut` and `sim_counter_events` in MySQL/InnoDB. The checkpoint is authoritative; per-entity serialized projections and event rows are updated in its transaction. Storage uses an injected SQL-session factory. Payloads use a versioned, quoted text codec, passed as binary SQL literals; no credentials enter checkpoints.

The single-writer lease uses server time, a 15-second expiry, row locks and monotonically increasing fencing tokens. The worker renews once per second, and commits verify owner/token/expiry before and after writes. A storage/lease failure marks the cache unready and stops accepting trades; restart/resync is required. An uncertain commit is reported as UnknownResult, not a safe-to-retry rejection. Reuse client_order_id when reconciling.

Legacy account IDs and initial balances are imported once, with a persisted migration flag. Existing balance/position/order history never existed on disk and is not fabricated. Subsequent starts retain the ledger, cancel expired day orders and complete missed daycuts, reset transport connection state and wait for fresh depth. Default accounts reconnect through the same async interface. Wind stores routing in table_trading_account.adapter_key/broker/site/external_account. Existing rows default to simulated/wind_simulated/default/SIM-<id>. Passwords are never stored; the simulator login opens an already authenticated Wind user's configured simulation route, not a real brokerage login.

## Rules and matching

Set `[Trade] rules_path=/absolute/path/counter-rules.json` in Wind's system INI. `rules.example.json` is an intentionally historical synthetic fixture, not exchange rules or current fees. Without a rule file the counter can serve accounts and queries but rejects orders lacking rules. Every calendar/fee/lot/tick/band field is required when importing JSON. Existing day/security rules are immutable, including version and corporate-action flag; use the next trading day's configuration for changes. Restart to import new configuration. Back up all sim_counter tables together.

Only configured MAIN/CHINEXT/STAR ordinary-stock rules on SSE/SZSE are accepted. The trusted import must classify securities correctly. Limit orders operate only during configured continuous sessions. Board-specific quantities, fees and price scales are explicit configuration, not inferred from symbol prefixes. Prices carry a decimal scale, quantities are shares, money is integer cents. Fees use cumulative order turnover: ceiling commission with an order-level minimum plus ceiling transfer fee and sell tax. Partial fills charge only the incremental cumulative fee. Sub-cent turnover and overflow are rejected rather than silently rounded. No market orders, auction simulation, ETF/bond/margin trading or corporate-action accounting.

Buy orders freeze worst-case limit notional plus fees; sells freeze available shares. Fills, fees, order/trade records, positions, funds and events commit atomically. Bought stock is not sellable in the same configured trading day. Day close expires remaining day orders; the next configured trading day's opening releases today's stock. No trading is allowed outside sessions. Corporate-action flagged securities reject orders/matching until an operator supplies the next valid configuration; automatic adjustments are not implemented.

Books are shared by all accounts. Orders consume ask/bid levels using price priority and counter order-ID FIFO. Each book sequence replaces, rather than accumulates, remaining liquidity. Duplicate/out-of-order/stale/future timestamps do not replenish liquidity. New orders can use unconsumed current liquidity. There is no fallback to last-price matching. Market loss clears cached books; queued books from a previous connection epoch are discarded. This is snapshot approximation, not real exchange queue reconstruction.

## Wire contract

All three projects share these additional request types and payloads:

- 49 TRADE_ORDER_SUBMIT: ExtraData account_id, client_order_id, security (e.g.600000.SSE), side=buy/sell, order_type=limit, price, price_scale, quantity. Reply TradeOrderList.
- 50 TRADE_ORDER_CANCEL: account_id, order_id. Reply TradeOrderList. A filled/otherwise terminal order is unchanged; inspect its final state.
- 51 TRADE_POSITION_QUERY / 52 TRADE_ORDER_QUERY / 53 TRADE_TRADE_QUERY: account_id, optional security; reply TradePositionList / TradeOrderList / TradeList.
- 54 TRADE_ACCOUNT_STATE_PUSH / 55 TRADE_ORDER_PUSH / 56 TRADE_TRADE_PUSH / 57 TRADE_ACCOUNT_INFO_PUSH / 58 TRADE_POSITION_PUSH: id=0, ExtraData sequence/request_id/account_id and the matching structured row/list payload.
- 59 TRADE_SYNC_QUERY: account_id, optional after_sequence; TradeSyncResult contains a consistent full snapshot, watermark and persisted events after that cursor. The snapshot already includes these events; apply the snapshot and only later events above its watermark, or use the event list against an older cache. Do not apply both to the same state.
- 60 TRADE_CAPABILITIES_QUERY: account_id; ExtraData.operations is a JSON integer array of supported trading::Operation ordinals + 1. Query before enabling operations in clients.

Replies preserve Type/id and use error_code/error_message. Trading Error codes: 0 None, 1 Rejected, 2 Unavailable, 3 Unsupported, 4 Timeout, 5 UnknownResult, 6 Conflict, 7 NotFound. Order side: 0 Buy, 1 Sell. Order state: 0 Accepted, 1 PartiallyFilled, 2 Filled, 3 Cancelled, 4 Expired, 5 Rejected. Event kind: 0 Session, 1 Order, 2 Trade, 3 Funds, 4 Position. Account INFO_QUERY retains account_ids JSON-array filtering across account routes. Other queries are account-scoped. The current account pool remains shared by all authenticated Wind users.

## Tests and build

```
cmake -S Wind/Wind/counter -B /tmp/counter-build
cmake --build /tmp/counter-build -j4
ctest --test-dir /tmp/counter-build --output-on-failure
```

Core tests use a fake clock, fixed rules/books and an injected store: multi-level fills, shared liquidity, duplicate/out-of-order/stale data, exact fees, lot/band/funds checks, T+1, cancellation, daycut, codec corruption, rollback, restart, replay and lease loss. The same async contract exercises an independent deferred fake broker and the in-process simulator. MySQL tests run only with COUNTER_MYSQL_TESTS=ON, explicit MYSQL_INCLUDE_DIR/MYSQL_CLIENT, and COUNTER_TEST_SOCKET pointing at a disposable local test server. They create/drop only a PID-named test database and verify real InnoDB rollback, recovery and fencing.

## Operational limits

Validation on 2026-10-24: standalone core and real MySQL tests passed (2/2 CTest targets); Mary's protocol tests passed (10 tests) and its Debug x64 build passed. Modified Wind integration sources and HQMarket protocol sources passed syntax compilation. A live Mary/Wind/HQMarket end-to-end run and full Wind/HQMarket linking were not verified. Address/undefined-behavior sanitizer execution was unavailable because this environment lacks the required sanitizer runtime libraries.

This first implementation favors rollback correctness: commands copy a state transaction and persist a full checkpoint plus entity projections; histories are retained in memory. Large histories need incremental storage/checkpoint compaction before high-throughput deployment. Queries/synchronization currently return full account history and are bounded by the existing network frame limit, so operationally keep test accounts small. The separate network service, broker SDK adapter, authorization isolation and UI trading screens are out of scope. Existing Mary can deserialize the new protocol but has no new order-entry screens.
