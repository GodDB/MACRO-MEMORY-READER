-- Read-only Cheat Engine Lua script for a located nearby-character tree.
-- Normally called by auto_nearby.lua after it finds the current root field.
local rootPointerExpression = nearbyRootPointerExpression
if not rootPointerExpression then
  print("Run auto_nearby.lua to locate the current character tree first.")
  return
end

local function hex(value)
  return string.format("%08X", value or 0)
end

local function bytesForDword(value)
  local parts = {}
  for i = 0, 3 do
    parts[#parts + 1] = string.format("%02X", math.floor(value / (256 ^ i)) % 256)
  end
  return table.concat(parts, " ")
end

local function nameAt(address)
  local length = readBytes(address + 4, 1)
  if not length or length < 2 or length > 24 then return nil end
  local bytes = readBytes(address + 5, length, true)
  if not bytes or #bytes ~= length then return nil end
  local chars = {}
  for i = 1, length do
    local byte = bytes[i]
    if not byte or byte < 32 or byte > 126 then return nil end
    chars[i] = string.char(byte)
  end
  return table.concat(chars)
end

local nameCache = {}
local function findName(id)
  if nameCache[id] ~= nil then return nameCache[id] or nil end
  local matches = AOBScan(bytesForDword(id), "+W-C")
  local name = nil
  if matches then
    for i = 0, matches.Count - 1 do
      local address = tonumber(matches[i], 16)
      if address then
        name = nameAt(address)
        if name then break end
      end
    end
    matches.destroy()
  end
  nameCache[id] = name or false
  return name
end

local pointerAddress = getAddressSafe(rootPointerExpression)
if not pointerAddress then
  print("Root pointer expression could not be resolved: " .. rootPointerExpression)
  return
end

local root = readInteger(pointerAddress)
if not root or root == 0 then
  print("Root pointer is empty at " .. hex(pointerAddress))
  return
end

local sentinel = readInteger(root + 4)
if not sentinel then
  print("Could not read the tree root at " .. hex(root))
  return
end
local rootActor = readInteger(root + 0x18)
local rootNodeId = readInteger(root + 0x10)
if not rootActor or not rootNodeId or rootNodeId <= 0 or rootNodeId > 0xFFFF or
   readInteger(rootActor + 8) ~= rootNodeId then
  print("The value at " .. hex(pointerAddress) .. " does not point to a character tree")
  return
end

print("Root pointer: " .. hex(pointerAddress) .. " -> " .. hex(root))
print("Name\tX\tY\tID\tActor\tNode")

local seen = {}
local seenActors = {}
local count = 0
local function walk(node)
  if not node or node == 0 or node == sentinel or seen[node] then return end
  if count >= 1024 then return end
  seen[node] = true
  count = count + 1

  local left = readInteger(node)
  local right = readInteger(node + 8)
  local actor = readInteger(node + 0x18)

  if left and left ~= sentinel and readInteger(left + 4) == node then walk(left) end
  if actor and actor ~= 0 and not seenActors[actor] then
    local id = readInteger(actor + 8)
    local x = readSmallInteger(actor + 0x0C)
    local y = readSmallInteger(actor + 0x0E)
    local nodeId = readInteger(node + 0x10)
    if id and nodeId == id and id > 0 and id <= 0xFFFF and x and y then
      seenActors[actor] = true
      local name = findName(id) or ("ID_" .. hex(id))
      print(string.format("%s\t%d\t%d\t%04X\t%s\t%s", name, x, y, id, hex(actor), hex(node)))
    end
  end
  if right and right ~= sentinel and readInteger(right + 4) == node then walk(right) end
end

walk(root)
print("Nodes visited: " .. count)
