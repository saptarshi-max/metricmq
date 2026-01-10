# Exactly-Once Semantics Explained

## What Problem Does It Solve?

### The Duplicate Message Problem

In distributed systems, messages can be delivered **more than once** due to:

1. **Network retries**: Publisher doesn't receive ACK, resends same message
2. **Broker crashes**: Message persisted but not marked as delivered
3. **Client reconnects**: Subscriber replays messages it already processed

**Example Problem**:
```
Banking App:
1. User clicks "Transfer $100" button
2. Message sent: "TRANSFER $100 from Alice to Bob"
3. Network glitch → timeout
4. App retries → same message sent AGAIN
5. Bob receives $200 instead of $100! ❌
```

### The Lost Message Problem

Messages can also be **never delivered**:

1. **Broker crash**: Message received but not persisted
2. **Network partition**: Message lost in transit
3. **Client disconnects**: Message sent but never acknowledged

**Example Problem**:
```
IoT Sensor:
1. Temperature reaches critical 105°C
2. Sends alert: "TEMP_CRITICAL 105"
3. Broker crashes before persisting
4. Alert never reaches monitoring system ❌
5. Equipment overheats
```

---

## What is Exactly-Once Semantics?

**Exactly-Once** guarantees that:
- ✅ **No duplicates**: Each message processed exactly ONE time
- ✅ **No losses**: Every message is delivered at least once
- ✅ **Idempotent**: Replaying messages doesn't cause side effects

### Three Delivery Guarantees

| Mode | Description | Use Case | Complexity |
|------|-------------|----------|------------|
| **At-Most-Once** | Send once, don't retry. May lose messages. | Sensor data (ok to drop) | Easy |
| **At-Least-Once** | Retry until ACK. May duplicate. | Logging (duplicates ok) | Medium |
| **Exactly-Once** | Retry + Deduplication. No loss/duplicates. | Banking, payments | Hard |

**Current MetricMQ**: At-Least-Once (with persistence, messages replay)
**Goal**: Add Exactly-Once (deduplicate replayed messages)

---

## How Exactly-Once Works

### Core Components

#### 1. Sequence IDs (Already in MetricMQ!)
Every message gets a unique, monotonically increasing ID:
```
msg:1 → "sensors/temp: 25.5°C"
msg:2 → "sensors/temp: 25.6°C"
msg:3 → "alerts/critical: HIGH_TEMP"
msg:4 → "sensors/temp: 25.7°C"
```

#### 2. Client-Side Tracking (TODO)
Each subscriber tracks the last sequence ID it processed **per topic**:

```cpp
struct SubscriberState {
    std::map<std::string, uint64_t> last_seq;
    // Example:
    // "sensors/temp" → 4
    // "alerts/critical" → 3
};
```

#### 3. Deduplication Logic (TODO)
Before processing a message, check if already seen:

```cpp
void onMessage(uint64_t seq, const std::string& topic, const std::string& payload) {
    if (seq <= state.last_seq[topic]) {
        // Already processed this message, skip!
        return;
    }
    
    // Process message
    processMessage(topic, payload);
    
    // Update state
    state.last_seq[topic] = seq;
}
```

#### 4. ACK Handling (TODO)
Publisher waits for acknowledgment, retries if timeout:

```cpp
void Publisher::sendWithACK(const std::string& topic, const std::string& payload) {
    uint64_t seq = broker.publish(topic, payload);
    
    // Wait for ACK
    auto ack = waitForAck(seq, timeout=5000ms);
    
    if (!ack.received) {
        // Retry
        broker.publish(topic, payload, seq);  // Same sequence ID
    }
}
```

---

## Example: Exactly-Once in Action

### Scenario: Payment Processing

**Without Exactly-Once** (Current MetricMQ):
```
1. Publisher: PUBLISH "payments/transfer" → "Alice→Bob $100" (seq:1)
2. Broker: Persists msg:1, sends to subscriber
3. Subscriber: Processes → Bob gets $100 ✅
4. Network glitch → ACK lost
5. Publisher: Timeout, retry → PUBLISH same message (seq:2)
6. Broker: Persists msg:2 (duplicate!), sends to subscriber
7. Subscriber: Processes AGAIN → Bob gets $200 total ❌ DUPLICATE!
```

**With Exactly-Once** (After Implementation):
```
1. Publisher: PUBLISH "payments/transfer" → "Alice→Bob $100" (seq:1)
2. Broker: Persists msg:1, sends to subscriber
3. Subscriber: 
   - Check: seq:1 > last_seq[payments/transfer]:0 ✅ Process
   - Executes: Bob gets $100
   - Updates: last_seq[payments/transfer] = 1
   - Sends: ACK(seq:1)
4. Network glitch → ACK lost
5. Publisher: Timeout, retry → PUBLISH same message (seq:1 again)
6. Broker: Detects duplicate, sends ACK immediately
7. Subscriber: 
   - Check: seq:1 <= last_seq[payments/transfer]:1 ✅ SKIP
   - No processing, just ACK
8. Result: Bob has $100 total ✅ CORRECT!
```

---

## Implementation Plan for MetricMQ

### Current State
✅ **Sequence IDs**: LMDB auto-increments on each publish
✅ **Persistence**: Messages stored with sequence numbers
✅ **Replay**: New subscribers get all historical messages

### What's Missing

#### 1. Broker-Side Changes
```cpp
// In Broker class
class Broker {
private:
    // Track pending ACKs for retries
    std::map<uint64_t, PendingMessage> pending_acks_;
    
public:
    // Publish with sequence ID returned
    uint64_t publish(const std::string& topic, const std::string& payload);
    
    // Handle ACK from subscriber
    void handle_ack(uint64_t seq);
    
    // Retry unacknowledged messages
    void retry_unacked(std::chrono::milliseconds timeout);
};
```

#### 2. Client-Side Changes
```cpp
// In Subscriber class
class Subscriber {
private:
    // Track last processed sequence per topic
    std::map<std::string, uint64_t> last_seq_;
    
public:
    void subscribe(const std::string& topic, 
                   std::function<void(const std::string&, const std::string&)> callback) {
        // ... existing code
        
        // Add deduplication
        auto enhanced_callback = [this, callback](uint64_t seq, const std::string& topic, 
                                                   const std::string& payload) {
            if (seq <= last_seq_[topic]) {
                // Duplicate, skip processing
                send_ack(seq);
                return;
            }
            
            // Process message
            callback(topic, payload);
            
            // Update state
            last_seq_[topic] = seq;
            
            // Send ACK
            send_ack(seq);
        };
    }
};
```

#### 3. Protocol Changes
Add ACK command to RESP protocol:
```
Current:
  SUBSCRIBE topic
  PUBLISH topic payload
  MESSAGE topic payload

New:
  ACK sequence_id
```

Add ACK to Binary Protocol:
```cpp
enum BinaryCommand {
    CMD_SUBSCRIBE = 0x01,
    CMD_PUBLISH = 0x02,
    CMD_MESSAGE = 0x03,
    CMD_ACK = 0x04,        // ← NEW
    // ...
};
```

---

## Benefits of Exactly-Once

### For Applications
✅ **Banking**: No duplicate transactions
✅ **E-commerce**: No double orders
✅ **IoT**: Accurate sensor readings
✅ **Logging**: Precise event counts

### For Reliability
✅ **Crash recovery**: Safe to replay after broker restart
✅ **Network issues**: Automatic retry without duplicates
✅ **Idempotent**: Can re-run processes safely

### For Compliance
✅ **Financial regulations**: Audit trails with no gaps/duplicates
✅ **Healthcare**: Patient data integrity (HIPAA)
✅ **Industrial**: Safety-critical systems (ISO 26262)

---

## Performance Impact

### Without Exactly-Once (Current)
```
Throughput: 2.1M msg/sec
Latency:    ~1ms per message
Memory:     O(n) for persistence
```

### With Exactly-Once (Estimated)
```
Throughput: 1.8M msg/sec (-15% for ACK handling)
Latency:    ~2ms per message (+1ms for ACK round-trip)
Memory:     O(n) + O(t) where t = number of topics tracked
```

**Trade-off**: Slightly slower, but guaranteed correctness.

---

## When Do You Need Exactly-Once?

### ✅ Use Exactly-Once When:
- Financial transactions (payments, transfers)
- Order processing (e-commerce)
- Database replication
- Event sourcing
- Audit logs
- Safety-critical systems

### ❌ Don't Need Exactly-Once When:
- Real-time metrics (ok to drop/duplicate)
- Sensor data aggregation (averaging smooths duplicates)
- Chat messages (user can see duplicates)
- Log forwarding (duplicates filtered downstream)

---

## Testing Exactly-Once

### Test Scenario 1: Duplicate Detection
```bash
1. Start broker
2. Publish: "payment: $100" (seq:1)
3. Subscriber receives, processes, ACKs
4. Manually replay same message (seq:1)
5. Subscriber should SKIP processing ✅
```

### Test Scenario 2: Lost ACK Retry
```bash
1. Start broker
2. Publish: "order: item123" (seq:1)
3. Subscriber receives, processes
4. Drop ACK packet (simulate network loss)
5. Broker retries after timeout
6. Subscriber receives again (seq:1)
7. Subscriber detects duplicate, skips, sends ACK ✅
```

### Test Scenario 3: Crash Recovery
```bash
1. Start broker
2. Publish: "alert: fire" (seq:1)
3. Kill broker BEFORE subscriber ACKs
4. Restart broker
5. Subscriber reconnects, replays seq:1
6. Subscriber checks: seq:1 already processed? 
   - If YES → skip
   - If NO → process and ACK
```

---

## Implementation Effort

### Estimated Time: 2-3 hours

**Breakdown**:
1. Add ACK command to protocols (30 min)
2. Implement broker ACK handling (1 hour)
3. Add client-side deduplication (1 hour)
4. Testing and validation (30 min)

**Files to Modify**:
- `src/broker.hpp/cpp` - ACK handling, pending message tracking
- `src/session.hpp/cpp` - Parse ACK commands
- `src/pubsub.cpp` - Client-side last_seq tracking
- `src/binary_pubsub.cpp` - Binary ACK support
- `src/resp_parser.cpp` - Add ACK to RESP protocol

---

## Real-World Comparison

### Kafka (Exactly-Once)
Uses **idempotent producer** + **transactional API**:
- Producer assigns sequence numbers
- Broker deduplicates based on producer ID + sequence
- Similar to our approach, but more complex (multi-partition)

### RabbitMQ (Exactly-Once)
Uses **publisher confirms** + **consumer ACKs**:
- Publisher waits for broker confirmation
- Consumer must ACK before message deleted
- We're implementing the same pattern!

### AWS SQS (Exactly-Once)
Uses **deduplication ID** + **visibility timeout**:
- Client provides dedup ID (like our sequence ID)
- Messages with same ID within 5min window are dropped
- Our LMDB persistence is permanent (better for long replays)

---

## Summary

**Exactly-Once Semantics** is the final piece for production-grade reliability:

| Current | After Exactly-Once |
|---------|-------------------|
| Messages replayed on reconnect | ✅ Same |
| Duplicates possible | ✅ Prevented |
| No retry mechanism | ✅ Auto-retry with ACK |
| Manual state tracking | ✅ Automatic deduplication |

**Bottom Line**: 
- Without it: Fast but may duplicate
- With it: Slightly slower but **guaranteed correct**

For critical applications (banking, payments, safety), this is **non-negotiable**.

---

**Next Step**: Implement exactly-once delivery semantics.
