"""Decode a captured H-018 memory receipt OFFLINE; never opens a process.

Input JSON: module_base, module_sha256, manager, regions[{address, hex}].
Addresses may be integers or 0x strings. Regions must be non-overlapping.
The producer must capture a coherent snapshot; this decoder cannot prove timing.
"""
import json
import struct
import sys
import zlib

sys.dont_write_bytecode = True
from verify_h005_skinning import EXPECTED_SHA256


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


class Snapshot:
    def __init__(self, receipt):
        if receipt["module_sha256"].lower() != EXPECTED_SHA256:
            raise ValueError("Unsupported snapshot module hash")
        self.base = number(receipt["module_base"])
        self.manager = number(receipt["manager"])
        self.regions = sorted((number(r["address"]), bytes.fromhex(r["hex"]))
                              for r in receipt["regions"])
        previous_end = 0
        for address, data in self.regions:
            if address <= 0 or address < previous_end or not data:
                raise ValueError("Invalid or overlapping memory regions")
            previous_end = address + len(data)

    def read(self, address, size):
        for start, data in self.regions:
            if start <= address and address + size <= start + len(data):
                return data[address-start:address-start+size]
        raise ValueError(f"Missing snapshot bytes at {address:#x}, size {size}")

    def unpack(self, address, fmt):
        return struct.unpack("<" + fmt, self.read(address, struct.calcsize("<" + fmt)))[0]

    def q(self, address):
        return self.unpack(address, "Q")

    def u(self, address):
        return self.unpack(address, "I")

    def byte(self, address):
        return self.unpack(address, "B")

    def string(self, address, limit=512):
        if not address:
            raise ValueError("Null string")
        data = bytearray()
        for i in range(limit):
            value = self.byte(address+i)
            if not value:
                return data.decode("ascii")
            data.append(value)
        raise ValueError("Unterminated or oversized string")

    def tree(self, head, count):
        if not head or count > 8192 or self.byte(head+0x19) != 1:
            raise ValueError("Invalid tree header/count")
        root = self.q(head+8)
        stack = [(root, head, False)]
        seen = set()
        ordered = []
        while stack:
            node, parent, emit = stack.pop()
            if node == head:
                continue
            if emit:
                ordered.append(node)
                continue
            if (not node or node in seen or len(seen) >= count
                    or self.byte(node+0x19) != 0 or self.q(node+8) != parent):
                raise ValueError("Invalid tree: cycle, count, nil or parent")
            seen.add(node)
            stack.extend([(self.q(node+0x10), node, False),
                          (node, parent, True), (self.q(node), node, False)])
        if len(ordered) != count:
            raise ValueError("Tree count does not match nodes")
        if (self.q(head) != (ordered[0] if ordered else head)
                or self.q(head+0x10) != (ordered[-1] if ordered else head)):
            raise ValueError("Tree extrema disagree with traversal")
        return ordered


def decode(receipt):
    s = Snapshot(receipt)
    m, base = s.manager, s.base
    if s.q(m) != base+0x1CBFC48 or s.q(m+8) != base+0x1CBFE08:
        raise ValueError("Manager vtables do not match H-018")
    filters = []
    for node in s.tree(s.q(m+0x48), s.q(m+0x50)):
        f = s.q(node+0x28)
        names = {s.q(n+0x20) for n in s.tree(s.q(f+0x20), s.q(f+0x28))}
        filters.append((s.string(s.q(f+0x38)), bool(s.byte(f+8)),
                        s.u(f+0x30), names))
    rows = []
    keys = []
    for node in s.tree(s.q(m+0x58), s.q(m+0x60)):
        key_crc = s.u(node+0x20)
        inp, action, amap = s.q(node+0x28), s.q(node+0x30), s.q(node+0x38)
        if (s.q(amap) != base+0x1CBE4F0 or s.q(action) != base+0x1CBE4A8
                or s.q(amap+0x10) != m or s.q(action+0x60) != amap):
            raise ValueError("Binding object vtable/backpointer mismatch")
        name_pointer = s.q(action+0x40)
        key = s.string(s.q(inp+0x18))
        expected_crc = zlib.crc32(key.lower().encode("ascii"))
        if not key or key_crc != expected_crc or s.u(inp+0x98) != key_crc:
            raise ValueError("Binding key/CRC mismatch")
        blocking = [name for name, enabled, kind, names in filters
                    if enabled and ((kind == 0) == (name_pointer not in names))]
        manager_enabled, map_enabled = bool(s.byte(m+0xDC)), bool(s.byte(amap+8))
        rows.append({
            "binding_node": hex(node), "map": s.string(s.q(amap+0x58)),
            "action": s.string(name_pointer), "key": key,
            "default_key": s.string(s.q(inp+0x50)), "crc": hex(key_crc),
            "action_device": s.u(inp), "activation_mask": s.u(inp+0xC4),
            "modifiers": s.u(inp+0xC8), "current_state": s.u(inp+0xD0),
            "analog_operation": s.u(inp+0xD4), "analog_value": s.unpack(inp+0xAC, "f"),
            "manager_enabled": manager_enabled, "map_enabled": map_enabled,
            "blocking_filters": blocking,
            "enabled_unfiltered": manager_enabled and map_enabled and not blocking,
        })
        keys.append(key_crc)
    if keys != sorted(keys):
        raise ValueError("CRC tree order is invalid")
    return {"live_tested": False, "event_acceptance_tested": False,
            "binding_count": len(rows), "bindings": rows}


def fixture_checks():
    # Hand-authored sparse-memory topology: two distinct actions share one key.
    # This is synthetic test data, never a claim about Prey's shipped bindings.
    start, base, m = 0x10000, 0x180000000, 0x10000
    memory = bytearray(0x6000)
    def put(address, fmt, value):
        struct.pack_into("<"+fmt, memory, address-start, value)
    def q(address, value): put(address, "Q", value)
    def u(address, value): put(address, "I", value)
    def b(address, value): put(address, "B", value)
    def string(address, value):
        data = value.encode("ascii")+bytes([0])
        memory[address-start:address-start+len(data)] = data
    def empty(head):
        for offset in [0, 8, 0x10]: q(head+offset, head)
        b(head+0x19, 1)
    def singleton(head, node):
        empty(head)
        for offset in [0, 8, 0x10]: q(head+offset, node)
        for offset in [0, 8, 0x10]: q(node+offset, head)
    q(m, base+0x1CBFC48); q(m+8, base+0x1CBFE08); b(m+0xDC, 1)
    bind_head, filter_head, first, second = 0x11000, 0x11100, 0x11200, 0x11300
    q(m+0x58, bind_head); q(m+0x60, 2)
    q(m+0x48, filter_head); q(m+0x50, 0); empty(filter_head)
    singleton(bind_head, first)
    q(first+0x10, second); q(second, bind_head); q(second+8, first)
    q(second+0x10, bind_head); q(bind_head+0x10, second)
    amap = 0x12000
    q(amap, base+0x1CBE4F0); b(amap+8, 1); q(amap+0x10, m)
    q(amap+0x58, 0x14000); string(0x14000, "synthetic_menu")
    string(0x14100, "fixture_key")
    key_crc = zlib.crc32(b"fixture_key")
    for i, node in enumerate([first, second]):
        action, inp, name = 0x12200+i*0x100, 0x12800+i*0x100, 0x14200+i*0x100
        q(node+0x28, inp); q(node+0x30, action); q(node+0x38, amap); u(node+0x20, key_crc)
        q(action, base+0x1CBE4A8); q(action+0x40, name); q(action+0x60, amap)
        string(name, ["menu_confirm", "menu_back"][i])
        q(inp+0x18, 0x14100); q(inp+0x50, 0x14100); u(inp+0x98, key_crc)
        u(inp, 1); u(inp+0xC4, 3)
    def run():
        return decode({"module_base": base, "module_sha256": EXPECTED_SHA256,
                       "manager": m, "regions": [{"address": start, "hex": memory.hex()}]})
    def check(condition, message):
        if not condition: raise ValueError("Fixture failed: "+message)
    def rejected():
        try: run()
        except ValueError: return
        raise ValueError("Malformed fixture was accepted")
    result = run()
    check([r["action"] for r in result["bindings"]] == ["menu_confirm", "menu_back"],
          "duplicate CRC must preserve both bindings")
    b(amap+8, 0)
    check(not any(r["enabled_unfiltered"] for r in run()["bindings"]), "disabled map")
    b(amap+8, 1)
    b(m+0xDC, 0)
    check(not any(r["enabled_unfiltered"] for r in run()["bindings"]), "disabled manager")
    b(m+0xDC, 1)
    fnode, f, set_head, member = 0x11400, 0x12500, 0x11500, 0x11600
    singleton(filter_head, fnode); q(m+0x50, 1); q(fnode+0x28, f)
    b(f+8, 1); q(f+0x20, set_head); q(f+0x28, 1)
    q(f+0x38, 0x14400); string(0x14400, "fixture_filter")
    singleton(set_head, member); q(member+0x20, 0x14200)
    check([r["enabled_unfiltered"] for r in run()["bindings"]] == [True, False],
          "type-zero allow list")
    u(f+0x30, 1)
    check([r["enabled_unfiltered"] for r in run()["bindings"]] == [False, True],
          "nonzero block list")
    q(second+0x10, first); rejected(); q(second+0x10, bind_head)
    q(m+0x60, 3); rejected(); q(m+0x60, 2)
    u(first+0x20, key_crc^1); rejected(); u(first+0x20, key_crc)
    q(0x12800+0x18, 0xFFFFFFFF); rejected(); q(0x12800+0x18, 0x14100)
    q(amap+0x10, m+1); rejected(); q(amap+0x10, m)
    return 10


if __name__ == "__main__":
    try:
        if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
            print(json.dumps({"synthetic_checks_passed": fixture_checks(), "live_tested": False}))
        else:
            with open(sys.argv[1], encoding="utf-8") as stream:
                print(json.dumps(decode(json.load(stream)), indent=2))
    except (OSError, ValueError, KeyError, IndexError, struct.error) as error:
        print(json.dumps({"error": str(error), "live_tested": False}), file=sys.stderr)
        sys.exit(1)
