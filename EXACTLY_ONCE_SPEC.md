# Exactly-Once Delivery Specification

**Status:** Design Document  
**Date:** January 7, 2026  
**Priority:** P0 (Critical for Production)

---

## 🎯 Goal

Implement **exactly-once delivery semantics** to guarantee:
- ✅ No duplicate message processing
- ✅ No lost messages
- ✅ Reliable replay after disconnection
- ✅ Client-side deduplication

---

## 📋 Requirements

### Functional Requirements
1. **ACK Mechanism** - Clients acknowledge successful message processing
2. **Sequence Tracking** - Broker tracks which messages each client has ACK'd
3. **Smart Replay** - Only replay messages NOT yet ACK'd by client
4. **Client Identity** - Persistent client IDs survive reconnection
5. **Cleanup** - Remove ACK'd messages when all subscribers confirm

### Non-Functional Requirements
- **Performance:** ACK overhead < 5% throughput impact
- **Storage:** ACK log should be memory-efficient
- **Compatibility:** Works with existing RESP + Binary protocols
- **Reliability:** ACK state persists across broker restarts

---

## 🏗️ Architecture

### High-Level Flow

```
┌─────────────┐
│ Publisher   │
└──────┬──────┘
       │ PUBLISH("topic", "payload")
       ↓
┌─────────────────────────────────┐
│ Broker                          │
│ 1. Assign sequence ID: seq=42   │
│ 2. Persist: LMDB.save(42, msg)  │
│ 3. Broadcast to subscribers     │
└─────────┬───────────────────────┘
          │
          ↓
┌─────────────────────────────────┐
│ Subscriber (client_id="sub-1")  │
│ 1. Receive: (seq=42, payload)   │
│ 2. Process: callback(payload)   │
│ 3. Send ACK(42) to broker       │
└─────────┬───────────────────────┘
          │ ACK(seq=42)
          ↓
┌─────────────────────────────────┐
│ Broker                          │
│ 1. Record: ack_log["sub-1"][42] │
│ 2. Check: all subscribers ACK?  │
│ 3. Cleanup: LMDB.delete(42)     │
└─────────────────────────────────┘
```

### Reconnection Scenario

```
┌─────────────┐
│ Subscriber  │ Connects: client_id="sub-1"
└──────┬──────┘
       │ SUBSCRIBE("topic")
       ↓
┌─────────────────────────────────┐
│ Broker                          │
│ 1. Check last_ack["sub-1"] = 39 │
│ 2. Query LMDB: seq > 39         │
│ 3. Replay: seq=40,41,42,...     │
│ 4. Resume live messages         │
└─────────────────────────────────┘
```

---

## 🔧 Implementation Details

### 1. Protocol Extension (Binary Protocol)

#### New Opcode: ACK
```cpp
enum class BinaryOpCode : uint8_t {
    PUBLISH = 0x01,
    SUBSCRIBE = 0x02,
    PUSH = 0x03,
    PULL = 0x04,
    MESSAGE = 0x05,
    ACK = 0x06,        // ← NEW
};
```

#### ACK Frame Format
```
┌──────────┬─────────────┐
│ OpCode   │ Sequence ID │
│ 1 byte   │ 8 bytes     │
└──────────┴─────────────┘

Total: 9 bytes

Example:
  [0x06] [0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x2A]
   ACK    seq=42
```

#### MESSAGE Frame Update (Add Sequence ID)
```
Current MESSAGE format:
┌──────────┬────────┬───────┬─────────┐
│ OpCode   │ Length │ Topic │ Payload │
│ 1 byte   │ 4 bytes│ Var   │ Var     │
└──────────┴────────┴───────┴─────────┘

NEW MESSAGE format:
┌──────────┬─────────────┬────────┬───────┬─────────┐
│ OpCode   │ Sequence ID │ Length │ Topic │ Payload │
│ 1 byte   │ 8 bytes     │ 4 bytes│ Var   │ Var     │
└──────────┴─────────────┴────────┴───────┴─────────┘

Total overhead: +8 bytes per message
```

### 2. Broker Data Structures

#### ACK Log (In-Memory)
```cpp
class Broker {
private:
    // Track which sequences each client has ACK'd
    std::unordered_map<std::string, std::set<uint64_t>> ack_log_;
    std::mutex ack_mutex_;
    
    // Track last ACK'd sequence per client (optimization)
    std::unordered_map<std::string, uint64_t> last_ack_seq_;
    
    // Track subscribers per topic (existing)
    std::unordered_map<std::string, std::set<Session*>> topic_subscribers_;
};
```

#### Persistent ACK State (LMDB)
```
Key Format:
  "ack:<client_id>:<seq>" → timestamp

Examples:
  "ack:sub-1:42" → "2026-01-07T10:30:00Z"
  "ack:sub-1:43" → "2026-01-07T10:30:01Z"
  "ack:sub-2:42" → "2026-01-07T10:30:02Z"
  
Query:
  Get all ACKs for client: scan "ack:sub-1:*"
  Check if message ACK'd: exists("ack:sub-1:42")
```

### 3. Broker API Changes

#### New Method: ack()
```cpp
class Broker {
public:
    /**
     * Record that a client has ACK'd a message
     * @param client_id Persistent client identifier
     * @param sequence Message sequence ID
     */
    void ack(const std::string& client_id, uint64_t sequence);
    
    /**
     * Get the highest ACK'd sequence for a client
     * @param client_id Client identifier
     * @return Last ACK'd sequence (0 if none)
     */
    uint64_t getLastAck(const std::string& client_id) const;
    
    /**
     * Replay messages since last ACK
     * @param session Client session
     * @param topic Topic to replay
     * @param client_id Persistent client identifier
     */
    void replayMessagesSinceLastAck(
        Session* session, 
        const std::string& topic,
        const std::string& client_id
    );
};
```

#### Updated Method: replayMessages()
```cpp
// BEFORE:
void replayMessages(Session* session, const std::string& topic, uint64_t since_seq);

// AFTER:
void replayMessages(
    Session* session, 
    const std::string& topic, 
    uint64_t since_seq,
    const std::string& client_id = ""  // If provided, skip ACK'd messages
);
```

### 4. Session Changes

#### Client ID Handling
```cpp
class Session {
private:
    std::string client_id_;  // Persistent client identifier
    
public:
    // NEW: Client sends ID on SUBSCRIBE
    void handleSubscribe(const std::string& topic, const std::string& client_id);
    
    // NEW: Handle incoming ACK frames
    void handleAck(uint64_t sequence);
};
```

#### SUBSCRIBE Frame Update
```
Current SUBSCRIBE:
  SUBSCRIBE <topic>

NEW SUBSCRIBE (with client ID):
  Binary format:
  ┌──────────┬────────┬──────────────┬───────┐
  │ OpCode   │ Length │ Client ID    │ Topic │
  │ 1 byte   │ 4 bytes│ Var (null)   │ Var   │
  └──────────┴────────┴──────────────┴───────┘
  
  Example:
    [0x02][0x00 0x00 0x00 0x14]["sub-1"][sensors/temp]
     SUB    Length=20          ID       Topic
```

### 5. Client Library Changes

#### BinarySubscriber Update
```cpp
class BinarySubscriber {
private:
    std::string client_id_;  // Persistent ID
    
    // Callback wrapper: auto-ACK after processing
    void messageHandler(uint64_t seq, const std::string& topic, const std::string& payload) {
        // 1. Call user callback
        user_callback_(topic, payload);
        
        // 2. Send ACK to broker
        sendAck(seq);
    }
    
    void sendAck(uint64_t sequence);
    
public:
    // Constructor with client ID
    BinarySubscriber(const std::string& client_id);
    
    // Subscribe with client ID
    void subscribe(const std::string& topic);
};
```

---

## 🧪 Test Scenarios

### Test 1: Basic ACK Flow
```cpp
// 1. Start broker
Broker broker;
broker.start();

// 2. Subscriber connects
BinarySubscriber sub("test-sub-1");
sub.connect("localhost", 6379);
int received_count = 0;
sub.subscribe("test/topic", [&](const Message& msg) {
    received_count++;
    // ACK sent automatically
});

// 3. Publish 10 messages
BinaryPublisher pub;
pub.connect("localhost", 6379);
for (int i = 0; i < 10; i++) {
    pub.publish("test/topic", "msg_" + std::to_string(i));
}

// 4. Wait for delivery
sleep(1);

// 5. Verify
assert(received_count == 10);
assert(broker.getLastAck("test-sub-1") == 10);  // All ACK'd
```

### Test 2: Replay Only Unacked Messages
```cpp
// 1. Publish 100 messages
for (int i = 0; i < 100; i++) {
    pub.publish("test/topic", "msg_" + std::to_string(i));
}

// 2. Subscribe, receive 50, disconnect mid-stream
BinarySubscriber sub("test-sub-2");
sub.connect("localhost", 6379);
int received_count = 0;
sub.subscribe("test/topic", [&](const Message& msg) {
    received_count++;
    if (received_count == 50) {
        sub.disconnect();  // Disconnect before ACK'ing all
    }
});

sleep(2);  // Process 50 messages

// 3. Reconnect
sub.connect("localhost", 6379);
received_count = 0;  // Reset counter
sub.subscribe("test/topic", [&](const Message& msg) {
    received_count++;
});

sleep(2);

// 4. Verify: should receive ONLY 50 unacked messages
assert(received_count == 50);  // NOT 100 (no duplicates)
```

### Test 3: Multiple Subscribers
```cpp
// 1. Publish 10 messages
for (int i = 0; i < 10; i++) {
    pub.publish("test/topic", "msg_" + std::to_string(i));
}

// 2. Two subscribers
BinarySubscriber sub1("sub-1");
BinarySubscriber sub2("sub-2");

sub1.subscribe("test/topic");
sub2.subscribe("test/topic");

sleep(2);

// 3. Verify both received all messages
assert(broker.getLastAck("sub-1") == 10);
assert(broker.getLastAck("sub-2") == 10);

// 4. Publish more messages
for (int i = 0; i < 5; i++) {
    pub.publish("test/topic", "msg_" + std::to_string(i + 10));
}

sleep(2);

// 5. Verify incremental ACKs
assert(broker.getLastAck("sub-1") == 15);
assert(broker.getLastAck("sub-2") == 15);
```

### Test 4: Persistence After Broker Restart
```cpp
// 1. Publish 50 messages
for (int i = 0; i < 50; i++) {
    pub.publish("test/topic", "msg_" + std::to_string(i));
}

// 2. Subscribe and ACK first 25
BinarySubscriber sub("persistent-sub");
sub.subscribe("test/topic");
sleep(1);  // Process half
sub.disconnect();

// 3. Kill broker
broker.stop();

// 4. Restart broker
broker.start();

// 5. Reconnect subscriber
sub.connect("localhost", 6379);
int received_count = 0;
sub.subscribe("test/topic", [&](const Message& msg) {
    received_count++;
});

sleep(2);

// 6. Verify: receives ONLY unacked messages (26-50)
assert(received_count == 25);  // NOT 50 (ACK state persisted)
```

### Test 5: Cleanup After All ACKs
```cpp
// 1. Publish 1000 messages
for (int i = 0; i < 1000; i++) {
    pub.publish("test/topic", "msg_" + std::to_string(i));
}

// 2. Single subscriber ACKs all
BinarySubscriber sub("cleanup-test");
sub.subscribe("test/topic");
sleep(5);  // Process all

// 3. Check LMDB size
uint64_t db_size_before = broker.getDatabaseSize();

// 4. Trigger cleanup (all messages ACK'd by all subscribers)
broker.cleanupAckedMessages();

// 5. Verify database shrinks
uint64_t db_size_after = broker.getDatabaseSize();
assert(db_size_after < db_size_before);  // Messages deleted
```

---

## 🚀 Implementation Order

### Phase 1: Protocol (1 hour)
1. ✅ Add `ACK` opcode to `BinaryOpCode` enum
2. ✅ Add sequence ID to `MESSAGE` frame
3. ✅ Implement `encodeAck()` and `decodeAck()` in `binary_protocol.cpp`
4. ✅ Update `encodeMessage()` to include sequence ID

### Phase 2: Broker (1 hour)
1. ✅ Add `ack_log_` and `last_ack_seq_` data structures
2. ✅ Implement `Broker::ack()` method
3. ✅ Update `Broker::publish()` to assign sequence IDs
4. ✅ Update `Broker::replayMessages()` to skip ACK'd messages
5. ✅ Add `Broker::getLastAck()` helper

### Phase 3: Session (30 min)
1. ✅ Add `client_id_` member to `Session`
2. ✅ Update `handleSubscribe()` to extract client ID
3. ✅ Add `handleAck()` to process ACK frames
4. ✅ Update `handleMessage()` to include sequence in outgoing frames

### Phase 4: Client (30 min)
1. ✅ Add `client_id_` to `BinarySubscriber` constructor
2. ✅ Update `subscribe()` to send client ID
3. ✅ Add `sendAck()` method
4. ✅ Wrap user callback to auto-ACK after processing

### Phase 5: Persistence (30 min)
1. ✅ Add ACK state to LMDB (`ack:<client>:<seq>`)
2. ✅ Load ACK state on broker startup
3. ✅ Flush ACK state on shutdown

### Phase 6: Testing (1 hour)
1. ✅ Create `exactly_once_test.cpp`
2. ✅ Implement all 5 test scenarios
3. ✅ Add to CMakeLists.txt
4. ✅ Verify all tests pass

**Total Time Estimate:** 4.5 hours

---

## 📊 Performance Impact

### Before (No ACK):
```
Throughput: 1.8M msg/sec (binary protocol)
Latency:    <1ms P50
Frame size: 13 bytes + topic + payload
```

### After (With ACK):
```
Throughput: ~1.7M msg/sec (-5% overhead from ACK frames)
Latency:    <1.2ms P50 (+0.2ms for ACK processing)
Frame size: 21 bytes + topic + payload (+8 bytes for sequence)
ACK frame:  9 bytes per message

Network overhead:
  Before: 13 + topic + payload
  After:  21 + topic + payload + 9 (ACK)
  Total:  +17 bytes per message (~30% for 64-byte payloads)
```

### Memory Impact:
```
ACK Log (in-memory):
  Per ACK: 8 bytes (sequence) + 32 bytes (set overhead) = 40 bytes
  100K messages: 4 MB
  1M messages: 40 MB
  
Cleanup strategy:
  - Keep only unACK'd sequences
  - Periodic cleanup (every 10 seconds)
  - Delete messages ACK'd by ALL subscribers
```

---

## 🔒 Edge Cases

### Case 1: Subscriber Never ACKs
**Problem:** Client processes messages but never sends ACK (buggy client)

**Solution:** Add timeout + retry
```cpp
// Track message send time
struct PendingMessage {
    uint64_t sequence;
    std::chrono::time_point<std::chrono::steady_clock> sent_at;
};

// Retry after 30 seconds
if (now - pending.sent_at > 30s) {
    // Resend message (mark as "duplicate")
    session->send(pending.sequence, topic, payload, duplicate=true);
}
```

### Case 2: ACK Arrives After Disconnect
**Problem:** Client sends ACK, then immediately disconnects

**Solution:** Persist ACK state immediately
```cpp
void Broker::ack(const std::string& client_id, uint64_t sequence) {
    // 1. Update in-memory
    ack_log_[client_id].insert(sequence);
    
    // 2. Persist to LMDB (synchronous)
    persistence_->saveAck(client_id, sequence);
    
    // 3. Update optimization cache
    last_ack_seq_[client_id] = std::max(last_ack_seq_[client_id], sequence);
}
```

### Case 3: Out-of-Order ACKs
**Problem:** Network delivers ACK for seq=43 before ACK for seq=42

**Solution:** Use set (not sequential tracking)
```cpp
// WRONG (assumes sequential):
uint64_t last_ack = 42;

// CORRECT (handles gaps):
std::set<uint64_t> ack_log = {40, 42, 43, 45};  // Missing 41, 44
```

### Case 4: Client ID Collision
**Problem:** Two clients use same ID

**Solution:** Reject duplicate connection
```cpp
void Broker::subscribe(Session* session, const std::string& topic, const std::string& client_id) {
    if (active_clients_.count(client_id) > 0) {
        // Client already connected
        session->sendError("CLIENT_ID_IN_USE");
        session->disconnect();
        return;
    }
    
    active_clients_[client_id] = session;
    // ... rest of subscribe logic
}
```

---

## 🎓 Alternatives Considered

### Alternative 1: Server-Side Deduplication
**Idea:** Broker tracks duplicates instead of client sending ACKs

**Pros:**
- Simpler protocol (no ACK frame)
- Less network traffic

**Cons:**
- Broker doesn't know if client successfully processed message
- Can't detect failed processing
- No replay control

**Decision:** Rejected - need client confirmation

### Alternative 2: Offset-Based (Kafka Style)
**Idea:** Client commits offset instead of individual ACKs

**Pros:**
- Less storage (one offset per client)
- Simple cleanup

**Cons:**
- Forces sequential processing
- Can't skip failed messages
- Harder to implement idempotency

**Decision:** Rejected - too restrictive for pub/sub

### Alternative 3: Bloom Filter for ACKs
**Idea:** Use probabilistic data structure

**Pros:**
- Constant memory usage
- Fast lookups

**Cons:**
- False positives possible
- Can't persist reliably
- Overkill for our scale

**Decision:** Rejected - std::set is sufficient

---

## ✅ Success Criteria

**This implementation is successful when:**
1. ✅ All 5 test scenarios pass
2. ✅ Throughput remains > 1.5M msg/sec
3. ✅ No duplicate deliveries in stress test (1M messages)
4. ✅ ACK state persists across broker restarts
5. ✅ Memory usage stays < 100 MB for 1M messages
6. ✅ Documentation updated (PERSISTENCE.md, EXACTLY_ONCE_EXPLAINED.md)

---

**Next Steps:**
1. Review this spec
2. Implement Phase 1 (Protocol changes)
3. Test incrementally
4. Move to Phase 2 (Broker)

