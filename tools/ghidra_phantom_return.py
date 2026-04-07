# Ghidra Jython script: find a precise patch site for boss-kill phantom return
# without breaking death/respawn.
#
# Run from Ghidra: Window -> Python (Jython), then:
#   execfile(r"E:\Seamless\tools\ghidra_phantom_return.py")
# Output: E:\Seamless\tools\ghidra_phantom_return_results.txt
#
# CONTEXT
# -------
# The current mod NOPs a CALL at exe+0x44ef7b inside FUN_14044ef30 (event
# dispatch). That CALL invokes FUN_140191bb0, which creates EventPhantomReturn
# objects (RTTI vtable at exe+0x156A7D4, constructor FUN_14018d660). The NOP
# was meant to stop boss-kill phantom dismissal. Side effect: FUN_14044ef30
# is a SHARED dispatcher that also handles player death events, so NOPing
# that CALL leaves the death state machine waiting forever for an ack that
# never comes -> player can't respawn, can't open menu.
#
# We need a more targeted patch. Three candidate strategies this script tries
# to gather evidence for:
#
# STRATEGY A: Patch INSIDE FUN_140191bb0 itself.
#   If FUN_140191bb0 iterates the phantom list and emits a LeaveGuestPlayer
#   per phantom, we can NOP the per-phantom iteration / dispatch only.
#   The function still returns normally so the dispatcher's state machine
#   advances, but no phantom is actually dismissed.
#
# STRATEGY B: Patch INSIDE FUN_14044ef30 with a more surgical NOP.
#   The CALL at +0x44ef7b is one of presumably many CALLs in the dispatcher.
#   If we dump the dispatcher's full disassembly we can see what kind of
#   discriminator it uses (switch table, function pointer, type tag) and
#   patch a smaller window than the full call.
#
# STRATEGY C: Find the boss-kill upstream caller.
#   Walk callers of FUN_140191bb0. If the only caller is the boss-kill path,
#   that's our target. If there are multiple callers we need to distinguish.
#
# OUTPUT
# ------
# This script does NOT patch anything. It only gathers evidence. We read
# the output, decide on a strategy, and write the actual patch in C++.

import jarray

OUT_PATH = r"E:\Seamless\tools\ghidra_phantom_return_results.txt"

program = currentProgram
if program is None:
    raise RuntimeError(
        "currentProgram is None. Open DarkSoulsII.exe in the Ghidra "
        "CodeBrowser, then run Window -> Python from inside the "
        "CodeBrowser window (not from the project manager)."
    )
listing = program.getListing()
memory = program.getMemory()
refMgr = program.getReferenceManager()
funcMgr = program.getFunctionManager()
baseAddr = program.getImageBase()

results = []

def log(msg):
    results.append(str(msg))
    print(msg)

def exeOffset(addr):
    return addr.subtract(baseAddr)

def addrFromOffset(offset):
    return baseAddr.add(offset)

def getXrefs(address):
    return list(refMgr.getReferencesTo(address))

def getFunctionContaining(address):
    return funcMgr.getFunctionContaining(address)

def getFunctionAt(address):
    return funcMgr.getFunctionAt(address)

def getCallers(func):
    callers = []
    if func is None:
        return callers
    entry = func.getEntryPoint()
    for ref in refMgr.getReferencesTo(entry):
        if ref.getReferenceType().isCall():
            caller_func = funcMgr.getFunctionContaining(ref.getFromAddress())
            callers.append((ref.getFromAddress(), caller_func))
    return callers

def disassembleFunction(func, max_instructions=400):
    """Return list of (address, mnemonic, operands_string) for every
    instruction in the function body, up to max_instructions."""
    out = []
    if func is None:
        return out
    body = func.getBody()
    it = listing.getInstructions(body, True)
    count = 0
    while it.hasNext() and count < max_instructions:
        inst = it.next()
        out.append((inst.getAddress(), inst.getMnemonicString(), inst.toString()))
        count += 1
    return out

def findCallSitesInFunction(func):
    """Return list of (address, callee_address_or_None, raw_text) for every
    CALL instruction in the function."""
    out = []
    if func is None:
        return out
    body = func.getBody()
    it = listing.getInstructions(body, True)
    while it.hasNext():
        inst = it.next()
        if inst.getMnemonicString().startswith("CALL"):
            target = None
            try:
                refs = inst.getReferencesFrom()
                for r in refs:
                    if r.getReferenceType().isCall():
                        target = r.getToAddress()
                        break
            except:
                pass
            out.append((inst.getAddress(), target, inst.toString()))
    return out

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

def getBytes(address, length):
    buf = jarray.zeros(length, 'b')
    memory.getBytes(address, buf)
    return [(b & 0xff) for b in buf]

# ============================================================================
log("=" * 70)
log("DS2 SotFS - Boss-Kill Phantom Return Patch Site Finder")
log("Base: %s" % baseAddr)
log("=" * 70)

# ----------------------------------------------------------------------------
# 1. Resolve the three known functions from the handoff doc and confirm them.
# ----------------------------------------------------------------------------
log("\n### 1. Confirm known functions ###")

dispatcher_addr = addrFromOffset(0x44ef30)  # FUN_14044ef30 (event dispatch)
creator_addr   = addrFromOffset(0x191bb0)   # FUN_140191bb0 (creates EventPhantomReturn)
ctor_addr      = addrFromOffset(0x18d660)   # FUN_14018d660 (EventPhantomReturn ctor)
patch_addr     = addrFromOffset(0x44ef7b)   # current NOP target

dispatcher_fn = getFunctionContaining(dispatcher_addr)
creator_fn    = getFunctionContaining(creator_addr)
ctor_fn       = getFunctionContaining(ctor_addr)

def reportFn(label, fn, expected_offset):
    if fn is None:
        log("  %s: NOT FOUND at +0x%X" % (label, expected_offset))
        return
    ep = fn.getEntryPoint()
    log("  %s: %s @ %s (+0x%X)  body bytes=%d" % (
        label, fn.getName(), ep, exeOffset(ep), fn.getBody().getNumAddresses()))

reportFn("dispatcher (FUN_14044ef30)", dispatcher_fn, 0x44ef30)
reportFn("creator    (FUN_140191bb0)", creator_fn, 0x191bb0)
reportFn("constructor(FUN_14018d660)", ctor_fn, 0x18d660)

# Verify the patch target byte sequence
try:
    b = getBytes(patch_addr, 5)
    log("  patch target bytes at +0x44ef7b: %s" % " ".join(["%02x" % x for x in b]))
    if b == [0xe8, 0x30, 0x2c, 0xd4, 0xff]:
        log("  -> matches expected unpatched CALL")
    elif b == [0x90, 0x90, 0x90, 0x90, 0x90]:
        log("  -> already NOPed")
    else:
        log("  -> UNEXPECTED bytes")
except Exception as e:
    log("  patch target read failed: %s" % str(e))

# ----------------------------------------------------------------------------
# 2. STRATEGY C: who calls FUN_140191bb0?
#    If only the boss-kill path calls it, we patch the start of the function.
#    If many callers, we need to discriminate.
# ----------------------------------------------------------------------------
log("\n### 2. Callers of FUN_140191bb0 (creator) ###")

if creator_fn is not None:
    callers = getCallers(creator_fn)
    log("  Total callers: %d" % len(callers))
    for (call_addr, caller_func) in callers:
        cname = caller_func.getName() if caller_func else "(unknown)"
        cep = caller_func.getEntryPoint() if caller_func else None
        cep_str = "+0x%X" % exeOffset(cep) if cep else "?"
        log("    CALL at %s (+0x%X) from %s @ %s" % (
            call_addr, exeOffset(call_addr), cname, cep_str))
else:
    log("  creator_fn is None, skipping")

# ----------------------------------------------------------------------------
# 3. STRATEGY C also: who calls FUN_14018d660 (the constructor)?
#    If creator is the only one, that's expected. If there are other paths
#    that build EventPhantomReturn objects, we need to know.
# ----------------------------------------------------------------------------
log("\n### 3. Callers of FUN_14018d660 (constructor) ###")

if ctor_fn is not None:
    callers = getCallers(ctor_fn)
    log("  Total callers: %d" % len(callers))
    for (call_addr, caller_func) in callers:
        cname = caller_func.getName() if caller_func else "(unknown)"
        cep = caller_func.getEntryPoint() if caller_func else None
        cep_str = "+0x%X" % exeOffset(cep) if cep else "?"
        log("    CALL at %s (+0x%X) from %s @ %s" % (
            call_addr, exeOffset(call_addr), cname, cep_str))
else:
    log("  ctor_fn is None, skipping")

# ----------------------------------------------------------------------------
# 4. xrefs to the EventPhantomReturn RTTI vtable / typeinfo
#    Anything reading this address is touching EventPhantomReturn instances.
#    Useful for finding alternate creation/destruction paths.
# ----------------------------------------------------------------------------
log("\n### 4. xrefs to EventPhantomReturn RTTI (+0x156A7D4) ###")

rtti_addr = addrFromOffset(0x156A7D4)
rtti_xrefs = getXrefs(rtti_addr)
log("  total xrefs: %d" % len(rtti_xrefs))
for ref in rtti_xrefs[:20]:
    fa = ref.getFromAddress()
    fn = getFunctionContaining(fa)
    fname = fn.getName() if fn else "(no func)"
    fep = ("+0x%X" % exeOffset(fn.getEntryPoint())) if fn else "?"
    log("    from %s (+0x%X) in %s @ %s [%s]" % (
        fa, exeOffset(fa), fname, fep, ref.getReferenceType()))

# Also search nearby -- vtables are usually a few dwords past the RTTI marker
log("\n  Also scanning RTTI region 0x156A7C0..0x156A800 for vtable refs:")
for delta in range(0x0, 0x40, 0x8):
    test_addr = addrFromOffset(0x156A7C0 + delta)
    xs = getXrefs(test_addr)
    if xs:
        log("    +0x%X has %d xrefs" % (0x156A7C0 + delta, len(xs)))
        for ref in xs[:5]:
            fa = ref.getFromAddress()
            fn = getFunctionContaining(fa)
            fname = fn.getName() if fn else "(no func)"
            log("      from %s in %s [%s]" % (fa, fname, ref.getReferenceType()))

# ----------------------------------------------------------------------------
# 5. STRATEGY A evidence: dump full disassembly of FUN_140191bb0
#    We need to see how it iterates and what it calls.
#    Look for: loops (JMP backwards), per-iteration CALLs, anything that
#    looks like "for each phantom -> dispatch leave".
# ----------------------------------------------------------------------------
log("\n### 5. FUN_140191bb0 (creator) full disassembly ###")

if creator_fn is not None:
    insts = disassembleFunction(creator_fn, max_instructions=500)
    log("  total instructions: %d" % len(insts))
    log("  body size: %d bytes" % creator_fn.getBody().getNumAddresses())
    for (a, mnem, text) in insts:
        log("    %s (+0x%X) %s" % (a, exeOffset(a), text))

# ----------------------------------------------------------------------------
# 6. STRATEGY B evidence: dump call sites in FUN_14044ef30 (the dispatcher)
#    We want to see: how many CALLs total, what targets, are there CMP/JMP
#    discriminators before our specific CALL at +0x44ef7b?
# ----------------------------------------------------------------------------
log("\n### 6. FUN_14044ef30 (dispatcher) call sites ###")

if dispatcher_fn is not None:
    log("  body size: %d bytes" % dispatcher_fn.getBody().getNumAddresses())
    calls = findCallSitesInFunction(dispatcher_fn)
    log("  total CALL instructions: %d" % len(calls))
    for (ca, target, text) in calls:
        target_str = "?"
        if target is not None:
            target_str = "%s (+0x%X)" % (target, exeOffset(target))
            tfn = getFunctionContaining(target)
            if tfn:
                target_str += " %s" % tfn.getName()
        marker = "  <-- CURRENT NOP" if exeOffset(ca) == 0x44ef7b else ""
        log("    CALL at %s (+0x%X) -> %s%s" % (
            ca, exeOffset(ca), target_str, marker))

# ----------------------------------------------------------------------------
# 7. Show the 30 instructions BEFORE and AFTER the current NOP target.
#    This tells us what the dispatcher does immediately around our patch.
#    If there's a JMP/CMP right before, we can see the discriminator.
# ----------------------------------------------------------------------------
log("\n### 7. Disassembly window around exe+0x44ef7b ###")

window_start = patch_addr.subtract(0x60)  # 96 bytes back
window_end   = patch_addr.add(0x60)       # 96 bytes forward

inst = listing.getInstructionAt(window_start)
if inst is None:
    # Fall back: walk backward from the patch site
    inst = listing.getInstructionContaining(window_start)

# Just iterate through instructions in the dispatcher body that fall in [window_start, window_end]
if dispatcher_fn is not None:
    body = dispatcher_fn.getBody()
    it = listing.getInstructions(body, True)
    count = 0
    while it.hasNext():
        i = it.next()
        a = i.getAddress()
        if a.compareTo(window_start) < 0:
            continue
        if a.compareTo(window_end) > 0:
            break
        marker = "  <-- NOP TARGET" if exeOffset(a) == 0x44ef7b else ""
        log("    %s (+0x%X) %s%s" % (a, exeOffset(a), i.toString(), marker))
        count += 1
    log("  (window contained %d instructions)" % count)

# ----------------------------------------------------------------------------
# 8. Look for boss-kill discriminator strings that might appear near the
#    dispatcher or upstream of it.
# ----------------------------------------------------------------------------
log("\n### 8. Boss-kill discriminator strings ###")

for term in ["BossDead", "BossKill", "BossDeath", "OnBossDeath",
             "EventBossBattle", "EventPhantomReturn",
             "RequestNotifyKillEnemy", "NotifyKillEnemy"]:
    hits = searchString(term)
    if hits:
        log("\n  '%s': %d hits" % (term, len(hits)))
        for h in hits[:5]:
            log("    %s (+0x%X)" % (h, exeOffset(h)))
            for xref in getXrefs(h)[:3]:
                fa = xref.getFromAddress()
                fn = getFunctionContaining(fa)
                fname = fn.getName() if fn else "(no func)"
                fep = ("+0x%X" % exeOffset(fn.getEntryPoint())) if fn else "?"
                log("      xref from %s in %s @ %s" % (fa, fname, fep))

# ----------------------------------------------------------------------------
log("\n" + "=" * 70)
log("Done. Read the output and pick a patch strategy.")
# (any non-ASCII characters here will break Jython 2.7)
log("=" * 70)

with open(OUT_PATH, 'w') as f:
    f.write('\n'.join(results))

print("\nSaved to %s (%d lines)" % (OUT_PATH, len(results)))
