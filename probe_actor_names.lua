-- Check whether each nearby actor has a usable name pointer at +0x544.
local base = getAddressSafe('darkeden.exe+AB31D0')
local manager = base and readInteger(base)
local header = manager and readInteger(manager + 0x34)
local root = header and readInteger(header + 4)
if not root or root == 0 then print('Nearby tree unavailable'); return end
local seen = {}
local function walk(node)
  if not node or node == 0 or node == header or seen[node] then return end
  seen[node] = true
  local left, right = readInteger(node), readInteger(node + 8)
  if left and readInteger(left + 4) == node then walk(left) end
  local actor = readInteger(node + 0x14)
  if actor and actor ~= 0 then
    local id = readInteger(actor + 8) or 0
    local namePointer = readInteger(actor + 0x544)
    local bytes = namePointer and readBytes(namePointer, 24, true)
    local hex = {}
    if bytes then for i = 1, #bytes do hex[#hex + 1] = string.format('%02X', bytes[i]) end end
    local ascii = namePointer and readString(namePointer, 64, false)
    print(string.format('id=%04X actor=%08X namePtr=%08X text=%q bytes=%s',
      id, actor, namePointer or 0, ascii or '', table.concat(hex, ' ')))
  end
  if right and readInteger(right + 4) == node then walk(right) end
end
walk(root)
