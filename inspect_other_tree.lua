-- Read-only inspection of the module-based map containing the known character.
local function u32(n)
  if not n then return nil end
  return n < 0 and n + 0x100000000 or n
end

local function fmt(n)
  return n and string.format("%08X", n) or "unreadable"
end

local base = getAddressSafe("darkeden.exe+AB31D0")
local manager = base and u32(readInteger(base))
local header = manager and u32(readInteger(manager + 0x34))
print("base=" .. fmt(base) .. " manager=" .. fmt(manager) .. " header=" .. fmt(header))
if not header or header == 0 then return end

local leftmost = u32(readInteger(header))
local root = u32(readInteger(header + 4))
local rightmost = u32(readInteger(header + 8))
print("header leftmost=" .. fmt(leftmost) .. " root=" .. fmt(root) ..
  " rightmost=" .. fmt(rightmost))

local function actorAt(node)
  if not node or node == 0 then return nil end
  local actor = u32(readInteger(node + 0x14))
  if not actor or actor == 0 then return nil end
  local id = u32(readInteger(actor + 8))
  local x = readSmallInteger(actor + 0x0C)
  local y = readSmallInteger(actor + 0x0E)
  if not id or id == 0 or id > 0xFFFF or not x or not y then return nil end
  return {address=actor, id=id, x=x, y=y}
end

local seen = {}
local actorSeen = {}
local valid = 0
local function walk(node, depth)
  if not node or node == 0 or node == header or seen[node] or depth > 100 then return end
  seen[node] = true
  local left = u32(readInteger(node))
  local parent = u32(readInteger(node + 4))
  local right = u32(readInteger(node + 8))
  local actor = actorAt(node)
  print(string.format("node=%s left=%s parent=%s right=%s actor=%s id=%s xy=%s,%s",
    fmt(node), fmt(left), fmt(parent), fmt(right),
    actor and fmt(actor.address) or "none", actor and fmt(actor.id) or "-",
    actor and tostring(actor.x) or "-", actor and tostring(actor.y) or "-"))
  if actor and not actorSeen[actor.address] then
    actorSeen[actor.address] = true
    valid = valid + 1
  end
  if left and left ~= header and u32(readInteger(left + 4)) == node then walk(left, depth + 1) end
  if right and right ~= header and u32(readInteger(right + 4)) == node then walk(right, depth + 1) end
end

walk(root, 0)
print("Valid unique actors=" .. valid)
