-- mods/HideGlass/init.lua
-- 目标：在运行时将命中“玻璃”的材质槽覆写为全透明占位材质
-- 依赖：CET（Cyber Engine Tweaks）和 Cron（CET 自带）

local NULL_MAT_PATH = "mat/glass_null.mi"         -- 你的全透明占位材质
local DEBUG_MAT_PATH = "mat/debug_magenta.mi"     -- 可选：目视检查用的洋红材质
local PREVIEW_MODE = false                        -- true 则先用 DEBUG 材质做目视验证

-- 命名规则：先从关键词开始，后续可替换为你的白名单
local glassPatterns = {"glass", "window", "windscreen", "veh_glass"}

-- 黑名单：避免误伤（例如全息、UI、雨刷水珠等）
local blacklistPatterns = {"holo", "ui_", "hud", "fx_", "rain"}

-- ===== 工具函数 =====

local function strtolower(s) return string.lower(s or "") end

local function hasPattern(name, pats)
  name = strtolower(name)
  for _, p in ipairs(pats) do
    if string.find(name, p, 1, true) then return true end
  end
  return false
end

local function isGlassName(name)
  if hasPattern(name, blacklistPatterns) then return false end
  return hasPattern(name, glassPatterns)
end

-- 安全调用某个方法（如果不存在则返回 nil）
local function safe_call(obj, method, ...)
  if not obj or not method or not obj[method] then return nil end
  local ok, ret = pcall(obj[method], obj, ...)
  if ok then return ret end
  return nil
end

-- 统一“写覆写”的函数：先清，再写
local function applyOverride(meshComp, slot, matPath)
  -- Clear → Set 的顺序能确保我们是“最后写入者”
  safe_call(meshComp, "ClearMaterialOverride", slot)
  return safe_call(meshComp, "SetMaterialOverride", slot, matPath)
end

-- 尝试取得“槽位数量”，同时兼容返回材质数组的实现
local function iterSlots(meshComp)
  local mats = safe_call(meshComp, "GetMaterials")
  if type(mats) == "table" and #mats > 0 then
    -- 旧风格：直接返回表
    local slots = {}
    for i, m in ipairs(mats) do
      table.insert(slots, {slot = i - 1, matObj = m})
    end
    return slots
  end
  -- 新风格：按槽位数量访问
  local n = safe_call(meshComp, "GetMaterialSlotCount") or 0
  local slots = {}
  for s = 0, n - 1 do
    table.insert(slots, {slot = s, matObj = safe_call(meshComp, "GetMaterial", s)})
  end
  return slots
end

-- 取得基材名与覆写名（尽可能多地兼容）
local function getNames(meshComp, slot)
  local baseName = safe_call(meshComp, "GetMaterialName", slot) or ""
  local ovwName  = safe_call(meshComp, "GetOverrideMaterialName", slot) or ""
  return baseName, ovwName
end

-- 核心判定：是否该槽位疑似玻璃
local function shouldPatchSlot(baseName, ovwName)
  -- 任何一个命中即可
  if isGlassName(baseName) then return true end
  if isGlassName(ovwName)  then return true end
  return false
end

-- 将单个 Mesh 组件的“玻璃槽位”覆写为透明材质（或预览材质）
local function patchMeshComponent(mc, logPrefix)
  local slots = iterSlots(mc)
  if not slots or #slots == 0 then return end
  for _, s in ipairs(slots) do
    local slot = s.slot
    local baseName, ovwName = getNames(mc, slot)
    if shouldPatchSlot(baseName, ovwName) then
      local target = PREVIEW_MODE and DEBUG_MAT_PATH or NULL_MAT_PATH
      local ok = applyOverride(mc, slot, target)
      if ok then
        print(string.format("%s override slot=%d base=\"%s\" ovw=\"%s\" -> %s",
              logPrefix or "[HideGlass]", slot, baseName, ovwName, target))
      end
    end
  end
end

-- 处理实体：遍历网格组件
local function patchEntity(ent, reason)
  local comps = safe_call(ent, "GetComponents") or {}
  for _, c in ipairs(comps) do
    local isMesh = (safe_call(c, "IsA", "entMeshComponent") == true)
                   or (safe_call(c, "IsA", "entSkinnedMeshComponent") == true)
    if isMesh then
      patchMeshComponent(c, string.format("[HideGlass][%s]", reason or "tick"))
    end
  end
end

-- ===== 事件注册 =====

registerForEvent("onInit", function()
  print("[HideGlass] init, NULL_MAT=" .. NULL_MAT_PATH .. ", PREVIEW=" .. tostring(PREVIEW_MODE))

  -- 1) 实体挂载后处理（此时大多基材已就位）
  Observe("entEntity", "OnGameAttach", function(self)
    pcall(patchEntity, self, "attach")
  end)

  -- 2) 外观变更后延迟 1 个 tick 再处理（等引擎把覆写落地）
  Observe("entEntity", "OnAppearanceChanged", function(self)
    Cron.After(0.05, function()
      pcall(patchEntity, self, "appearance")
    end)
  end)

  -- 3) 兜底：每 1s 巡检可见实体，防漏网之鱼，代价低
  Cron.Every(1.0, function()
    local ents = (Game.GetVisibleEntities and Game.GetVisibleEntities()) or {}
    for _, e in ipairs(ents) do
      pcall(patchEntity, e, "cron")
    end
  end)
end)
