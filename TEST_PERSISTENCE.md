# Persistence Test Instructions

## Manual Test Steps

Since automated testing requires multiple terminals, here's how to manually verify persistence:

### Step 1: Start the Broker
```powershell
cd C:\Users\Sapta\Documents\Projects\MetricMQ\build\Release
.\metricmq-broker.exe
```
**Expected output**: `Broker listening on port 6379`

### Step 2: Open NEW Terminal - Publish Messages
```powershell
cd C:\Users\Sapta\Documents\Projects\MetricMQ\build\Release
.\pub_only.exe
```
**Expected**: Successfully publishes 10 messages to "chat" topic

### Step 3: Open NEW Terminal - Subscribe (Before Killing Broker)
```powershell
cd C:\Users\Sapta\Documents\Projects\MetricMQ\build\Release
.\sub_only.exe
```
**Expected**: Receives 10 messages (from persistence replay)

### Step 4: Kill Broker
In Terminal 1 (broker), press `Ctrl+C`

### Step 5: Verify Database Was Created
```powershell
dir C:\Users\Sapta\Documents\Projects\MetricMQ\metricmq.db
```
**Expected**: File exists (persistence database)

### Step 6: Restart Broker
```powershell
cd C:\Users\Sapta\Documents\Projects\MetricMQ\build\Release
.\metricmq-broker.exe
```

### Step 7: Subscribe Again (NEW Terminal)
```powershell
cd C:\Users\Sapta\Documents\Projects\MetricMQ\build\Release
.\sub_only.exe
```
**Expected**: ✅ **Receives same 10 messages from persistence!**

This proves messages survived the broker restart.

## Quick Automated Test (if broker already running)

If you have the broker running in one terminal, this single command tests everything:

```powershell
# Publish 5 messages
.\pub_only.exe

# Subscribe (will receive replayed messages)
.\sub_only.exe
```

Both should work if broker is active on port 6379.
