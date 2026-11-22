-- TFC bridge v1.2
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
local serverFireAck = "idle"
local serverFireMessage = "No server fire request yet"
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
        if ok and IsValid(result) and isfunction(result.GetActiveWeapon) then
            return result
        end
    end

    if IsValid(ply) and isfunction(ply.lvsGetWeaponHandler) then
        local ok, result = pcall(ply.lvsGetWeaponHandler, ply)
        if ok and IsValid(result) and isfunction(result.GetActiveWeapon) then
            return result
        end
    end

    return NULL
end

local function getMuzzle(base, host)
    if not IsValid(base) then return nil, nil, nil end

    local configured = istable(host) and host.muzzle_attachment or "auto"
    local attachmentNames = {}
    if isstring(configured) and configured ~= "" and configured ~= "auto" then
        attachmentNames[1] = configured
    else
        local preferred = base.TurretBallisticsMuzzleAttachment
        if isstring(preferred) and preferred ~= "" then
            attachmentNames[#attachmentNames + 1] = preferred
        end
        attachmentNames[#attachmentNames + 1] = "muzzle"
        attachmentNames[#attachmentNames + 1] = "muzzle_2"
        attachmentNames[#attachmentNames + 1] = "muzzle_1"
        for i = 3, 12 do
            attachmentNames[#attachmentNames + 1] = "muzzle_" .. i
        end
    end

    for _, attachmentName in ipairs(attachmentNames) do
        local ok, index = pcall(base.LookupAttachment, base, attachmentName)
        if ok and isnumber(index) and index > 0 then
            local got, attachment = pcall(base.GetAttachment, base, index)
            if got and istable(attachment) and
               isvector(attachment.Pos) and isangle(attachment.Ang) then
                return attachment.Pos, attachment.Ang, attachmentName
            end
        end
    end

    return nil, nil, attachmentNames[1]
end

local function weaponReadiness(seat, ply)
    local gunner = getGunner(seat, ply)
    if not IsValid(gunner) or not isfunction(gunner.GetActiveWeapon) then
        return false, false, -1, -1, true, false, "Weapon handler unavailable"
    end

    local ok, activeWeapon = pcall(gunner.GetActiveWeapon, gunner)
    if not ok or not istable(activeWeapon) or table.IsEmpty(activeWeapon) then
        return false, false, -1, -1, true, false, "No active LVS weapon"
    end

    -- LVS GetClip() is a heat-derived display value and can be -1 for
    -- unlimited-ammo weapons. It must not be used as the ammo-ready test.
    local clip = safeNumber(gunner, "GetClip", -1)
    local maxAmmo = safeNumber(gunner, "GetMaxAmmo", -1)
    local ammo = safeNumber(gunner, "GetNWAmmo", -1)
    local ammoLimited = maxAmmo > 0

    local overheated = false
    if isfunction(gunner.GetNWOverheated) then
        local heatOk, value = pcall(gunner.GetNWOverheated, gunner)
        overheated = not heatOk or value == true
    elseif isfunction(gunner.GetOverheated) then
        local heatOk, value = pcall(gunner.GetOverheated, gunner)
        overheated = not heatOk or value == true
    end

    local clipConfigured = isnumber(activeWeapon.Clip) and activeWeapon.Clip > 0
    local ammoReady = not ammoLimited or ammo > 0
    local clipRelevant = ammoLimited or clipConfigured
    local clipReady = not clipRelevant or clip > 0
    local ready = ammoReady and clipReady and not overheated
    local reason = overheated and "Weapon overheated" or
        (not ammoReady and "Out of ammo" or
         (not clipReady and "Weapon clip/heat capacity depleted" or "Weapon ready"))
    return true, ready, clip, ammo, overheated, ammoLimited, reason, clipRelevant
end

local function isMuzzleAligned(base, host, target)
    target = target or lastAim
    if not target then return false, nil end

    local _, muzzleAngle = getMuzzle(base, host)
    if not isangle(muzzleAngle) then return false, nil end

    local tolerance = tonumber(target.tolerance) or
        (istable(host) and tonumber(host.aim_tolerance_deg)) or 1.5
    local desiredForward = Angle(target.pitch, target.yaw, 0):Forward()
    local actualForward = muzzleAngle:Forward()
    local cosine = math.Clamp(desiredForward:Dot(actualForward), -1, 1)
    local errorDegrees = math.deg(math.acos(cosine))

    return errorDegrees <= tolerance, muzzleAngle, errorDegrees
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
    local eyeForward = IsValid(ply) and ply:EyeAngles():Forward() or vector_origin
    local aimForward = IsValid(ply) and ply:GetAimVector() or vector_origin
    local playerAim = aimForward:Angle()
    local eyePosition = IsValid(ply) and ply:EyePos() or vector_origin
    local playerPosition = IsValid(ply) and ply:GetPos() or vector_origin
    local vehiclePosition = inVehicle and base:GetPos() or vector_origin
    local vehicleAngles = inVehicle and base:GetAngles() or angle_zero

    local viewOrigin = eyePosition
    local viewAttachment = ""
    if inVehicle then
        local viewName = isstring(base.TurretBallisticsViewAttachment)
            and base.TurretBallisticsViewAttachment or "sight"
        local lookupOk, viewIndex = pcall(base.LookupAttachment, base, viewName)
        if lookupOk and isnumber(viewIndex) and viewIndex > 0 then
            local attachmentOk, attachment = pcall(base.GetAttachment, base, viewIndex)
            if attachmentOk and istable(attachment) and isvector(attachment.Pos) then
                viewOrigin = attachment.Pos
                viewAttachment = viewName
            end
        end
    end

    local zoomHeld = false
    if IsValid(ply) and isfunction(ply.lvsKeyDown) then
        local zoomOk, value = pcall(ply.lvsKeyDown, ply, "ZOOM")
        zoomHeld = zoomOk and value == true
    end
    local thirdPerson = false
    if IsValid(seat) and isfunction(seat.GetThirdPersonMode) then
        local thirdOk, value = pcall(seat.GetThirdPersonMode, seat)
        thirdPerson = thirdOk and value == true
    end
    local predictedBallistics = inVehicle and base.TurretBallisticsPredicted == true
    local zoomOnly = inVehicle and base.OpticsZoomOnly == true
    local uprightZ = inVehicle and base:GetUp().z or 1
    local uprightThreshold = inVehicle and tonumber(base.TurretBallisticsUpright) or 0.6
    local lvsBallisticsActive = predictedBallistics and uprightZ >= uprightThreshold
        and not thirdPerson and (not zoomOnly or zoomHeld)
    local lvsBallisticsVelocity = inVehicle
        and tonumber(base.TurretBallisticsProjectileVelocity) or 0
    local hasGunner, gunnerReady, clip, ammo, overheated, ammoLimited, weaponReason, clipRelevant =
        weaponReadiness(seat, ply)
    local aimAligned, muzzleError = false, nil
    if inVehicle then
        local aligned, _, errorDegrees = isMuzzleAligned(base, host)
        aimAligned = aligned
        muzzleError = errorDegrees
    end

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
        eye_angles = { pitch = eye.p, yaw = eye.y, roll = eye.r },
        view_forward = { x = eyeForward.x, y = eyeForward.y, z = eyeForward.z },
        player_aim_angles = { pitch = playerAim.p, yaw = playerAim.y, roll = playerAim.r },
        eye_position = { x = eyePosition.x, y = eyePosition.y, z = eyePosition.z },
        view_origin = { x = viewOrigin.x, y = viewOrigin.y, z = viewOrigin.z },
        view_attachment = viewAttachment,
        player_position = {
            x = playerPosition.x, y = playerPosition.y, z = playerPosition.z
        },
        vehicle_position = {
            x = vehiclePosition.x, y = vehiclePosition.y, z = vehiclePosition.z
        },
        vehicle_angles = {
            pitch = vehicleAngles.p, yaw = vehicleAngles.y, roll = vehicleAngles.r
        },
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
        turret_ballistics_predicted = predictedBallistics,
        turret_ballistics_active = lvsBallisticsActive,
        turret_ballistics_velocity = lvsBallisticsVelocity,
        turret_optics_zoom_only = zoomOnly,
        zoom_held = zoomHeld,
        third_person_mode = thirdPerson,
        turret_upright_z = uprightZ,
        gravity = readGravity(),
        gunner_present = hasGunner,
        gunner_ready = gunnerReady,
        weapon_reason = weaponReason or "",
        ammo_limited = ammoLimited,
        clip_relevant = clipRelevant,
        fire_ready = inVehicle and hasGunner and gunnerReady and isvector(muzzlePosition)
            and aimAligned and not overheated,
        aim_aligned = aimAligned,
        muzzle_error_deg = muzzleError,
        ammo_clip = clip,
        ammo_reserve = ammo,
        overheated = overheated,
        server_fire_ack = serverFireAck,
        server_fire_message = serverFireMessage,
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
        serverFireAck = "disconnected"
        serverFireMessage = "Desktop heartbeat expired"
        return nil
    end

    if host.session ~= activeSession then
        activeSession = host.session
        lastSequence = 0
        acknowledgedSequence = 0
        aimCommand = nil
        lastAim = nil
        serverFireAck = "idle"
        serverFireMessage = "Server fire channel ready"
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
                lastAim = {
                    yaw = tonumber(command.muzzle_yaw) or yaw,
                    pitch = tonumber(command.muzzle_pitch) or pitch,
                    tolerance = tolerance
                }
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
        local hasGunner, ready, _, _, heat, _, reason = weaponReadiness(seat, ply)
        local requested = {
            yaw = tonumber(command.muzzle_yaw) or tonumber(command.world_yaw),
            pitch = tonumber(command.muzzle_pitch) or tonumber(command.view_pitch),
            tolerance = tonumber(host.aim_tolerance_deg) or 1.5
        }
        local aligned = requested.yaw and requested.pitch and
            isMuzzleAligned(base, host, requested)

        if not IsValid(base) or base.LVS ~= true or not hasGunner or
           not ready or heat or not aligned then
            commandAck = "fire_rejected"
            commandMessage = not hasGunner and "No active LVS weapon handler" or
                (not ready and (reason or "Weapon is not ready") or
                 "Blocked: actual muzzle is not aligned")
        else
            local sent, sendError = pcall(function()
                net.Start("tfc_bridge_fire_request")
                net.SendToServer()
            end)
            if sent then
                serverFireAck = "request_sent"
                serverFireMessage = "Fire request sent to the LVS server"
                commandAck = "fire_sent_to_server"
                commandMessage = serverFireMessage
            else
                serverFireAck = "request_failed"
                serverFireMessage = tostring(sendError)
                commandAck = "fire_rejected"
                commandMessage = "Server fire channel unavailable: " .. tostring(sendError)
            end
        end
    elseif kind == "cancel" then
        aimCommand = nil
        lastAim = nil
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

            local base = getLVSContext()
            local host = readJson(HOST_FILE)
            local aligned = isMuzzleAligned(base, host)
            if aligned then
                aimCommand = nil
                commandAck = "muzzle_aligned"
                commandMessage = "Actual muzzle attachment angle is within tolerance"
            end
        end
    end

end)

net.Receive("tfc_bridge_fire_ack", function()
    local accepted = net.ReadBool()
    local message = net.ReadString()
    serverFireAck = accepted and "server_accepted" or "server_rejected"
    serverFireMessage = message
    commandAck = accepted and "fire_server_accepted" or "fire_server_rejected"
    commandMessage = message
end)

timer.Create("TFC_Bridge_Poll", 0.10, 0, function()
    local host = processCommand()
    if not istable(host) then return end

    if CurTime() - lastStateWrite >= 0.25 then
        lastStateWrite = CurTime()
        writeState(host)
    end
end)

MsgC(Color(100, 190, 145),
    "[TFC] Lua bridge ready; waiting for desktop SUO.\n")
