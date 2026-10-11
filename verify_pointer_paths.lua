-- Run this in Cheat Engine's Lua Engine after restarting the game.
-- It only reads memory. The saved candidates target the root-pointer field.
local candidates = dofile(nearbyPointerCandidateFile or
  [[C:\Users\Administrator\Desktop\MACRO\pointer_candidates.lua]])

local function u32(value)
  if not value then return nil end
  if value < 0 then return value + 0x100000000 end
  return value
end

local function hex(value)
  return string.format("%08X", value or 0)
end

local baseResolved = 0
local pathResolved = 0
local rootReadable = 0
local nodesValidated = 0
local examples = {}

local function resolve(candidate)
  local address = getAddressSafe(candidate.module .. "+" .. string.format("%X", candidate.base))
  if not address then return nil end
  baseResolved = baseResolved + 1
  for i = #candidate.offsets, 1, -1 do
    local ptr = u32(readInteger(address))
    if not ptr or ptr == 0 then return nil end
    address = ptr + candidate.offsets[i]
  end
  return address
end

local function validNode(node)
  if not node or node == 0 then return false end
  local actor = u32(readInteger(node + 0x18))
  local nodeId = u32(readInteger(node + 0x10))
  if not actor or actor == 0 or not nodeId or nodeId == 0 then return false end
  local actorId = u32(readInteger(actor + 8))
  local x = readSmallInteger(actor + 0x0C)
  local y = readSmallInteger(actor + 0x0E)
  return actorId == nodeId and x ~= nil and y ~= nil
end

local function validTree(root)
  if not validNode(root) then return false end
  local sentinel = u32(readInteger(root + 4))
  if not sentinel or sentinel == 0 or sentinel == root then return false end
  local left = u32(readInteger(root))
  local right = u32(readInteger(root + 8))
  return (left and left ~= sentinel and validNode(left)) or
         (right and right ~= sentinel and validNode(right))
end

local matches = {}
for _, candidate in ipairs(candidates) do
  local field = resolve(candidate)
  if field then pathResolved = pathResolved + 1 end
  local root = field and u32(readInteger(field))
  if root then rootReadable = rootReadable + 1 end
  if root and validNode(root) then nodesValidated = nodesValidated + 1 end
  if field and #examples < 8 then
    examples[#examples + 1] = string.format("%s+%X -> field=%s root=%s",
      candidate.module, candidate.base, hex(field), root and hex(root) or "unreadable")
  end
  if root and validTree(root) then
    matches[#matches + 1] = {candidate = candidate, field = field, root = root}
  end
end

table.sort(matches, function(a, b)
  local x = a.candidate
  local y = b.candidate
  if #x.offsets ~= #y.offsets then return #x.offsets < #y.offsets end
  if (x.module == "darkeden.exe") ~= (y.module == "darkeden.exe") then
    return x.module == "darkeden.exe"
  end
  return x.base < y.base
end)

print("Valid paths: " .. #matches .. " / " .. #candidates)
print(string.format("Stages: module bases=%d, complete paths=%d, readable roots=%d, valid nodes=%d",
  baseResolved, pathResolved, rootReadable, nodesValidated))
for _, example in ipairs(examples) do print("Example: " .. example) end
local oldRoot = u32(readInteger(0x0F66A3A0))
print("Old field 0F66A3A0 now contains: " .. (oldRoot and hex(oldRoot) or "unreadable"))
for i = 1, math.min(#matches, 20) do
  local m = matches[i]
  local parts = {}
  for j = #m.candidate.offsets, 1, -1 do
    parts[#parts + 1] = string.format("%X", m.candidate.offsets[j])
  end
  print(string.format("%d. %s+%X offsets=%s field=%s root=%s",
    i, m.candidate.module, m.candidate.base, table.concat(parts, ","),
    hex(m.field), hex(m.root)))
end

if #matches > 0 then
  nearbyRootPointerExpression = hex(matches[1].field)
  print("Nearby characters from first valid path:")
  dofile([[C:\Users\Administrator\Desktop\MACRO\nearby_characters.lua]])
  nearbyRootPointerExpression = nil
end
