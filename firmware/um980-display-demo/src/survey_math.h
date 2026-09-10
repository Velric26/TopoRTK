#pragma once
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace survey {
constexpr double pi=3.14159265358979323846;
struct Position { double latitude,longitude,height; Position(double a=0,double b=0,double c=0):latitude(a),longitude(b),height(c){} };
struct Cartesian { double x,y,z; Cartesian(double a=0,double b=0,double c=0):x(a),y(b),z(c){} };
inline Cartesian ecef(Position p) {
  const double lat=p.latitude*pi/180, lon=p.longitude*pi/180;
  const double e2=6.6943799901413165e-3, n=6378137/std::sqrt(1-e2*std::sin(lat)*std::sin(lat));
  return {(n+p.height)*std::cos(lat)*std::cos(lon),(n+p.height)*std::cos(lat)*std::sin(lon),
          (n*(1-e2)+p.height)*std::sin(lat)};
}
inline Position geodetic(Cartesian p) {
  const double e2=6.6943799901413165e-3, r=std::hypot(p.x,p.y);
  double lat=std::atan2(p.z,r*(1-e2)), h=0;
  for (int i=0;i<12;++i) {
    const double n=6378137/std::sqrt(1-e2*std::sin(lat)*std::sin(lat));
    h=r/std::cos(lat)-n;
    lat=std::atan2(p.z,r*(1-e2*n/(n+h)));
  }
  return {lat*180/pi,std::atan2(p.y,p.x)*180/pi,h};
}
inline double distance(Position a,Position b) {
  const Cartesian x=ecef(a),y=ecef(b); return std::sqrt(std::pow(x.x-y.x,2)+std::pow(x.y-y.y,2)+std::pow(x.z-y.z,2));
}
// WGS84 UTM series, restricted to the standard zone (within 3 degrees of CM).
// No datum transformation or implicit grid-to-ground scaling is performed.
inline bool utm(Position p,int zone,bool south,double &east,double &north) {
  if (zone<1 || zone>60 || !std::isfinite(p.latitude) || !std::isfinite(p.longitude) ||
      p.latitude < -80 || p.latitude > 84 || (p.latitude<0)!=south) return false;
  const double central=zone*6-183, delta=p.longitude-central;
  if (std::abs(delta)>3.00000001) return false;
  const double lat=p.latitude*pi/180, a=6378137, e2=6.6943799901413165e-3, ep=e2/(1-e2), k=.9996;
  const double s=std::sin(lat),c=std::cos(lat),t=std::tan(lat), n=a/std::sqrt(1-e2*s*s);
  const double T=t*t,C=ep*c*c,A=c*delta*pi/180;
  const double M=a*((1-e2/4-3*e2*e2/64-5*e2*e2*e2/256)*lat
    -(3*e2/8+3*e2*e2/32+45*e2*e2*e2/1024)*std::sin(2*lat)
    +(15*e2*e2/256+45*e2*e2*e2/1024)*std::sin(4*lat)
    -35*e2*e2*e2/3072*std::sin(6*lat));
  east=500000+k*n*(A+(1-T+C)*std::pow(A,3)/6+(5-18*T+T*T+72*C-58*ep)*std::pow(A,5)/120);
  north=(south?10000000:0)+k*(M+n*t*(A*A/2+(5-T+9*C+4*C*C)*std::pow(A,4)/24+
                         (61-58*T+T*T+600*C-330*ep)*std::pow(A,6)/720));
  return std::isfinite(east)&&std::isfinite(north);
}
inline uint64_t bits(const uint8_t *p,int start,int count) {
  uint64_t v=0; for (int i=0;i<count;++i) v=(v<<1)|((p[(start+i)/8]>>(7-(start+i)%8))&1); return v;
}
inline double signed38(const uint8_t *p,int offset) {
  uint64_t u=bits(p,offset,38); int64_t s=(u&(1ULL<<37)) ? int64_t(u)-int64_t(1ULL<<38) : int64_t(u);
  return double(s)*.0001;
}
inline bool reference_station(const uint8_t *frame,size_t length,Cartesian &position,uint16_t &station) {
  if (length<25) return false; const uint8_t *p=frame+3;
  const auto type=bits(p,0,12); if (type!=1005 && type!=1006) return false;
  station=uint16_t(bits(p,12,12)); position={signed38(p,34),signed38(p,74),signed38(p,114)};
  const double radius=std::sqrt(position.x*position.x+position.y*position.y+position.z*position.z);
  return radius>6000000 && radius<7000000;
}
}
