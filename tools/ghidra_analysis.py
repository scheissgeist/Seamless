# Ghidra Jython script for DS2 SotFS seamless co-op analysis.
# Run from Ghidra: Window -> Python (Jython), then:
#   execfile(r"E:\Seamless\tools\ghidra_analysis.py")
# Output: E:\Seamless\tools\ghidra_results.txt

import jarray

OUT_PATH = r"E:\Seamless\tools\ghidra_results.txt"

program = currentProgram
listing = program.getListing()
memory = program.getMemory()
refMgr = program.getReferenceManager()
funcMgr = program.getFunctionManager()
baseAddr = program.getImageBase()

results = []

def log(msg):
    results.append(str(msg))
    print(msg)

def searchBytes(search_str):
    found = []
    pattern = [ord(c) for c in search_str]
    pat_bytes = jarray.array(pattern, 'b')
    masks = jarray.array([-1] * len(pattern), 'b')
    for block in memory.getBlocks():
        if not block.isInitialized():
            continue
        start = block.getStart()
        end = block.getEnd()
        addr = memory.findBytes(start, masks, pat_bytes, masks, True, monitor)
        while addr is not None and addr.compareTo(end) <= 0:
            found.append(addr)
            try:
                addr = memory.findBytes(addr.add(1), end, pat_bytes, masks, True, monitor)
            except:
                break
    return found

def searchString(s):
    found = []
    pat = jarray.array([ord(c) for c in s], 'b')
    masks = jarray.array([-1] * len(s), 'b')
    for block in memory.getBlocks():
        if not block.isInitialized():
            continue
        start = block.getStart()
        end = block.getEnd()
        a = memory.findBytes(start, end, pat, masks, True, monitor)
        while a is not None:
            found.append(a)
            try:
                next_a = a.add(1)
                if next_a.compareTo(end) >= 0:
                    break
                a = memory.findBytes(next_a, end, pat, masks, True, monitor)
            except:
                break
    return found

def getXrefs(address):
    return list(refMgr.getReferencesTo(address))

def getFunctionContaining(address):
    return funcMgr.getFunctionContaining(address)

def getCallers(func):
    callers = []
    if func is None:
        return callers
    entry = func.getEntryPoint()
    for ref in refMgr.getReferencesTo(entry):
        if ref.getReferenceType().isCall():
            caller = funcMgr.getFunctionContaining(ref.getFromAddress())
            callers.append((ref.getFromAddress(), caller))
    return callers

def analyzeFuncCmps(func):
    """Find CMP instructions with small constants in a function."""
    hits = []
    if func is None:
        return hits
    body = func.getBody()
    it = listing.getInstructions(body, True)
    while it.hasNext():
        inst = it.next()
        if inst.getMnemonicString() == "CMP":
            s = inst.toString()
            for val in [",0x2", ",0x3", ",0x4", ",2", ",3", ",4"]:
                if val in s:
                    hits.append((inst.getAddress(), s))
                    break
    return hits

def showContext(addr, before=2, after=2):
    lines = []
    inst = listing.getInstructionAt(addr)
    if inst is None:
        return lines
    prev = inst
    prevs = []
    for _ in range(before):
        prev = listing.getInstructionBefore(prev.getAddress())
        if prev:
            prevs.insert(0, prev)
    for p in prevs:
        lines.append("    -%d: %s  %s" % (before, p.getAddress(), p.toString()))
    lines.append("    >> %s  %s" % (addr, inst.toString()))
    nxt = inst
    for i in range(after):
        nxt = listing.getInstructionAfter(nxt.getAddress())
        if nxt:
            lines.append("    +%d: %s  %s" % (i+1, nxt.getAddress(), nxt.toString()))
    return lines

def getBytes(address, length):
    buf = jarray.zeros(length, 'b')
    memory.getBytes(address, buf)
    return [(b & 0xff) for b in buf]

def exeOffset(addr):
    return addr.subtract(baseAddr)

# ============================================================================
log("=" * 70)
log("DS2 SotFS Ghidra Analysis - Seamless Co-op")
log("Base: %s" % baseAddr)
log("=" * 70)

# ============================================================================
log("\n### 1. Player cap: CMP 2/3/4 in GuestPlayer functions ###")
# ============================================================================

guest_strings = searchString("GuestPlayer")
log("GuestPlayer string hits: %d" % len(guest_strings))
checked = set()
for s in guest_strings:
    for xref in getXrefs(s):
        func = getFunctionContaining(xref.getFromAddress())
        if func is None:
            continue
        ep = func.getEntryPoint()
        if ep in checked:
            continue
        checked.add(ep)
        cmps = analyzeFuncCmps(func)
        if cmps:
            log("\nFunc %s at %s (offset +0x%X):" % (func.getName(), ep, exeOffset(ep)))
            for cmp_addr, cmp_str in cmps:
                log("  CMP at %s (offset +0x%X): %s" % (cmp_addr, exeOffset(cmp_addr), cmp_str))
                for line in showContext(cmp_addr):
                    log(line)

# ============================================================================
log("\n### 2. Fog wall / boss gate strings ###")
# ============================================================================

for term in ["FogWall", "FogGate", "FogDoor", "BossGate", "AreaBoss", "FogArea"]:
    hits = searchString(term)
    if hits:
        log("\n'%s' found at %d locations:" % (term, len(hits)))
        for h in hits:
            log("  %s (offset +0x%X)" % (h, exeOffset(h)))
            for xref in getXrefs(h)[:3]:
                func = getFunctionContaining(xref.getFromAddress())
                fname = func.getName() if func else "unknown"
                log("    xref from %s in %s" % (xref.getFromAddress(), fname))

# ============================================================================
log("\n### 3. Phantom damage / hit validation ###")
# ============================================================================

for term in ["ApplyDamage", "HitResult", "CanDamage", "DamageCalc", "DamageParam", "AttackParam"]:
    hits = searchString(term)
    if hits:
        log("'%s': %d hits" % (term, len(hits)))
        for h in hits[:3]:
            log("  %s" % h)

# ============================================================================
log("\n### 4. Session/max player strings ###")
# ============================================================================

for term in ["MaxPlayer", "MaxGuest", "MaxPhantom", "SessionSlot", "PlayerSlot", "MaxSummon", "NumPhantom", "PhantomCount"]:
    hits = searchString(term)
    if hits:
        log("'%s': %d hits" % (term, len(hits)))
        for h in hits[:3]:
            log("  %s (offset +0x%X)" % (h, exeOffset(h)))
            for xref in getXrefs(h)[:2]:
                func = getFunctionContaining(xref.getFromAddress())
                fname = func.getName() if func else "unknown"
                log("    xref from %s in %s" % (xref.getFromAddress(), fname))

# ============================================================================
log("\n### 5. RTTI class names ###")
# ============================================================================

for cls in ["EventPhantomReturn", "GuestManager", "PhantomManager",
            "SummonManager", "CoopManager", "SessionManager",
            "NetSessionManager", "EventBossBattle"]:
    hits = searchString(cls)
    if hits:
        log("Class '%s':" % cls)
        for h in hits:
            log("  RTTI at %s (offset +0x%X)" % (h, exeOffset(h)))
            for xref in getXrefs(h)[:2]:
                func = getFunctionContaining(xref.getFromAddress())
                fname = func.getName() if func else "unknown"
                log("    xref from %s in %s" % (xref.getFromAddress(), fname))

# ============================================================================
log("\n### 6. Verify boss-kill patch ###")
# ============================================================================

patch_addr = baseAddr.add(0x44ef7b)
try:
    b = getBytes(patch_addr, 5)
    log("Bytes at exe+0x44ef7b: %s" % " ".join(["%02x" % x for x in b]))
    expected = [0xe8, 0x30, 0x2c, 0xd4, 0xff]
    if b == expected:
        log("MATCH - patch target confirmed")
    elif b == [0x90]*5:
        log("ALREADY PATCHED (NOPs)")
    else:
        log("MISMATCH - expected e8 30 2c d4 ff")
except Exception as e:
    log("Error reading patch addr: %s" % str(e))

# ============================================================================
log("\n### 7. NotifyJoinGuestPlayer vtable location ###")
# ============================================================================

hits = searchString("RequestNotifyJoinGuestPlayer")
log("RequestNotifyJoinGuestPlayer strings: %d" % len(hits))
for h in hits:
    log("  at %s (offset +0x%X)" % (h, exeOffset(h)))
    for xref in getXrefs(h)[:4]:
        func = getFunctionContaining(xref.getFromAddress())
        fname = func.getName() if func else "unknown"
        log("    xref from %s in %s" % (xref.getFromAddress(), fname))

# ============================================================================
log("\n" + "=" * 70)
log("Done.")
log("=" * 70)

with open(OUT_PATH, 'w') as f:
    f.write('\n'.join(results))

print("\nSaved to %s (%d lines)" % (OUT_PATH, len(results)))
