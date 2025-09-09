-- mods/GlassSpy/init.lua
-- 目的：运行时扫描“玻璃相关”的材质槽，打印潜在的 ID/路径，并导出 CSV
-- 依赖：CET，Cron（CET 自带）

-- ===== 可调参数 =====
local Cron = require("Cron")
local SCAN_INTERVAL = 1.0      -- 巡检周期（秒）；设为 <=0 则只在热键触发时扫描
local HOTKEY = "F3"            -- 手动触发扫描的按键
local NAME_PATTERNS = {"window", "glass", "windscreen", "veh_glass"}  -- 命名命中关键词（小写）
local BLACKLIST = {"holo", "ui_", "hud", "fx_", "rain"}               -- 排除关键词（小写）
local CSV_DIR = "bin/x64/plugins/cyber_engine_tweaks/mods/GlassSpy"
local CSV_FILE = CSV_DIR .. "/glass_scan.csv"

-- ===== 工具封装 =====
local function strtolower(s) return string.lower(s or "") end
local function hasPattern(name, pats)
  local n = strtolower(name)
  for _, p in ipairs(pats) do
    if string.find(n, p, 1, true) then return true end
  end
  return false
end

local function isGlassLike(name)
  if hasPattern(name, BLACKLIST) then return false end
  return hasPattern(name, NAME_PATTERNS)
end

local function safe_call(obj, method, ...)
  if not obj or not method or not obj[method] then return nil end
  local ok, ret = pcall(obj[method], obj, ...)
  if ok then return ret end
  return nil
end

-- 取得槽位数量与迭代器（兼容不同实现）
local function iterSlots(meshComp)
  local mats = safe_call(meshComp, "GetMaterials")
  if type(mats) == "table" and #mats > 0 then
    local slots = {}
    for i, m in ipairs(mats) do
      table.insert(slots, i - 1)
    end
    return slots
  end
  local n = safe_call(meshComp, "GetMaterialSlotCount") or 0
  local slots = {}
  for s = 0, n - 1 do table.insert(slots, s) end
  return slots
end

-- 尽可能采集材质信息（名字、覆写名、路径、可能的 ID/哈希）
local function collectMaterialInfo(meshComp, slot)
  -- 名称
  local baseName = safe_call(meshComp, "GetMaterialName", slot) or ""         -- 常见：可能就是 basematerial 的路径/名称
  local ovwName  = safe_call(meshComp, "GetOverrideMaterialName", slot) or "" -- 若该槽位有覆写
  -- 材质对象
  local matObj   = safe_call(meshComp, "GetMaterial", slot)

  -- 进一步尝试从 matObj 取“DepotPath/资源路径/实例名/哈希/ID”等（不同版本可能不可用）
  local depotPath = ""
  if matObj then
    depotPath = matObj.DepotPath or matObj.depotPath or matObj.path or ""
    if type(depotPath) ~= "string" then depotPath = "" end
  end

  -- 可能的“实例/哈希/ID”尝试（不保证存在）
  local instId   = ""
  if matObj then
    instId = (safe_call(matObj, "GetInstanceID") or safe_call(matObj, "GetGuid") or safe_call(matObj, "GetHash")) or ""
    if type(instId) ~= "string" then
      -- 有些实现返回 number；统一转字符串
      if type(instId) == "number" then instId = tostring(instId) else instId = "" end
    end
  end

  return baseName, ovwName, depotPath, instId
end

-- 扫描单个网格组件
local function scanMeshComponent(entName, meshComp, results)
  local slots = iterSlots(meshComp)
  if not slots or #slots == 0 then return end
  for _, slot in ipairs(slots) do
    local baseName, ovwName, depotPath, instId = collectMaterialInfo(meshComp, slot)
    -- 命中规则：base/override 名称任何一侧包含 window/glass… 即记录
    if isGlassLike(baseName) or isGlassLike(ovwName) then
      table.insert(results, {
        entity = entName or "",
        slot   = slot,
        base   = baseName,
        ovw    = ovwName,
        path   = depotPath,
        iid    = instId
      })
    end
  end
end

-- 扫描一个实体
local function scanEntity(e, results)
  local entName = tostring(safe_call(e, "GetCurrentAppearanceName") or safe_call(e, "ToString") or "")
  local comps = safe_call(e, "GetComponents") or {}
  for _, c in ipairs(comps) do
    local isMesh = (safe_call(c, "IsA", "entMeshComponent") == true)
                or (safe_call(c, "IsA", "entSkinnedMeshComponent") == true)
    if isMesh then scanMeshComponent(entName, c, results) end
  end
end

-- 写 CSV（追加）
local function ensureDir(path)
  -- 简单确保目录存在（Windows Lua 无内置 mkdir，这里尝试通过 io.open 侧写法）
  -- 若失败也不致命，只是不写文件
  local f = io.open(path, "a+"); if f then f:close() end
end

local function writeCsv(results)
  ensureDir(CSV_DIR .. "/dummy.txt") -- 触发目录创建（CET 环境通常已存在 mods 子目录）
  local file, err = io.open(CSV_FILE, "w")
  if not file then
    print("[GlassSpy] !! cannot open CSV for write: " .. tostring(err))
    return
  end
  file:write("entity,slot,baseName,overrideName,depotPath,instanceID\n")
  for _, r in ipairs(results) do
    local function esc(s)
      s = tostring(s or "")
      s = s:gsub('"', '""')
      return '"' .. s .. '"'
    end
    file:write(string.format("%s,%d,%s,%s,%s,%s\n",
      esc(r.entity), r.slot, esc(r.base), esc(r.ovw), esc(r.path), esc(r.iid)))
  end
  file:close()
  print(string.format("[GlassSpy] exported %d rows -> %s", #results, CSV_FILE))
end

-- 一次完整扫描：可见实体 → 过滤 → 打印 & 导出
local function runScan()
  local results = {}
  local ents = (Game.GetVisibleEntities and Game.GetVisibleEntities()) or {}
  for _, e in ipairs(ents) do
    pcall(scanEntity, e, results)
  end

  -- 控制台打印少量样例
  print(string.format("[GlassSpy] hits=%d", #results))
  for i = 1, math.min(10, #results) do
    local r = results[i]
    print(string.format("  [%d] slot=%d base=%s ovw=%s path=%s iid=%s",
      i, r.slot, r.base, r.ovw, r.path, r.iid))
  end

  writeCsv(results)
end

-- ===== 事件与热键 =====
registerForEvent("onInit", function()
  print("[GlassSpy] init. Hotkey=" .. HOTKEY .. ", interval=" .. tostring(SCAN_INTERVAL))
  -- 热键：按下触发一次扫描
  registerHotkey("GlassSpyScan", HOTKEY, function()
    print("[GlassSpy] manual scan...")
    pcall(runScan)
  end)

  -- 自动巡检（可选）
  if SCAN_INTERVAL and SCAN_INTERVAL > 0 then
    Cron.Every(SCAN_INTERVAL, function()
      pcall(runScan)
    end)
  end
end)
