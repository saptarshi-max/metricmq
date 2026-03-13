# MetricMQ Developer Reference {#mainpage}

**MetricMQ** is a lightweight C++20 message broker designed for IoT and edge deployments.
It speaks both RESP (Redis-compatible) and a compact binary protocol, supports Ed25519-signed
messages, persists everything to LMDB, and exposes Prometheus metrics over HTTP.

---

## Quick Links

| What you need | Where to look |
|---|---|
| Wire protocol byte layout | metricmq::BinaryProtocol |
| Publish a message | metricmq::BinaryPublisher |
| Subscribe with exactly-once | metricmq::BinarySubscriber |
| Ed25519 sign & verify | metricmq::crypto |
| Manage trusted device keys | metricmq::crypto::TrustedKeyStore |
| Message persistence | metricmq::storage::LmdbStorage |
| Prometheus metrics | metricmq::Metrics |
| Structured logging | metricmq::Logger |

---

## Architecture at a Glance

```
TCP :6379 ──► Broker::run() ──► accept() loop
                    │
              ┌─────┼──────── (detached threads) ──────────────┐
              │                                                  │
         Session (RESP)                            Session (Binary)
              │                                         │
         RespParser                          BinaryProtocol::parse()
              │                                         │
              └────────────── Broker (mutex) ────────────┘
                                  │
                           LmdbStorage (1 GB)
HTTP :9091 ──► MetricsServer (Poco) ──► Metrics::exportPrometheus()
```

---

## Further Reading

- [Technical Reference](../TECHNICAL.md) — Architecture, API, wire format, defensive practices
- [Testing Guide](../TESTING.md) — How to run every test and what each one verifies
