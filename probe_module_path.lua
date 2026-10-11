-- Read-only check of the module-based character path already present in this project.
local function u32(n)
  if not n then return nil end
  return n < 0 and n + 0x100000000 or n
end

local function fmt(n)
  return n and string.format("%08X", n) or "unreadable"
end

local base = getAddressSafe("darkeden.exe+A23C2C")
print("module field=" .. fmt(base))
if not base then return end
local manager = u32(readInteger(base))
print("manager=" .. fmt(manager))
if not manager or manager == 0 then return end
local node = u32(readInteger(manager + 0x68))
print("[manager+68]=" .. fmt(node))
if not node or node == 0 then return end
local actor = u32(readInteger(node + 8))
print("[node+8]=" .. fmt(actor))
if actor and actor ~= 0 then
  print("actor ID=" .. fmt(u32(readInteger(actor + 8))) ..
    " X=" .. tostring(readSmallInteger(actor + 0x0C)) ..
    " Y=" .. tostring(readSmallInteger(actor + 0x0E)))
end
print("node fields:")
for offset = 0, 0x28, 4 do
  print(string.format("+%02X = %s", offset, fmt(u32(readInteger(node + offset)))))
end
