#include "ballistics.hpp"
#include <algorithm>
#include <cmath>
namespace tfc {
std::optional<BallisticSolution> solveLowArc(const Vec3&a,const Vec3&b,double v,double g){
 constexpr double pi=3.14159265358979323846;
 if(!std::isfinite(v)||!std::isfinite(g)||v<=0||g<=0)return std::nullopt;
 double dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z,h=std::hypot(dx,dy),yaw=std::atan2(dy,dx)*180/pi;
 if(h<1e-6)return std::nullopt;
 double v2=v*v,disc=v2*v2-g*(g*h*h+2*dz*v2);if(!std::isfinite(disc)||disc<0)return std::nullopt;
 double theta=std::atan((v2-std::sqrt(std::max(0.0,disc)))/(g*h)),c=std::cos(theta);if(c<=1e-8)return std::nullopt;
 double t=h/(v*c);if(!std::isfinite(t)||t<=0)return std::nullopt;double elev=theta*180/pi;
 return BallisticSolution{yaw,-elev,t,elev};
}}
