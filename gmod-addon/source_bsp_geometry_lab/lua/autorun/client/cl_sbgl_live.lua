-- SBGL client adapter. Requires the optional native gmcl_sbgl_bridge_win64.dll.
if SERVER then return end

local ok, bridge = pcall(require, "sbgl_bridge")
if not ok or not istable(bridge) then
    ErrorNoHalt("[SBGL] Native bridge unavailable. Install gmcl_sbgl_bridge_win64.dll in garrysmod/lua/bin/.\n")
    return
end

local ADDON = "source_bsp_geometry_lab"
local SEND_INTERVAL = 0.10
local lastSend = 0
local latestTelemetry = nil
local renderCamera = nil
local target = nil

local function vec(v)
    if not v then return nil end
    return { x = v.x, y = v.y, z = v.z }
end

local function angle(a)
    if not a then return nil end
    return { pitch = a.p, yaw = a.y, roll = a.r }
end

local function safeMethod(ent, name)
    if not IsValid(ent) or not isfunction(ent[name]) then return nil end
    local okCall, result = pcall(ent[name], ent)
    if okCall then return result end
    return nil
end

local function collectTelemetry()
    local ply = LocalPlayer()
    if not IsValid(ply) then return nil end

    local vehicle, handler
    if isfunction(ply.lvsGetVehicle) then
        local success, result = pcall(ply.lvsGetVehicle, ply)
        if success and IsValid(result) then vehicle = result end
    end
    if isfunction(ply.lvsGetWeaponHandler) then
        local success, result = pcall(ply.lvsGetWeaponHandler, ply)
        if success and IsValid(result) then handler = result end
    end

    local aimVector = safeMethod(handler, "GetAimVector")
    return {
        type = "telemetry",
        schema = 1,
        player = {
            position = vec(ply:GetPos()),
            eye_position = renderCamera and renderCamera.position or nil,
            eye_angles = renderCamera and renderCamera.angles or angle(ply:EyeAngles()),
            eye_vector = renderCamera and renderCamera.vector or nil,
            fov = ply:GetFOV()
        },
        lvs = {
            vehicle_class = IsValid(vehicle) and vehicle:GetClass() or nil,
            vehicle_position = IsValid(vehicle) and vec(vehicle:GetPos()) or nil,
            weapon_handler_class = IsValid(handler) and handler:GetClass() or nil,
            aim_vector = vec(aimVector),
            aim_angles = aimVector and angle(aimVector:Angle()) or nil
        }
    }
end

hook.Add("PreDrawTranslucentRenderables", "SBGL_CaptureRenderCamera", function()
    renderCamera = {
        position = vec(EyePos()),
        angles = angle(EyeAngles()),
        vector = vec(EyeVector())
    }
end)

timer.Create("SBGL_BridgeTelemetry", SEND_INTERVAL, 0, function()
    local now = RealTime()
    if now - lastSend < SEND_INTERVAL then return end
    lastSend = now
    latestTelemetry = collectTelemetry()
    if latestTelemetry then
        bridge.send(util.TableToJSON(latestTelemetry, false) or "{}")
    end

    local command = bridge.poll()
    if command and command ~= "" then
        local decoded = util.JSONToTable(command)
        if istable(decoded) and decoded.type == "target" then target = decoded end
    end
end)

hook.Add("HUDPaint", "SBGL_BridgeAimMarker", function()
    local ply = LocalPlayer()
    if not IsValid(ply) then return end

    -- A HUD marker is only drawn when the Qt app has supplied a target command.
    -- The marker is a visual reference; it does not control the weapon.
    if istable(target) and target.enabled and isnumber(target.yaw) and isnumber(target.pitch) then
        local view = EyeAngles()
        local dyaw = math.AngleDifference(target.yaw, view.y)
        local dpitch = target.pitch - view.p
        local fov = math.max(ply:GetFOV(), 1)
        local focal = ScrH() * 0.5 / math.tan(math.rad(fov * 0.5))
        local x = ScrW() * 0.5 + math.tan(math.rad(dyaw)) * focal
        local y = ScrH() * 0.5 - math.tan(math.rad(dpitch)) * focal
        if x > -40 and x < ScrW() + 40 and y > -40 and y < ScrH() + 40 then
            surface.SetDrawColor(70, 235, 255, 235)
            surface.DrawLine(x - 12, y, x - 4, y)
            surface.DrawLine(x + 4, y, x + 12, y)
            surface.DrawLine(x, y - 12, x, y - 4)
            surface.DrawLine(x, y + 4, x, y + 12)
            surface.DrawOutlinedRect(x - 3, y - 3, 6, 6, 1)
        end
    end

    local lvs = latestTelemetry and latestTelemetry.lvs or nil
    local vehicle = lvs and lvs.vehicle_class or "no LVS vehicle"
    draw.SimpleTextOutlined("SBGL BRIDGE  |  " .. vehicle, "DermaDefaultBold",
        16, ScrH() - 26, Color(110, 230, 255), TEXT_ALIGN_LEFT, TEXT_ALIGN_TOP, 1, Color(0, 0, 0, 220))
end)
