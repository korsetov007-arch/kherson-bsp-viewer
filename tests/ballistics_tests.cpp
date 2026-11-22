#include "ballistics.hpp"
#include <cassert>
#include <cmath>
int main(){auto s=tfc::solveLowArc({0,0,0},{1000,0,0},4000,600);assert(s&&std::isfinite(s->viewPitchDeg)&&s->flightTimeSec>0);assert(!tfc::solveLowArc({0,0,0},{1000000,0,0},100,600));assert(!tfc::solveLowArc({0,0,0},{100,0,0},0,600));return 0;}
