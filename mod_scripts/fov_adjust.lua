-- mods/SimpleFOV/init.lua

local originalFOV = nil

registerForEvent("onInit", function()
    local cam = Game.GetPlayer():GetFPPCameraComponent()
    if cam ~= nil then
        originalFOV = cam:GetFOV()
        print("[SimpleFOV] 脚本已加载，记录初始 FOV = " .. originalFOV)
    end
end)

-- 减小 FOV
registerHotkey("FOV_Dec", "Decrease FOV", function()
    local cam = Game.GetPlayer():GetFPPCameraComponent()
    if cam ~= nil then
        cam:SetFOV(cam:GetFOV() - 5.0)
        print("FOV 调整为: " .. cam:GetFOV())
    end
end)

-- 增加 FOV
registerHotkey("FOV_Inc", "Increase FOV", function()
    local cam = Game.GetPlayer():GetFPPCameraComponent()
    if cam ~= nil then
        cam:SetFOV(cam:GetFOV() + 5.0)
        print("FOV 调整为: " .. cam:GetFOV())
    end
end)

-- 恢复原始 FOV
registerHotkey("FOV_Reset", "Reset FOV", function()
    local cam = Game.GetPlayer():GetFPPCameraComponent()
    if cam ~= nil and originalFOV ~= nil then
        cam:SetFOV(originalFOV)
        print("FOV 已恢复为原始值: " .. originalFOV)
    end
end)
