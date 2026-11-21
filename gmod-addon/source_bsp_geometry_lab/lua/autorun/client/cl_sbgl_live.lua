-- Source BSP Geometry Lab: client-side telemetry bridge
-- Install under garrysmod/addons/source_bsp_geometry_lab/lua/autorun/client/
-- No server-side files or permissions are required.
if SERVER then return end

local ADDON = "source_bsp_geometry_lab"
local DATA_FILE = ADDON .. "/telemetry.json"
local UPDATE_INTERVAL = 0.10

local function validEntity(ent)
    return IsValid(ent)
end

local function vec(v)
    if not v then return nil end
    return { x = v.x, y = v.y, z = v.z }
end

local function ang(a)
    if not a then return nil end
    return { pitch = a.p, yaw = a.y, roll = a.r }
end

local function safeCall(obj, method)
    if not validEntity(obj) or not isfunction(obj[method]) then return nil end
    local ok, result = pcall(obj[method], obj)
    if ok then return result end
    return nil
end

local function collect()
    local ply = LocalPlayer()
    if not validEntity(ply) then return nil end

    local vehicle
    if isfunction(ply.lvsGetVehicle) then
        local ok, result = pcall(ply.lvsGetVehicle, ply)
        if ok and validEntity(result) then vehicle = result end
    end

    local handler
    if isfunction(ply.lvsGetWeaponHandler) then
        local ok, result = pcall(ply.lvsGetWeaponHandler, ply)
        if ok and validEntity(result) then handler = result end
    end

    local aimVector = safeCall(handler, "GetAimVector")
    local vehicleClass = validEntity(vehicle) and vehicle:GetClass() or nil
    local weaponClass = validEntity(handler) and handler:GetClass() or nil

    return {
        schema = 1,
        timestamp = os.time(),
        player = {
            position = vec(ply:GetPos()),
            eye_position = vec(EyePos()),
            eye_angles = ang(EyeAngles()),
            eye_vector = vec(EyeVector()),
            fov = ply:GetFOV()
        },
        lvs = {
            vehicle_class = vehicleClass,
            vehicle_position = validEntity(vehicle) and vec(vehicle:GetPos()) or nil,
            weapon_handler_class = weaponClass,
            aim_vector = vec(aimVector),
            aim_angles = aimVector and ang(aimVector:Angle()) or nil
        }
    }
end

local function writeTelemetry()
    local payload = collect()
    if not payload then return end
    file.CreateDir(ADDON)
    file.Write(DATA_FILE, util.TableToJSON(payload, true) or "{}")
end

timer.Create("SBGL_TelemetryExport", UPDATE_INTERVAL, 0, writeTelemetry)

-- Minimal in-game diagnostic indicator. This reports what API data is available;
-- it deliberately does not alter weapon controls or fire anything.
hook.Add("HUDPaint", "SBGL_TelemetryStatus", function()
    local ply = LocalPlayer()
    if not validEntity(ply) then return end

    local vehicle = isfunction(ply.lvsGetVehicle) and ply:lvsGetVehicle() or nil
    local handler = isfunction(ply.lvsGetWeaponHandler) and ply:lvsGetWeaponHandler() or nil
    local vehicleName = validEntity(vehicle) and vehicle:GetClass() or "not detected"
    local handlerName = validEntity(handler) and handler:GetClass() or "not detected"

    draw.SimpleTextOutlined("SBGL LIVE  |  LVS: " .. vehicleName,
        "DermaDefaultBold", 16, ScrH() - 44, Color(120, 230, 255), TEXT_ALIGN_LEFT,
        TEXT_ALIGN_TOP, 1, Color(0, 0, 0, 220))
    draw.SimpleTextOutlined("Weapon handler: " .. handlerName,
        "DermaDefault", 16, ScrH() - 28, Color(230, 230, 230), TEXT_ALIGN_LEFT,
        TEXT_ALIGN_TOP, 1, Color(0, 0, 0, 220))
end)
