if not CLIENT then return end

local DATA_DIR = "source_bsp_geometry_lab"
local AIM_PATH = DATA_DIR .. "/aim.json"
local TELEMETRY_PATH = DATA_DIR .. "/telemetry.json"
local POLL_INTERVAL = 0.10
local AIM_MAX_AGE = 3

file.CreateDir(DATA_DIR)

local aimState = nil
local telemetryWriteAt = 0
local aimReadAt = 0
local aimLastReadOk = false
local hudEnabled = CreateClientConVar(
    "sbgl_hud_enabled", "1", true, false,
    "Show the Source BSP Geometry Lab live aiming marker"
)

local function vectorTable(v)
    return { x = v.x, y = v.y, z = v.z }
end

local function correctedViewAngles(ply)
    local eye = ply:EyeAngles()
    -- EyeAngles may be vehicle-local in some Source vehicles.
    if ply:InVehicle() then
        local vehicle = ply:GetVehicle()
        if IsValid(vehicle) then
            return vehicle:LocalToWorldAngles(eye)
        end
    end
    return eye
end

local function readAimState()
    local raw = file.Read(AIM_PATH, "DATA")
    if not raw or raw == "" then
        aimState = nil
        aimLastReadOk = false
        return
    end

    local parsed = util.JSONToTable(raw)
    if not istable(parsed) then
        aimState = nil
        aimLastReadOk = false
        return
    end

    aimState = parsed
    aimLastReadOk = true
end

local function writeTelemetry()
    local ply = LocalPlayer()
    if not IsValid(ply) then return end

    local view = correctedViewAngles(ply)
    local aimVector = ply:GetAimVector()
    local aimAngles = aimVector:Angle()
    local shootPos = ply:GetShootPos()

    local payload = {
        version = 1,
        timestamp = os.time(),
        map = game.GetMap() or "",
        playerPos = vectorTable(ply:GetPos()),
        eyePos = vectorTable(ply:EyePos()),
        shootPos = vectorTable(shootPos),
        angles = { yaw = view.y, pitch = view.p },
        aimAngles = { yaw = aimAngles.y, pitch = aimAngles.p },
        aimVector = vectorTable(aimVector),
        inVehicle = ply:InVehicle()
    }

    file.Write(TELEMETRY_PATH, util.TableToJSON(payload, false) or "{}")
end

timer.Create("SBGL_LiveExchange", POLL_INTERVAL, 0, function()
    local now = RealTime()
    if now >= aimReadAt then
        aimReadAt = now + POLL_INTERVAL
        readAimState()
    end
    if now >= telemetryWriteAt then
        telemetryWriteAt = now + POLL_INTERVAL
        writeTelemetry()
    end
end)

concommand.Add("sbgl_hud_toggle", function()
    local nextValue = hudEnabled:GetBool() and "0" or "1"
    RunConsoleCommand("sbgl_hud_enabled", nextValue)
    chat.AddText(Color(90, 210, 255), "[SBGL] ",
        color_white, "Live marker ", nextValue == "1" and "enabled" or "disabled",
        ". Console variable: sbgl_hud_enabled")
end)

local function drawCross(x, y, color, size)
    surface.SetDrawColor(color)
    surface.DrawLine(x - size, y, x - 3, y)
    surface.DrawLine(x + 3, y, x + size, y)
    surface.DrawLine(x, y - size, x, y - 3)
    surface.DrawLine(x, y + 3, x, y + size)
    surface.DrawOutlinedRect(x - 7, y - 7, 14, 14, 1)
    surface.DrawCircle(x, y, 12, color.r, color.g, color.b, 235)
end

local function drawPanel(x, y, w, h, title)
    draw.RoundedBox(6, x, y, w, h, Color(12, 17, 24, 225))
    surface.SetDrawColor(65, 83, 100, 230)
    surface.DrawOutlinedRect(x, y, w, h, 1)
    draw.SimpleText(title, "DermaDefaultBold", x + 10, y + 8, Color(90, 210, 255), TEXT_ALIGN_LEFT, TEXT_ALIGN_TOP)
end

hook.Add("HUDPaint", "SBGL_LiveAimMarker", function()
    if not hudEnabled:GetBool() then return end

    if not aimLastReadOk or not istable(aimState) then
        drawPanel(20, ScrH() - 78, 310, 54, "SOURCE BSP GEOMETRY LAB")
        draw.SimpleText("Waiting for desktop app link…", "DermaDefault", 30, ScrH() - 48,
            Color(220, 225, 235), TEXT_ALIGN_LEFT, TEXT_ALIGN_TOP)
        return
    end

    if not aimState.active then return end
    local timestamp = tonumber(aimState.timestamp or 0) or 0
    if timestamp <= 0 or math.abs(os.time() - timestamp) > AIM_MAX_AGE then
        drawPanel(20, ScrH() - 78, 310, 54, "SOURCE BSP GEOMETRY LAB")
        draw.SimpleText("Aim data is stale — check desktop app.", "DermaDefault", 30, ScrH() - 48,
            Color(255, 185, 85), TEXT_ALIGN_LEFT, TEXT_ALIGN_TOP)
        return
    end

    local ply = LocalPlayer()
    if not IsValid(ply) then return end

    local desiredYaw = tonumber(aimState.desiredYaw)
    local desiredPitch = tonumber(aimState.desiredPitch)
    if not desiredYaw or not desiredPitch then return end

    local view = correctedViewAngles(ply)
    local deltaYaw = math.AngleDifference(desiredYaw, view.y)
    local deltaPitch = math.AngleDifference(desiredPitch, view.p)
    local sw, sh = ScrW(), ScrH()
    local cx, cy = sw * 0.5, sh * 0.5

    local hfov = math.Clamp(tonumber(ply:GetFOV()) or 90, 30, 120)
    local vfov = math.deg(2 * math.atan(math.tan(math.rad(hfov * 0.5)) * sh / sw))
    vfov = math.Clamp(vfov, 20, 120)

    local markerX, markerY
    if math.abs(deltaYaw) < 88 and math.abs(deltaPitch) < 88 then
        markerX = cx + math.tan(math.rad(deltaYaw)) / math.tan(math.rad(hfov * 0.5)) * cx
        markerY = cy + math.tan(math.rad(deltaPitch)) / math.tan(math.rad(vfov * 0.5)) * cy
    else
        -- If the required direction is behind the view, point toward the nearest screen edge.
        local dx = deltaYaw / math.max(hfov * 0.5, 1)
        local dy = deltaPitch / math.max(vfov * 0.5, 1)
        local scale = math.max(math.abs(dx), math.abs(dy), 0.001)
        markerX = cx + dx / scale * (cx - 34)
        markerY = cy + dy / scale * (cy - 52)
    end

    local margin = 32
    local offscreen = markerX < margin or markerX > sw - margin or markerY < margin or markerY > sh - margin
    markerX = math.Clamp(markerX, margin, sw - margin)
    markerY = math.Clamp(markerY, margin, sh - margin)

    local accent = offscreen and Color(255, 185, 75) or Color(90, 235, 180)
    surface.SetDrawColor(accent.r, accent.g, accent.b, 135)
    surface.DrawLine(cx, cy, markerX, markerY)
    drawCross(markerX, markerY, accent, offscreen and 13 or 10)

    local horizontal = math.abs(deltaYaw) < 0.5 and "CENTER"
        or (deltaYaw > 0 and "RIGHT" or "LEFT")
    local vertical = math.abs(deltaPitch) < 0.5 and "LEVEL"
        or (deltaPitch < 0 and "UP" or "DOWN")
    local direction = vertical .. " / " .. horizontal
    if offscreen then direction = "TURN " .. horizontal .. " · " .. vertical end

    local range = tonumber(aimState.range) or 0
    local elevation = tonumber(aimState.elevationDeg) or 0
    local flight = tonumber(aimState.flightSeconds) or 0
    local desiredMap = tostring(aimState.map or "")
    local panelW, panelH = 370, 106
    drawPanel(20, sh - panelH - 20, panelW, panelH, "SOURCE BSP GEOMETRY LAB  ·  LIVE")
    draw.SimpleText("Adjust view: " .. direction, "DermaDefaultBold", 30, sh - 86,
        accent, TEXT_ALIGN_LEFT, TEXT_ALIGN_TOP)
    draw.SimpleText(string.format("Δ yaw %+.1f°   Δ pitch %+.1f°", deltaYaw, deltaPitch),
        "DermaDefault", 30, sh - 66, color_white, TEXT_ALIGN_LEFT, TEXT_ALIGN_TOP)
    draw.SimpleText(string.format("Range %.0f   elevation %.1f°   flight %.2f s", range, elevation, flight),
        "DermaDefault", 30, sh - 48, color_white, TEXT_ALIGN_LEFT, TEXT_ALIGN_TOP)
    draw.SimpleText("Map: " .. (desiredMap ~= "" and desiredMap or (game.GetMap() or "unknown")),
        "DermaDefault", 30, sh - 30, Color(185, 196, 209), TEXT_ALIGN_LEFT, TEXT_ALIGN_TOP)
end)
