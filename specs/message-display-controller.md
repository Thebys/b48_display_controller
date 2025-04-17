# Message Display Controller Algorithm Specification

## 1. Overview
This document describes the algorithm and state machine for the Message Display Controller component, focusing on message scheduling, prioritization, and the integration of ephemeral (temporary) messages.

## 2. Core Concepts

### 2.1 Message Types
- **Persistent Messages**: Stored in SQLite database, survive reboots
- **Ephemeral Messages**: Temporary, stored in RAM only, higher immediate priority

### 2.2 Display Cycles
- **Cycle 0**: Main content (line, tariff, intro, scroll message)
- **Cycle 6**: Transition content (next stop/message hint)

## 3. State Machine

```
                            ┌───────────────────────┐
                            │ EMERGENCY_INTERRUPT   │
                            │ (Bypass Transition)   │
                            └───────────┬───────────┘
                                        │
                                        ▼
┌─────────────────┐      ┌────────────────────┐      ┌─────────────────┐
│ TRANSITION_MODE │ ──→  │ MESSAGE_PREPARATION│ ──→  │ DISPLAY_MESSAGE │
│   (Cycle 6)     │      │                    │      │    (Cycle 0)    │
└─────────────────┘      └────────────────────┘      └─────────────────┘
        ↑                                                     │
        └─────────────────────────────────────────────────────┘
```

### 3.1 States
1. **TRANSITION_MODE**: Display Cycle 6 showing next message hint and time
2. **MESSAGE_PREPARATION**: Prepare data for next message
3. **DISPLAY_MESSAGE**: Display Cycle 0 showing current message content

## 4. Message Selection Algorithm

### 4.1 Message Queue Structure
```cpp
struct MessageEntry {
    bool isEphemeral;
    int messageId;          // -1 for ephemeral
    int priority;           // 0-100
    time_t expiryTime;      // When this message expires
    time_t lastDisplayTime; // Timestamp when this message was last selected for display
    // Message content fields...
};
```

### 4.2 Queue Management
- RAM-based collection (e.g., `std::vector`) holding both persistent (cached from DB) and ephemeral messages.
- No longer strictly a priority queue sorted once; selection logic dynamically evaluates messages.

### 4.3 Selection Logic
- The goal is dynamic message rotation, preventing starvation of low-priority messages and avoiding immediate repetition of high-priority ones. A strict fairness ratio is secondary to keeping the display dynamic and responsive. The message selection should feel somewhat unpredictable or "quirky" while still prioritizing important or fresh information.
- Uses a combination of priority and time since last display. Other potential approaches include weighted random selection or round-robin variations with added jitter to prevent monotony.
- **Consideration:** Explore algorithms that explicitly penalize repeatedly showing the same high-priority message if other messages are waiting.

```cpp
// Constants (tune as needed)
const int EMERGENCY_PRIORITY_THRESHOLD = 95; // Priority 95-100 reserved for urgent interrupting alerts
const int MIN_SECONDS_BETWEEN_REPEATS = 30; // Minimum time before showing the same message

Function selectNextMessage():
    currentTime = getCurrentTime()
    selectedMsg = null
    highestScore = -infinity

    // First, check for any *new* emergency messages
    for each ephemeralMsg in ephemeralMessages:
        if ephemeralMsg.priority >= EMERGENCY_PRIORITY_THRESHOLD and ephemeralMsg.lastDisplayTime == 0: // Check if never shown
             // Return immediately if a new emergency message is found
            ephemeralMsg.lastDisplayTime = currentTime 
            return ephemeralMsg

    // Evaluate all displayable messages (persistent cache + non-emergency ephemeral)
    candidateMessages = getAllMessages() // Combine persistent cache and non-emergency ephemeral

    for each msg in candidateMessages:
        // Skip expired messages
        if msg.expiryTime > 0 and msg.expiryTime <= currentTime:
            continue 
            
        // Skip messages shown too recently
        if (currentTime - msg.lastDisplayTime) < MIN_SECONDS_BETWEEN_REPEATS:
             continue

        // Calculate a score (simple example: higher priority and older messages get higher scores)
        // Adjust scoring based on desired behavior (e.g., add randomness, stronger time weighting)
        timeSinceLastDisplay = (msg.lastDisplayTime == 0) ? 99999 : (currentTime - msg.lastDisplayTime); // Prioritize never-shown
        score = msg.priority * 10 + timeSinceLastDisplay 

        if score > highestScore:
            highestScore = score
            selectedMsg = msg

    // If no suitable message found (e.g., all shown recently), maybe return a default/fallback?
    // For now, just return the best candidate, even if shown recently (fallback)
    if selectedMsg == null and !candidateMessages.isEmpty():
       // Fallback: Pick highest priority among available, ignoring MIN_SECONDS_BETWEEN_REPEATS
       // (Requires finding highest priority again, simplified here)
       selectedMsg = findHighestPriorityMessage(candidateMessages) 

    if selectedMsg != null:
         selectedMsg.lastDisplayTime = currentTime // Update last display time *after* selection

    return selectedMsg 
```

## 5. Fairness and Priority Weighting

### 5.1 Message Display Dynamics
- The selection logic (Section 4.3) aims for dynamic display rotation, making the display feel alive rather than strictly predictable.
- Higher priority messages are inherently favored by the scoring.
- The `MIN_SECONDS_BETWEEN_REPEATS` helps prevent high-priority messages from monopolizing the display *too* often, but the primary goal is dynamism, not strict fairness.
- Lower priority messages will eventually be shown as their `timeSinceLastDisplay` increases their score relative to recently shown higher priority messages.
- **Note:** The previous table with strict frequencies/gaps is removed, as the dynamic logic provides approximate weighting rather than guaranteed ratios. Tuning constants (`MIN_SECONDS_BETWEEN_REPEATS`, scoring formula, potential randomness factors) adjusts the behavior.

### 5.2 Display Time Calculation (Updated)
- The duration a message is shown in Cycle 0 depends primarily on the length of its scrolling content.
- **Formula (Initial):**
  `displayDuration = 20.0 + (0.3 * length(scrolling_message))` (in seconds)
- This provides a robust baseline duration (20 seconds) plus an additional 300 milliseconds for each character.
- **Tuning methodology:**
  1. Measure actual scroll rate across device samples (chars/second)
  2. Allow minimum 2x time for viewers to read message content
  3. Adjust baseline (20s) for very short messages to ensure minimum visibility
  4. Maximum duration should not exceed 180 seconds regardless of message length. This should cover 511 characters 1x.

## 6. Ephemeral Message Handling

### 6.1 Registration
```
Function registerEphemeralMessage(content, priority, displayCount, ttl):
    msg = new EphemeralMessage()
    msg.content = content
    msg.priority = priority
    msg.remainingDisplays = displayCount
    msg.expiryTime = currentTime + ttl
    msg.lastDisplayTime = 0 // Initialize last display time
    
    ephemeralMessages.add(msg)
    // No need to sort a central queue anymore, selection logic handles it
```

### 6.2 Lifecycle
- Ephemeral messages expire after:
  - Reaching their display count (e.g., show 2 times)
  - Exceeding their TTL (time-to-live)
- Emergency messages (priority >= `EMERGENCY_PRIORITY_THRESHOLD`) interrupt the normal display cycle

## 7. Display Flow Implementation

```cpp
// Constants
const int TRANSITION_DURATION = 4; // Seconds for Cycle 6 display
const int TIME_SYNC_INTERVAL = 60; // Seconds between time updates to display
time_t lastTimeSync = 0;

Function runDisplayCycle():
    While true:
        currentTime = getCurrentTime()
        
        // --- Emergency Message Handling ---
        emergencyMsg = checkForNewEmergencyMessages() // Modification of selectNextMessage logic for emergencies
        if emergencyMsg != null:
            prepareMessage(emergencyMsg) // Prepare commands for the emergency message
            sendCommandsForCycle0(emergencyMsg) // Send line, tariff, intro, scroll commands
            switchToCycle0() // Send xC0 command
            displayDuration = calculateDisplayDuration(emergencyMsg)
            wait(displayDuration)
            updateMessageDisplayStats(emergencyMsg) // Mark as shown, handle ephemeral lifecycle
            lastTimeSync = 0 // Ensure time sync happens after emergency
            continue // Restart loop immediately

        // --- Normal Message Flow ---
        msg = selectNextMessage() // Selects next non-emergency message using updated logic

        if msg == null:
            // Handle empty queue or no suitable message case
            displayFallbackMessage() // e.g., show time and "No messages"
            wait(DEFAULT_FALLBACK_DURATION) 
            lastTimeSync = 0 // Ensure time sync happens after fallback
            continue

        // 1. TRANSITION_MODE (Cycle 6)
        // Display "next stop" hint and potentially sync time
        displayNextStopHint(msg.nextMessageHint) 
        if (currentTime - lastTimeSync) >= TIME_SYNC_INTERVAL:
             sendTimeUpdate(currentTime) // Send 'uHHMM' command
             lastTimeSync = currentTime
        switchToCycle6() // Send xC6 command
        wait(TRANSITION_DURATION)
        
        // 2. MESSAGE_PREPARATION 
        // Already done conceptually by having 'msg' object. Send commands now or earlier.
        // If commands not sent yet, send Cycle 0 commands here:
        // sendCommandsForCycle0(msg) 

        // 3. DISPLAY_MESSAGE (Cycle 0)
        prepareMessage(msg) // Prepare Cycle 0 commands if not done earlier
        sendCommandsForCycle0(msg) // Send line, tariff, intro, scroll commands if not sent
        switchToCycle0() // Send xC0 command
        displayDuration = calculateDisplayDuration(msg)
        wait(displayDuration)
        
        // 4. Update message stats
        updateMessageDisplayStats(msg) // Includes updating lastDisplayTime
        if msg.isEphemeral:
            msg.remainingDisplays--
            if msg.remainingDisplays <= 0 or (msg.expiryTime > 0 and msg.expiryTime <= getCurrentTime()):
                removeEphemeralMessage(msg)

```

## 8. Edge Cases and Resilience

### 8.1 Empty Queue Handling (Updated)
- When no messages are available for selection (`selectNextMessage` returns null):
  1. Display fallback message showing current date/time with "No messages" text
  2. Use `DEFAULT_FALLBACK_DURATION` (30 seconds recommended)
  3. Continue regular polling for new messages
  4. Log this condition for monitoring

### 8.2 Emergency Interruption
- Emergency messages (priority >= `EMERGENCY_PRIORITY_THRESHOLD`), such as doorbell alerts or critical server statuses, bypass the normal `selectNextMessage` logic and the TRANSITION_MODE (Cycle 6).
- Upon detection of a *new* emergency message, the controller **immediately**:
    1. Prepares the necessary commands (`l`, `e`, `zI`, `zM`) for the emergency message content.
    2. Sends these commands to the display.
    3. Sends the `xC0` command to force the display into Cycle 0, effectively interrupting whatever was previously shown and displaying the emergency message immediately.
- After the emergency message's calculated display duration, the regular display cycle logic (checking for other messages, potentially entering transition mode) resumes.
- The `runDisplayCycle` pseudocode in Section 7 reflects this interruption logic.

### 8.3 Message Expiry During Display
- Background thread marks expired messages
- Messages won't be selected after expiry time
- Current display completes normally

## 9. Performance Considerations
- Cache optimization: Pre-format message commands
- Minimize sorting operations using insertion-sorted collections
- Lazy expiry checking when selecting next message
