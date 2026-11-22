-- TFC bridge server-side LVS input relay.
-- LVS evaluates the ATTACK key state on the SERVER, so a client-only hook cannot fire weapons.
if not SERVER then return end

util.AddNetworkString("tfc_bridge_fire_request")
util.AddNetworkString("tfc_bridge_fire_ack")

local pendingPulse = setmetatable({}, { __mode = "k" })
local lastRequest = setmetatable({}, { __mode = "k" })

local function safeNumber(ent, method, fallback)
    if not IsValid(ent) or not isfunction(ent[method]) then return fallback end
    local ok, value = pcall(ent[method], ent)
    if ok and isnumber(value) then return value end
    return fallback
end

local function reply(ply, accepted, message)
    if not IsValid(ply) then return end
    net.Start("tfc_bridge_fire_ack")
        net.WriteBool(accepted)
        net.WriteString(message or "")
    net.Send(ply)
end

local function getWeaponHandler(ply, seat)
    if IsValid(seat) and isfunction(seat.lvsGetWeapon) then
        local ok, handler = pcall(seat.lvsGetWeapon, seat)
        if ok and IsValid(handler) and isfunction(handler.GetActiveWeapon) then
            return handler
        end
    end

    if isfunction(ply.lvsGetWeaponHandler) then
        local ok, handler = pcall(ply.lvsGetWeaponHandler, ply)
        if ok and IsValid(handler) and isfunction(handler.GetActiveWeapon) then
            return handler
        end
    end

    return NULL
end

net.Receive("tfc_bridge_fire_request", function(_, ply)
    if not IsValid(ply) or not ply:IsPlayer() then return end

    if (lastRequest[ply] or 0) > CurTime() then
        reply(ply, false, "Fire request rate-limited")
        return
    end

    if isfunction(ply.lvsGetInputEnabled) and not ply:lvsGetInputEnabled() then
        reply(ply, false, "LVS player input is disabled")
        return
    end

    local seat = ply:GetVehicle()
    if not IsValid(seat) or not isfunction(ply.lvsGetVehicle) then
        reply(ply, false, "Player is not in an LVS seat")
        return
    end

    local okBase, base = pcall(ply.lvsGetVehicle, ply)
    if not okBase or not IsValid(base) or base.LVS ~= true then
        reply(ply, false, "Current vehicle is not LVS")
        return
    end

    local handler = getWeaponHandler(ply, seat)
    if not IsValid(handler) then
        reply(ply, false, "Active LVS weapon handler not found")
        return
    end

    local okWeapon, weapon = pcall(handler.GetActiveWeapon, handler)
    if not okWeapon or not istable(weapon) or table.IsEmpty(weapon) then
        reply(ply, false, "No active LVS weapon is selected")
        return
    end

    local overheated = false
    if isfunction(handler.GetOverheated) then
        local okHeat, value = pcall(handler.GetOverheated, handler)
        overheated = not okHeat or value == true
    elseif isfunction(handler.GetNWOverheated) then
        local okHeat, value = pcall(handler.GetNWOverheated, handler)
        overheated = not okHeat or value == true
    end
    if overheated then
        reply(ply, false, "LVS weapon is overheated")
        return
    end

    -- In LVS, Ammo <= 0 means unlimited ammunition. GetClip() is a heat
    -- display calculation and can be -1 for unlimited-ammo weapon presets.
    local maxAmmo = safeNumber(handler, "GetMaxAmmo", -1)
    local ammo = safeNumber(handler, "GetAmmo", safeNumber(handler, "GetNWAmmo", -1))
    local clip = safeNumber(handler, "GetClip", -1)
    local clipConfigured = isnumber(weapon.Clip) and weapon.Clip > 0
    if maxAmmo > 0 and ammo <= 0 then
        reply(ply, false, "LVS weapon is out of ammunition")
        return
    end
    if (maxAmmo > 0 or clipConfigured) and clip <= 0 then
        reply(ply, false, "LVS weapon clip/heat capacity is depleted")
        return
    end

    local keys = ply._lvsKeyDown
    if istable(keys) and keys.ATTACK == true then
        reply(ply, false, "ATTACK is already held; release the game fire key first")
        return
    end

    -- lvsKeyDown() calls LVS.PlayerKeyDown on the SERVER immediately before
    -- LVS WeaponsThink reads ATTACK. Returning true once makes a one-read pulse.
    pendingPulse[ply] = CurTime() + 0.35
    lastRequest[ply] = CurTime() + 0.25
    reply(ply, true, "Server accepted one LVS ATTACK pulse")
end)

hook.Add("LVS.PlayerKeyDown", "TFC_Bridge_ServerAttackPulse", function(ply, actionName)
    if actionName ~= "ATTACK" then return end

    local expires = pendingPulse[ply]
    if not expires then return end

    pendingPulse[ply] = nil
    if CurTime() <= expires then
        return true
    end
end)

hook.Add("PlayerDisconnected", "TFC_Bridge_ClearPulse", function(ply)
    pendingPulse[ply] = nil
    lastRequest[ply] = nil
end)

print("[TFC] Server-side LVS fire relay ready")
