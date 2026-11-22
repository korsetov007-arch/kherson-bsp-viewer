-- TFC bridge v1. Install once at garrysmod/addons/tfc_bridge/lua/autorun/client/.
-- Built-in DATA filesystem only. No socket library, HTTP, DLL or external module.
if not CLIENT then return end
local D="tfc_suo";local HF=D.."/host.json";local CF=D.."/command.json";local SF=D.."/state.json";local TF=D.."/state.tmp.json";local P=1
local session=nil;local lastSeq=0;local ackSeq=0;local ack="idle";local message="Waiting for TFC";local aim=nil;local aimUntil=0;local firePulse=false;local attackUntil=0;local lastWrite=0;local lastAim=nil
file.CreateDir(D)
local function read(path)local raw=file.Read(path,"DATA");if not isstring(raw) or raw=="" then return nil end;local ok,v=pcall(util.JSONToTable,raw);if ok and istable(v) then return v end end
local function write(path,t)local s=util.TableToJSON(t,false);if not isstring(s) then return false end;file.Write(path,s);return true end
local function num(e,m,d)if not IsValid(e) or not isfunction(e[m]) then return d end;local ok,v=pcall(e[m],e);if ok and isnumber(v) then return v end;return d end
local function context()
 local p=LocalPlayer();if not IsValid(p) then return nil,nil,nil end;local seat=p:GetVehicle();if not IsValid(seat) then return nil,nil,p end
 local base=NULL;if isfunction(p.lvsGetVehicle) then local ok,v=pcall(p.lvsGetVehicle,p);if ok and IsValid(v) then base=v end end
 if not IsValid(base) and isfunction(seat.lvsGetVehicle) then local ok,v=pcall(seat.lvsGetVehicle,seat);if ok and IsValid(v) then base=v end end
 return base,seat,p
end
local function gunner(seat,p)
 if IsValid(seat) and isfunction(seat.lvsGetWeapon) then local ok,v=pcall(seat.lvsGetWeapon,seat);if ok and IsValid(v) then return v end end
 if IsValid(p) and isfunction(p.lvsGetWeaponHandler) then local ok,v=pcall(p.lvsGetWeaponHandler,p);if ok and IsValid(v) and v.LVS_GUNNER then return v end end
 return NULL
end
local function muzzle(base)
 if not IsValid(base) then return nil end;local name=base.TurretBallisticsMuzzleAttachment or "muzzle";local id=base:LookupAttachment(name);if not isnumber(id) or id<=0 then return nil end
 local a=base:GetAttachment(id);if istable(a) and isvector(a.Pos) then return a.Pos end
end
local function weaponReady(seat,p)
 local g=gunner(seat,p);if not IsValid(g) or not isfunction(g.GetActiveWeapon) then return false,false,-1,-1,true end
 local ok,w=pcall(g.GetActiveWeapon,g);if not ok or not istable(w) or table.IsEmpty(w) then return false,false,-1,-1,true end
 local clip=num(g,"GetClip",-1);local ammo=num(g,"GetNWAmmo",-1);local heat=false
 if isfunction(g.GetNWOverheated) then local oh,v=pcall(g.GetNWOverheated,g);heat=not oh or v==true end
 return true,clip>0 and not heat,clip,ammo,heat
end
local function aligned(p)
 if not IsValid(p) or not lastAim then return false end;local a=p:EyeAngles()
 return math.abs(math.AngleDifference(a.y,lastAim.yaw))<=1.5 and math.abs(math.AngleDifference(a.p,lastAim.pitch))<=1.5
end
local function gravity()
 local ok,v=pcall(function()return physenv.GetGravity()end);if ok and isvector(v) and math.abs(v.z)>0 then return math.abs(v.z) end
 local c=GetConVar("sv_gravity");return c and math.max(1,c:GetFloat()) or 600
end
local function writeState(h)
 local base,seat,p=context();local veh=IsValid(base) and IsValid(seat);local m=veh and muzzle(base) or nil
 local eye=IsValid(p) and p:EyeAngles() or angle_zero;local pos=IsValid(p) and p:EyePos() or vector_origin
 local has,ready,clip,ammo,heat=weaponReady(seat,p);local aimOk=aligned(p);local speed=4000
 if veh and isnumber(base.TurretBallisticsProjectileVelocity) and base.TurretBallisticsProjectileVelocity>0 then speed=base.TurretBallisticsProjectileVelocity end
 local s={protocol=P,session=h.session,heartbeat=os.time(),map=game.GetMap(),in_vehicle=veh,is_lvs=veh and base.LVS==true,vehicle_class=veh and base:GetClass() or "",
 seat_index=IsValid(seat) and isfunction(seat.lvsGetPodIndex) and seat:lvsGetPodIndex() or -1,
 eye={x=eye.p,y=eye.y,z=eye.r},eye_position={x=pos.x,y=pos.y,z=pos.z},muzzle_valid=isvector(m),muzzle=isvector(m) and {x=m.x,y=m.y,z=m.z} or {},
 turret_yaw=veh and num(base,"GetTurretYaw",0) or 0,turret_pitch=veh and num(base,"GetTurretPitch",0) or 0,projectile_speed=speed,gravity=gravity(),
 gunner_present=has,gunner_ready=ready,fire_ready=has and ready and aimOk,aim_aligned=aimOk,ammo_clip=clip,ammo_reserve=ammo,overheated=heat,aim_active=aim~=nil,last_command_seq=ackSeq,command_ack=ack,command_message=message}
 if write(TF,s) then file.Delete(SF);file.Rename(TF,SF) end
end
local function process()
 local h=read(HF);if not istable(h) or h.protocol~=P or h.app_running~=true or not isstring(h.session) or os.time()-(tonumber(h.heartbeat) or 0)>3 then aim=nil;firePulse=false;attackUntil=0;return end
 if h.session~=session then session=h.session;lastSeq=0;ackSeq=0;aim=nil;lastAim=nil;firePulse=false;attackUntil=0;ack="connected";message="TFC desktop session connected" end
 local c=read(CF);if not istable(c) or c.protocol~=P or c.session~=session then return end;local seq=tonumber(c.seq) or 0;if seq<=lastSeq then return end;lastSeq=seq
 local base,seat,p=context();local typ=c.type
 if typ=="aim" then
  if not IsValid(base) or not IsValid(seat) or base.LVS~=true then ack="aim_rejected";message="Current seat is not supported LVS"
  else local y=tonumber(c.world_yaw);local pitch=tonumber(c.view_pitch);if not y or not pitch then ack="aim_rejected";message="Aim angles missing" else aim={yaw=y,pitch=pitch};lastAim=aim;aimUntil=CurTime()+20;ack="aim_started";message="Applying requested player view angles" end end
 elseif typ=="fire" then
  local has,ready,clip,_,heat=weaponReady(seat,p);local y=tonumber(c.world_yaw);local pitch=tonumber(c.view_pitch);local a=IsValid(p) and p:EyeAngles()
  local okAim=a and y and pitch and math.abs(math.AngleDifference(a.y,y))<=1.5 and math.abs(math.AngleDifference(a.p,pitch))<=1.5
  if not IsValid(base) or base.LVS~=true or not has or not ready or clip<=0 or heat or not okAim then ack="fire_rejected";message="Blocked: LVS readiness and target view alignment not confirmed"
  else firePulse=true;lastAim={yaw=y,pitch=pitch};ack="fire_queued";message="One ATTACK pulse queued" end
 elseif typ=="cancel" then aim=nil;firePulse=false;attackUntil=0;ack="cancelled";message="Aim cancelled"
 else ack="ignored";message="Unknown command type" end
 ackSeq=seq
end
hook.Add("CreateMove","TFC_Bridge_CreateMove",function(cmd)
 if aim then if CurTime()>aimUntil then aim=nil;ack="aim_timeout";message="Aim timed out" else local a=cmd:GetViewAngles();cmd:SetViewAngles(Angle(math.ApproachAngle(a.p,aim.pitch,2),math.ApproachAngle(a.y,aim.yaw,2),0));if aligned(LocalPlayer()) then aim=nil;ack="aim_view_aligned";message="View angles reached target; verify physical turret alignment" end end end
 if firePulse then firePulse=false;attackUntil=CurTime()+math.max(FrameTime(),.02);ack="fire_pulse_sent";message="One-frame LVS ATTACK pulse sent" end
end)
hook.Add("LVS.PlayerKeyDown","TFC_Bridge_AttackPulse",function(p,name,pressed)if p==LocalPlayer() and name=="ATTACK" and attackUntil>CurTime() then return true end end)
timer.Create("TFC_Bridge_Poll",.10,0,function()
 process();local h=read(HF);if not istable(h) or h.app_running~=true or not isstring(h.session) or os.time()-(tonumber(h.heartbeat) or 0)>3 then return end
 if CurTime()-lastWrite>=.25 then lastWrite=CurTime();writeState(h) end
end)
MsgC(Color(100,190,145),"[TFC] Lua bridge ready; waiting for desktop SUO.\n")
