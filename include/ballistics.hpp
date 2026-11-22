#pragma once
#include <optional>
namespace tfc { struct Vec3 { double x=0,y=0,z=0; }; struct BallisticSolution { double worldYawDeg=0,viewPitchDeg=0,flightTimeSec=0,elevationDeg=0; }; std::optional<BallisticSolution> solveLowArc(const Vec3&,const Vec3&,double,double); }
