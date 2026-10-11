-- Read-only check of the older module-based other-character path in README.md.
local function u32(n)
  if not n then return nil end
  return n < 0 and n + 0x100000000 or n
end

local function fmt(n)
  return n and string.format("%08X", n) or "unreadable"
end

local address = getAddressSafe("darkeden.exe+AB31D0")
print("module field=" .. fmt(address))
if not address then return end
local offsets = {0x34, 0x00, 0x14}
for i, offset in ipairs(offsets) do
  local pointer = u32(readInteger(address))
  print(string.format("read %d: [%s]=%s", i, fmt(address), fmt(pointer)))
  if not pointer or pointer == 0 then return end
  address = pointer + offset
end
local actor = u32(readInteger(address))
print("actor=" .. fmt(actor))
if actor and actor ~= 0 then
  print("actor ID=" .. fmt(u32(readInteger(actor + 8))) ..
    " X=" .. tostring(readSmallInteger(actor + 0x0C)) ..
    " Y=" .. tostring(readSmallInteger(actor + 0x0E)))
end
