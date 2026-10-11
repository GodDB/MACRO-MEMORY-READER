-- Read-only search for the nearby-character tree in the current game run.
local knownIds = {0x2F52, 0x2F07, 0x2F3C, 0x2F66}

local function u32(value)
  if not value then return nil end
  if value < 0 then return value + 0x100000000 end
  return value
end

local function hex(value)
  return string.format("%08X", value or 0)
end

local function bytesForDword(value)
  local parts = {}
  for i = 0, 3 do
    parts[#parts + 1] = string.format("%02X", math.floor(value / 256 ^ i) % 256)
  end
  return table.concat(parts, " ")
end

local function validNode(node)
  if not node or node == 0 then return false end
  local actor = u32(readInteger(node + 0x18))
  local id = u32(readInteger(node + 0x10))
  if not actor or actor == 0 or not id or id == 0 then return false end
  local actorId = u32(readInteger(actor + 8))
  local x = readSmallInteger(actor + 0x0C)
  local y = readSmallInteger(actor + 0x0E)
  return actorId == id and x ~= nil and y ~= nil
end

local function rootFrom(node)
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

local function countTree(root)
  local sentinel = u32(readInteger(root + 4))
  local seen = {}
  local function walk(node)
    if not node or node == sentinel or seen[node] or not validNode(node) then return 0 end
    seen[node] = true
    local left = u32(readInteger(node))
    local right = u32(readInteger(node + 8))
    local total = 1
    if left and u32(readInteger(left + 4)) == node then total = total + walk(left) end
    if right and u32(readInteger(right + 4)) == node then total = total + walk(right) end
    return total
  end
  return walk(root)
end

local function childParentFields(root)
  local sentinel = u32(readInteger(root + 4))
  local seen = {}
  local fields = {}
  local function walk(node)
    if not node or node == sentinel or seen[node] or not validNode(node) then return end
    seen[node] = true
    local left = u32(readInteger(node))
    local right = u32(readInteger(node + 8))
    for _, child in ipairs({left or 0, right or 0}) do
      if child ~= 0 and child ~= sentinel and u32(readInteger(child + 4)) == node then
        fields[child + 4] = true
        walk(child)
      end
    end
  end
  walk(root)
  return fields
end

local roots = {}
local nodeHits = 0
for _, id in ipairs(knownIds) do
  local scan = AOBScan(bytesForDword(id), "+W-C")
  if scan then
    for i = 0, scan.Count - 1 do
      local hit = tonumber(scan[i], 16)
      local node = hit and hit - 0x10
      if validNode(node) then
        nodeHits = nodeHits + 1
        local root = rootFrom(node)
        if root then roots[root] = countTree(root) end
      end
    end
    scan.destroy()
  end
end

local ranked = {}
for root, count in pairs(roots) do
  ranked[#ranked + 1] = {root = root, count = count}
end
table.sort(ranked, function(a, b) return a.count > b.count end)

print("Matching nodes: " .. nodeHits .. "; distinct roots: " .. #ranked)
for i = 1, math.min(#ranked, 10) do
  print(string.format("%d. root=%s, valid characters=%d", i, hex(ranked[i].root), ranked[i].count))
end

if #ranked == 0 then
  print("No known IDs found in a valid tree. The IDs may have changed after restart.")
  return
end

print("Searching for external fields pointing at each root; this can take a few minutes...")
local candidates = {}
for _, item in ipairs(ranked) do
  if item.count >= 2 then
    local parentFields = childParentFields(item.root)
    local fields = AOBScan(bytesForDword(item.root), "+W-C")
    if fields then
      for i = 0, fields.Count - 1 do
        local address = tonumber(fields[i], 16)
        if address and not parentFields[address] then
          candidates[#candidates + 1] = {root = item.root, count = item.count, field = address}
        end
      end
      fields.destroy()
    end
  end
end

print("External root fields: " .. #candidates)
for i = 1, math.min(#candidates, 30) do
  local item = candidates[i]
  print(string.format("%d. field=%s -> root=%s; characters=%d",
    i, hex(item.field), hex(item.root), item.count))
end

if #candidates > 0 then
  nearbyRootPointerExpression = hex(candidates[1].field)
  print("Characters from the first external field:")
  dofile([[C:\Users\Administrator\Desktop\MACRO\nearby_characters.lua]])
  nearbyRootPointerExpression = nil
end
