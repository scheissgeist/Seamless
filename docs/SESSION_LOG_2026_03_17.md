# Summon Sign Protobuf Structure - Research from ds3os

## DS2 SotFS Protobuf Field Layout (from ds3os)

Source: `https://github.com/TLeonardUK/ds3os/blob/main/Protobuf/DarkSouls2/DS2_Frpg2RequestMessage.proto`

### RequestCreateSign (opcode 0x0394)
```protobuf
message RequestCreateSign {
    required uint32 online_area_id = 1;
    required MatchingParameter matching_parameter = 2;
    required bytes player_struct = 3;
    required uint32 cell_id = 4;
    required uint32 sign_type = 5;
}
```
**NOTE:** RequestCreateSign does NOT contain player_id or steam_id. The server fills those from the authenticated session (`Player.GetPlayerId()`, `Player.GetSteamId()`).

### SignData (sent TO clients in GetSignListResponse)
```protobuf
message SignData {
    required SignInfo sign_info = 1;     // contains player_id + sign_id
    required int64 online_area_id = 2;
    required MatchingParameter matching_parameter = 3;
    required bytes player_struct = 4;
    required string player_steam_id = 5; // STEAM ID IS FIELD 5
    required int64 cell_id = 6;
    required SignType sign_type = 7;
}
```

### SignInfo
```protobuf
message SignInfo {
    required uint32 player_id = 1;       // SERVER-ASSIGNED player ID
    required uint32 sign_id = 2;         // SERVER-ASSIGNED sign ID
}
```

### PushRequestSummonSign (opcode 0x039B - sent TO the sign placer)
```protobuf
message PushRequestSummonSign {
    required PushMessageId push_message_id = 1;  // always 0x039B
    required int64 player_id = 2;                // WHO is summoning (the host)
    required int64 sign_id = 3;
    required bytes player_struct = 4;
    required string player_steam_id = 5;         // STEAM ID of summoner
}
```

### MatchingParameter (THE KEY for Name-Engraved Ring)
```protobuf
message MatchingParameter {
    required uint32 calibration_version = 1;
    required uint32 soul_level = 2;
    required uint32 clear_count = 3;
    required uint32 unknown_4 = 4;
    // NOTE: field 5 is MISSING (skipped)
    required uint32 covenant = 6;
    required uint32 unknown_7 = 7;              // nat type?
    required uint32 disable_cross_region_play = 8;
    required uint32 unknown_9 = 9;              // region?
    required uint32 unknown_10 = 10;
    required uint32 name_engraved_ring = 11;    // 0 = not using, >0 = god selection
    required uint32 soul_memory = 12;
}
```

### PushRequestRemoveSign (opcode 0x039D)
```protobuf
message PushRequestRemoveSign {
    required PushMessageId push_message_id = 1;
    required int64 player_id = 2;
    required int64 sign_id = 3;
    required string player_steam_id = 4;
}
```

## How ds3os Handles Sign Filtering

From `DS2_SignManager.cpp`:

1. **Sign creation**: Server assigns `PlayerId` from session, NOT from the protobuf message
2. **Sign list retrieval**: `CanMatchWith()` checks soul_memory ranges + name_engraved_ring
3. **Name-Engraved Ring**: Field 11 of MatchingParameter, checked as `> 0`
4. **Matching**: `Config.DS2_WhiteSoapstoneMatchingParameters.CheckMatch(host_SM, match_SM, host_NER > 0)`

## Key Insight for Seamless ParseHook

**The messages we can intercept in ParseHook and what they contain:**

### When RECEIVING sign list (RequestGetSignListResponse):
- Contains `SignData` entries with `player_steam_id` (field 5) and `SignInfo.player_id` (field 1 of embedded message at field 1)
- This is what we'd filter: drop signs from non-session players

### When RECEIVING a summon push (PushRequestSummonSign):
- Field 2 = player_id (int64/varint), Field 5 = player_steam_id (string)
- We could reject summons from non-session players

### Wire format for extracting player_steam_id from raw bytes:
```
Protobuf wire format:
  field_key = (field_number << 3) | wire_type

  For field 5 (string): key = (5 << 3) | 2 = 0x2A
  For field 2 (varint/int64): key = (2 << 3) | 0 = 0x10
  For field 1 (varint for push_message_id enum): key = (1 << 3) | 0 = 0x08

Strategy to find player_steam_id in SignData:
  1. Scan bytes for 0x2A (field 5, wire type 2 = length-delimited)
  2. Read varint length
  3. Read that many bytes as UTF-8 string
  4. That string is the Steam ID (looks like "76561198XXXXXXXXX")
```

### Without full protobuf parser - minimal extraction:

For **SignData** (in GetSignListResponse), player_steam_id is field 5:
- Wire key byte = `0x2A` (field 5, wire type 2)
- Followed by varint length, then the steam ID string

For **PushRequestSummonSign**, player_steam_id is also field 5:
- Same wire key byte = `0x2A`
- But field 1 (push_message_id) comes first as a varint

For **SignInfo** (embedded in SignData at field 1), player_id is field 1:
- Wire key = `0x08` (field 1, wire type 0 = varint)
- SignInfo itself is embedded as a length-delimited blob at field 1 of SignData (key `0x0A`)

## Approach for Filtering in ParseHook

```
In ParseHook, when className contains "SignList" or "SignData":
  1. Call original parse first (let protobuf populate the object)
  2. The raw bytes (data, size) contain the serialized protobuf
  3. Scan raw bytes for steam_id strings (pattern: "7656119" prefix, 17 digits)
  4. Compare against session player list
  5. If steam_id NOT in session -> return false (parse "failed", game ignores message)

PROBLEM: GetSignListResponse contains MULTIPLE signs. We can't partially filter.
We'd need to either:
  a) Block the entire response and re-request (bad)
  b) Modify the raw bytes to remove non-session signs (complex)
  c) Filter at a different layer (let signs show but block the summon)

BETTER APPROACH - Filter at SUMMON time:
  When className contains "PushRequestSummonSign":
    - Extract player_steam_id from field 5
    - If NOT in session player list -> return false (block the summon)
  This lets session players see all signs but only session summons go through.

BEST APPROACH - Filter the SIGN LIST:
  When className contains "RequestGetSignListResponse":
    - After parse, the C++ object has the sign data populated
    - We can't easily access the parsed C++ fields without knowing the class layout
    - BUT we know the raw bytes and can scan for steam IDs
    - Strategy: scan for ALL steam IDs in the response, check if ANY are non-session
    - If the response contains ONLY session player signs, allow it
    - If it contains ANY non-session signs... we need the modification approach
```

## DS3 Comparison (for reference)

DS3 uses a `SummonSignMessage` sub-message:
```protobuf
message SummonSignMessage {
    required uint32 player_id = 1;
    required string steam_id = 2;
    required SignInfo sign_info = 3;
    required bytes player_struct = 4;
}
```

DS3 has a `password` field in MatchingParameter (field 10) instead of name_engraved_ring.
DS2 has `name_engraved_ring` (field 11) which serves the same purpose - private matchmaking.

## Name-Engraved Ring Protocol Details

The Name-Engraved Ring works by setting `MatchingParameter.name_engraved_ring` to a non-zero value (1-13, one per god). In ds3os:
- `CanMatchWith()` passes `Host.name_engraved_ring() > 0` to `CheckMatch()`
- When NER is active, matching ranges are widened (more permissive soul memory range)
- Both players must have the SAME god selected for the ring to match
- The server filters signs by checking if the MatchingParameter values are compatible

**For Seamless**: We could USE the Name-Engraved Ring as a private session filter. If all session players select the same god, only session players' signs would appear. But this requires the ring item and a specific god selection, which is limiting.
