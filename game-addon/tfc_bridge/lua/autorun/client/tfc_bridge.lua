-- TFC bridge v1.1
-- Installed once at garrysmod/addons/tfc_bridge/lua/autorun/client/.
-- Built-in DATA filesystem only. No socket library, HTTP, DLL or external module.
-- Important: physical muzzle attachment Ang from the live game is the aim feedback source.
if not CLIENT then return end

local DATA_DIR = "tfc_suo"
local HOST_FILE = DATA_DIR .. "/host.json"
local COMMAND_FILE = DATA_DIR .. "/command.json"
local STATE_FILE = DATA_DIR .. "/state.json"
local TEMP_FILE = DATA_DIR .. "/state.tmp.json"
local PROTOCOL = 1

local activeSession
local lastSequence = 0
local acknowledgedSequence = 0
local commandAck = "idle"
local commandMessage = "Waiting for TFC"
local aimCommand
local aimExpires = 0
local lastAim
local firePulse = false
local attackPulsePending = false
local attackPulseExpires = 0
local lastStateWrite = 0

file.CreateDir(DATA_DIR)

local function readJson(path)
    local raw = file.Read(path, "DATA")
    if not isstring(raw) or raw == "" then return nil end
    local ok, value = pcall(util.JSONToTable, raw)
    if ok and istable(value) then return value end
    return nil
end

local function writeJson(path, value)
    local encoded = util.TableToJSON(value, false)
    if not isstring(encoded) then return false end
    file.Write(path, encoded)
    return true
end

local function safeNumber(entity, method, fallback)
    if not IsValid(entity) or not isfunction(entity[method]) then return fallback end
    local ok, value = pcall(entity[method], entity)
    if ok and isnumber(value) then return value end
    return fallback
end

local function getLVSContext()
    local ply = LocalPlayer()
    if not IsValid(ply) then return nil, nil, nil end

    local seat = ply:GetVehicle()
    if not IsValid(seat) then return nil, nil, ply end

    local base = NULL
    if isfunction(ply.lvsGetVehicle) then
        local ok, result = pcall(ply.lvsGetVehicle, ply)
        if ok and IsValid(result) then base = result end
    end

    if not IsValid(base) and isfunction(seat.lvsGetVehicle) then
        local ok, result = pcall(seat.lvsGetVehicle, seat)
        if ok and IsValid(result) then base = result end
    end

    return base, seat, ply
end

local function getGunner(seat, ply)
    if IsValid(seat) and isfunction(seat.lvsGetWeapon) then
        local ok, result = pcall(seat.lvsGetWeapon, seat)
        if ok and IsValid(result) then return result end
    end

    if IsValid(ply) and isfunction(ply.lvsGetWeaponHandler) then
        local ok, result = pcall(ply.lvsGetWeaponHandler, ply)
        if ok and IsValid(result) and result.LVS_GUNNER then return result end
    end

    return NULL
end

local function getMuzzle(base, host)
    if not IsValid(base) then return nil, nil, nil end

    local configured = istable(host) and host.muzzle_attachment or "auto"
    local attachmentName = configured
    if not isstring(attachmentName) or attachmentName == "" or attachmentName == "auto" then
        attachmentName = base.TurretBallisticsMuzzleAttachment or "muzzle"
    end

    local ok, index = pcall(base.LookupAttachment, base, attachmentName)
    if not ok or not isnumber(index) or index <= 0 then
        return nil, nil, attachmentName
    end

    local got, attachment = pcall(base.GetAttachment, base, index)
    if not got or not istable(attachment) or
       not isvector(attachment.Pos) or not isangle(attachment.Ang) then
        return nil, nil, attachmentName
    end

    return attachment.Pos, attachment.Ang, attachmentName
end

local function weaponReadiness(seat, ply)
    local gunner = getGunner(seat, ply)
    if not IsValid(gunner) or not isfunction(gunner.GetActiveWeapon) then
        return false, false, -1, -1, true
    end

    local ok, activeWeapon = pcall(gunner.GetActiveWeapon, gunner)
    if not ok or not istable(activeWeapon) or table.IsEmpty(activeWeapon) then
        return false, false, -1, -1, true
    end

    local clip = safeNumber(gunner, "GetClip", -1)
    local ammo = safeNumber(gunner, "GetNWAmmo", -1)
    local overheated = false

    if isfunction(gunner.GetNWOverheated) then
        local heatOk, value = pcall(gunner.GetNWOverheated, gunner)
        overheated = not heatOk or value == true
    end

    return true, clip > 0 and not overheated, clip, ammo, overheated
end

local function isMuzzleAligned(base, host, target)
    target = target or lastAim
    if not target then return false, nil end

    local _, muzzleAngle = getMuzzle(base, host)
    if not isangle(muzzleAngle) then return false, nil end

    local tolerance = tonumber(target.tolerance) or
        (istable(host) and tonumber(host.aim_tolerance_deg)) or 1.5
    local yawError = math.abs(math.AngleDifference(muzzleAngle.y, target.yaw))
    local pitchError = math.abs(math.AngleDifference(muzzleAngle.p, target.pitch))

    return yawError <= tolerance and pitchError <= tolerance, muzzleAngle
end

local function readGravity()
    local ok, gravityVector = pcall(function() return physenv.GetGravity() end)
    if ok and isvector(gravityVector) and math.abs(gravityVector.z) > 0 then
        return math.abs(gravityVector.z)
    end

    local gravityConVar = GetConVar("sv_gravity")
    return gravityConVar and math.max(1, gravityConVar:GetFloat()) or 600
end

local function writeState(host)
    local base, seat, ply = getLVSContext()
    local inVehicle = IsValid(base) and IsValid(seat)
    local muzzlePosition, muzzleAngle, attachmentName
    if inVehicle then
        muzzlePosition, muzzleAngle, attachmentName = getMuzzle(base, host)
    end

    local eye = IsValid(ply) and ply:EyeAngles() or angle_zero
    local eyePosition = IsValid(ply) and ply:EyePos() or vector_origin
    local hasGunner, gunnerReady, clip, ammo, overheated =
        weaponReadiness(seat, ply)
    local aimAligned = inVehicle and isMuzzleAligned(base, host) or false

    local state = {
        protocol = PROTOCOL,
        session = host.session,
        heartbeat = os.time(),
        map = game.GetMap(),
        in_vehicle = inVehicle,
        is_lvs = inVehicle and base.LVS == true,
        vehicle_class = inVehicle and base:GetClass() or "",
        seat_index = IsValid(seat) and isfunction(seat.lvsGetPodIndex)
            and seat:lvsGetPodIndex() or -1,
        eye = { x = eye.p, y = eye.y, z = eye.r },
        eye_position = { x = eyePosition.x, y = eyePosition.y, z = eyePosition.z },
        muzzle_valid = isvector(muzzlePosition) and isangle(muzzleAngle),
        muzzle_attachment = attachmentName or "",
        muzzle = isvector(muzzlePosition) and {
            x = muzzlePosition.x, y = muzzlePosition.y, z = muzzlePosition.z
        } or {},
        muzzle_angle = isangle(muzzleAngle) and {
            pitch = muzzleAngle.p, yaw = muzzleAngle.y, roll = muzzleAngle.r
        } or {},
        turret_yaw = inVehicle and safeNumber(base, "GetTurretYaw", 0) or 0,
        turret_pitch = inVehicle and safeNumber(base, "GetTurretPitch", 0) or 0,
        projectile_speed = 4000,
        gravity = readGravity(),
        gunner_present = hasGunner,
        gunner_ready = gunnerReady,
        fire_ready = hasGunner and gunnerReady and aimAligned and not overheated,
        aim_aligned = aimAligned,
        ammo_clip = clip,
        ammo_reserve = ammo,
        overheated = overheated,
        aim_active = aimCommand ~= nil,
        last_command_seq = acknowledgedSequence,
        command_ack = commandAck,
        command_message = commandMessage
    }

    if writeJson(TEMP_FILE, state) then
        file.Delete(STATE_FILE)
        file.Rename(TEMP_FILE, STATE_FILE)
    end
end

local function processCommand()
    local host = readJson(HOST_FILE)
    local hostAlive = istable(host) and host.protocol == PROTOCOL and
        host.app_running == true and isstring(host.session) and
        os.time() - (tonumber(host.heartbeat) or 0) <= 3

    if not hostAlive then
        aimCommand = nil
        lastAim = nil
        firePulse = false
        attackPulsePending = false
        attackPulseExpires = 0
        return nil
    end

    if host.session ~= activeSession then
        activeSession = host.session
        lastSequence = 0
        acknowledgedSequence = 0
        aimCommand = nil
        lastAim = nil
        firePulse = false
        attackPulsePending = false
        attackPulseExpires = 0
        commandAck = "connected"
        commandMessage = "TFC desktop session connected"
    end

    local command = readJson(COMMAND_FILE)
    if not istable(command) or command.protocol ~= PROTOCOL or
       command.session ~= activeSession then
        return host
    end

    local sequence = tonumber(command.seq) or 0
    if sequence <= lastSequence then return host end
    lastSequence = sequence

    local base, seat, ply = getLVSContext()
    local kind = command.type

    if kind == "aim" then
        if not IsValid(base) or not IsValid(seat) or base.LVS ~= true then
            commandAck = "aim_rejected"
            commandMessage = "Current seat is not a supported LVS vehicle"
        else
            local yaw = tonumber(command.world_yaw)
            local pitch = tonumber(command.view_pitch)
            if not yaw or not pitch then
                commandAck = "aim_rejected"
                commandMessage = "Aim angles are missing"
            else
                local tolerance = tonumber(host.aim_tolerance_deg) or 1.5
                local step = tonumber(host.aim_step_deg_per_tick) or 2.0
                lastAim = { yaw = yaw, pitch = pitch, tolerance = tolerance }
                aimCommand = {
                    yaw = yaw,
                    pitch = pitch,
                    step = math.Clamp(step, 0.1, 10.0)
                }
                aimExpires = CurTime() + 20
                commandAck = "aim_started"
                commandMessage = "Driving LVS toward target; checking actual muzzle angle"
            end
        end
    elseif kind == "fire" then
        local hasGunner, ready, clip, _, heat = weaponReadiness(seat, ply)
        local requested = {
            yaw = tonumber(command.world_yaw),
            pitch = tonumber(command.view_pitch),
            tolerance = tonumber(host.aim_tolerance_deg) or 1.5
        }
        local aligned = requested.yaw and requested.pitch and
            isMuzzleAligned(base, host, requested)

        if not IsValid(base) or base.LVS ~= true or not hasGunner or
           not ready or clip <= 0 or heat or not aligned then
            commandAck = "fire_rejected"
            commandMessage = "Blocked: physical muzzle alignment or LVS weapon readiness not confirmed"
        else
            firePulse = true
            commandAck = "fire_queued"
            commandMessage = "One LVS ATTACK pulse queued"
        end
    elseif kind == "cancel" then
        aimCommand = nil
        lastAim = nil
        firePulse = false
        attackPulsePending = false
        attackPulseExpires = 0
        commandAck = "cancelled"
        commandMessage = "Aim cancelled"
    else
        commandAck = "ignored"
        commandMessage = "Unknown command type"
    end

    acknowledgedSequence = sequence
    return host
end

hook.Add("CreateMove", "TFC_Bridge_CreateMove", function(cmd)
    if aimCommand then
        if CurTime() > aimExpires then
            aimCommand = nil
            commandAck = "aim_timeout"
            commandMessage = "Aim timed out"
        else
            local current = cmd:GetViewAngles()
            cmd:SetViewAngles(Angle(
                math.ApproachAngle(current.p, aimCommand.pitch, aimCommand.step),
                math.ApproachAngle(current.y, aimCommand.yaw, aimCommand.step),
                0
            ))

            local base, _, ply = getLVSContext()
            local host = readJson(HOST_FILE)
            local aligned = isMuzzleAligned(base, host)
            if aligned then
                aimCommand = nil
                commandAck = "muzzle_aligned"
                commandMessage = "Actual muzzle attachment angle is within tolerance"
            end
        end
    end

    if firePulse then
        firePulse = false
        attackPulsePending = true
        attackPulseExpires = CurTime() + 0.15
        commandAck = "fire_pulse_sent"
        commandMessage = "One-shot LVS ATTACK pulse armed"
    end
end)

-- Standard LVS action identifier is ATTACK. This overrides the LVS key-state
-- hook for a short single pulse rather than holding the regular mouse button.
hook.Add("LVS.PlayerKeyDown", "TFC_Bridge_AttackPulse", function(ply, actionName)
    if ply == LocalPlayer() and actionName == "ATTACK" and
       attackPulsePending and CurTime() <= attackPulseExpires then
        attackPulsePending = false -- consume exactly once
        return true
    end
end)

timer.Create("TFC_Bridge_Poll", 0.10, 0, function()
    local host = processCommand()
    if attackPulsePending and CurTime() > attackPulseExpires then
        attackPulsePending = false
        commandAck = "fire_pulse_expired"
        commandMessage = "LVS did not consume the queued ATTACK pulse"
    end
    if not istable(host) then return end

    if CurTime() - lastStateWrite >= 0.25 then
        lastStateWrite = CurTime()
        writeState(host)
    end
end)

MsgC(Color(100, 190, 145),
    "[TFC] Lua bridge ready; waiting for desktop SUO.\n")
