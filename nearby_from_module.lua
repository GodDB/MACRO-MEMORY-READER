-- Read-only nearby character list from a module-relative tree path.
-- [darkeden.exe+AB31D0] -> manager; [manager+34] -> tree header;
-- [header+4] -> root; node children at +0/+8, actor pointer at +14.
local function u32(n)
  if not n then return nil end
  return n < 0 and n + 0x100000000 or n
end

local function hex(n)
  return string.format("%08X", n or 0)
end

local function idBytes(id)
  local parts = {}
  for i = 0, 3 do
    parts[#parts + 1] = string.format("%02X", math.floor(id / 256 ^ i) % 256)
  end
  return table.concat(parts, " ")
end

local function fallbackName(id)
  local hits = AOBScan(idBytes(id), "+W-C")
  if not hits then return nil end
  local name = nil
  for i = 0, hits.Count - 1 do
    local address = tonumber(hits[i], 16)
    local length = address and readBytes(address + 4, 1)
    if length and length >= 2 and length <= 32 then
      local bytes = readBytes(address + 5, length, true)
      if bytes and #bytes == length then
        local chars = {}
        local valid = true
        for j = 1, length do
          local byte = bytes[j]
          if not byte or byte < 32 or byte > 126 then valid = false; break end
          chars[#chars + 1] = string.char(byte)
        end
        if valid then name = table.concat(chars); break end
      end
    end
  end
  hits.destroy()
  return name
end

local names = {}
local function nameFor(id, actor)
  if names[id] then return names[id] end
  local namePointer = actor and u32(readInteger(actor + 0x544))
  local direct = namePointer and readString(namePointer, 64, false)
  if direct and direct ~= "" then names[id] = direct; return direct end
  names[id] = fallbackName(id) or ("ID_" .. hex(id))
  return names[id]
end

local moduleField = getAddressSafe("darkeden.exe+AB31D0")
if not moduleField then
  print("darkeden.exe module not found. Attach Cheat Engine to the game.")
  return
end
local manager = u32(readInteger(moduleField))
local header = manager and u32(readInteger(manager + 0x34))
local root = header and u32(readInteger(header + 4))
if not manager or not header or not root or manager == 0 or header == 0 or root == 0 then
  print("Nearby tree is not available yet. Enter the game world and retry.")
  return
end

print(string.format("Path: darkeden.exe+AB31D0 -> %s -> +34 %s -> +4 %s",
  hex(manager), hex(header), hex(root)))
print("Name\tRace\tX\tY\tID\tActor\tNode")

local function raceFor(actor)
  local flag = u32(readInteger(actor + 0x558))
  local kind = u32(readInteger(actor + 0x610))
  if flag == 0x42 and kind == 0 then return "Ousters" end
  if flag == 0 and kind == 2 then return "Vampire" end
  if flag == 0x63 and kind == 0 then return "Slayer" end
  return "Unknown"
end

local seenNodes = {}
local seenActors = {}
local count = 0
local function walk(node)
  if not node or node == 0 or node == header or seenNodes[node] or count >= 2048 then return end
  seenNodes[node] = true
  local left = u32(readInteger(node))
  local right = u32(readInteger(node + 8))
  local actor = u32(readInteger(node + 0x14))
  if left and left ~= header and u32(readInteger(left + 4)) == node then walk(left) end
  if actor and actor ~= 0 and not seenActors[actor] then
    local id = u32(readInteger(actor + 8))
    local key = u32(readInteger(node + 0x10))
    local x = readSmallInteger(actor + 0x0C)
    local y = readSmallInteger(actor + 0x0E)
    if id and id > 0 and key == id and x and y and x < 5000 and y < 5000 then
      seenActors[actor] = true
      count = count + 1
      print(string.format("%s\t%s\t%d\t%d\t%04X\t%s\t%s",
        nameFor(id, actor), raceFor(actor), x, y, id, hex(actor), hex(node)))
    end
  end
  if right and right ~= header and u32(readInteger(right + 4)) == node then walk(right) end
end

walk(root)
print("Characters: " .. count)
