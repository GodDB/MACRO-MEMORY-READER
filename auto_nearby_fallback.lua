-- Read-only locator for the nearby-character tree after a game restart.
-- Finds the current ID of crazyfuc, locates tree nodes, and checks root references.
local myName = "crazyfuc"

local function u32(value)
  if not value then return nil end
  if value < 0 then return value + 0x100000000 end
  return value
end

local function hex(value)
  return string.format("%08X", value or 0)
end

local function aobForDword(value)
  local parts = {}
  for i = 0, 3 do
    parts[#parts + 1] = string.format("%02X", math.floor(value / 256 ^ i) % 256)
  end
  return table.concat(parts, " ")
end

local function validNode(node)
  if not node or node < 0x10000 then return false end
  local actor = u32(readInteger(node + 0x18))
  local nodeId = u32(readInteger(node + 0x10))
  if not actor or not nodeId or nodeId == 0 or nodeId > 0xFFFF then return false end
  local actorId = u32(readInteger(actor + 8))
  local x = readSmallInteger(actor + 0x0C)
  local y = readSmallInteger(actor + 0x0E)
  return actorId == nodeId and x and y and x < 5000 and y < 5000
end

local function treeRoot(node)
  local seen = {}
  for _ = 1, 64 do
    if not validNode(node) or seen[node] then return nil end
    seen[node] = true
    local parent = u32(readInteger(node + 4))
    if not validNode(parent) then return node end
    if u32(readInteger(parent)) ~= node and
       u32(readInteger(parent + 8)) ~= node then return node end
    node = parent
  end
  return nil
end

local function treeSize(root, wantedId)
  local sentinel = u32(readInteger(root + 4))
  local seen = {}
  local wanted = false
  local function walk(node)
    if not node or node == sentinel or seen[node] or not validNode(node) then return 0 end
    seen[node] = true
    if u32(readInteger(node + 0x10)) == wantedId then wanted = true end
    local total = 1
    for _, child in ipairs({u32(readInteger(node)) or 0, u32(readInteger(node + 8)) or 0}) do
      if child ~= 0 and child ~= sentinel and u32(readInteger(child + 4)) == node then
        total = total + walk(child)
      end
    end
    return total
  end
  local count = walk(root)
  return count, wanted
end

local letters = {}
for i = 1, #myName do letters[#letters + 1] = string.format("%02X", myName:byte(i)) end
local names = AOBScan(table.concat(letters, " "), "+W-C")
local ids = {}
if names then
  for i = 0, names.Count - 1 do
    local address = tonumber(names[i], 16)
    if address and readBytes(address - 1, 1) == #myName then
      local id = u32(readInteger(address - 5))
      if id and id > 0 and id <= 0xFFFF then ids[id] = true end
    end
  end
  names.destroy()
end

local idCount = 0
for _ in pairs(ids) do idCount = idCount + 1 end
print("IDs found for " .. myName .. ": " .. idCount)
if idCount == 0 then
  print("The character name is not in readable writable memory yet. Enter the game world and try again.")
  return
end

local roots = {}
local nodeCount = 0
for id in pairs(ids) do
  print("ID " .. hex(id))
  local hits = AOBScan(aobForDword(id), "+W-C")
  if hits then
    for i = 0, hits.Count - 1 do
      local address = tonumber(hits[i], 16)
      local node = address and address - 0x10
      if validNode(node) and u32(readInteger(node + 0x10)) == id then
        nodeCount = nodeCount + 1
        local root = treeRoot(node)
        if root then
          local count, containsMe = treeSize(root, id)
          if containsMe and count > 0 then roots[root] = {count = count, id = id} end
        end
      end
    end
    hits.destroy()
  end
end

local ranked = {}
for root, info in pairs(roots) do
  ranked[#ranked + 1] = {root = root, count = info.count, id = info.id}
end
table.sort(ranked, function(a, b)
  local ax = a.root >= 0x10000000 and a.root < 0x20000000
  local bx = b.root >= 0x10000000 and b.root < 0x20000000
  if ax ~= bx then return ax end
  if a.count ~= b.count then return a.count > b.count end
  return a.root < b.root
end)
print("Matching nodes: " .. nodeCount .. "; roots containing me: " .. #ranked)
for i, item in ipairs(ranked) do
  print(string.format("Root %d: %s, characters=%d", i, hex(item.root), item.count))
end

local fields = {}
for _, item in ipairs(ranked) do
  local hits = AOBScan(aobForDword(item.root), "+W-C")
  if hits then
    for i = 0, hits.Count - 1 do
      local address = tonumber(hits[i], 16)
      if address and u32(readInteger(address)) == item.root then
        local count, containsMe = treeSize(item.root, item.id)
        if containsMe and count > 0 then
          fields[#fields + 1] = {field = address, root = item.root, count = count}
        end
      end
    end
    hits.destroy()
  end
end

table.sort(fields, function(a, b)
  if a.count ~= b.count then return a.count > b.count end
  local aExternal = math.abs(a.field - a.root) > 0x10000
  local bExternal = math.abs(b.field - b.root) > 0x10000
  if aExternal ~= bExternal then return aExternal end
  return a.field < b.field
end)
print("Current root fields: " .. #fields)
for i = 1, math.min(#fields, 20) do
  local item = fields[i]
  print(string.format("%d. field=%s -> root=%s, characters=%d",
    i, hex(item.field), hex(item.root), item.count))
end

for i = 1, #fields do
  local item = fields[i]
  if u32(readInteger(item.field)) == item.root then
    print("Nearby list from field " .. hex(item.field) .. ":")
    nearbyRootPointerExpression = hex(item.field)
    dofile([[C:\Users\Administrator\Desktop\MACRO\nearby_characters.lua]])
    break
  end
end
nearbyRootPointerExpression = nil
