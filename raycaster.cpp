// Hyperbolic Rogue -- raycaster
// Copyright (C) 2011-2019 Zeno Rogue, see 'hyper.cpp' for details

/** \file raycaster.cpp
 *  \brief A raycaster to draw walls.
 */

#include "hyper.h"

namespace hr {

/** \brief raycaster */
EX namespace ray {

#if CAP_RAY

/** texture IDs */
GLuint txConnections = 0, txWallcolor = 0, txTextureMap = 0, txVolumetric = 0;

EX bool in_use;
EX bool comparison_mode;

/** 0 - never use, 2 - always use, 1 = smart selection */
EX int want_use = 1;

/** generate the map for raycasting just once */
EX bool fixed_map = false;

EX ld exp_start = 1;
EX ld exp_decay_exp = 4;
EX ld exp_decay_poly = 10;

#ifdef GLES_ONLY
const int gms_limit = 16; /* enough for Bringris -- need to do better */
#else
const int gms_limit = 110;
#endif

EX int gms_array_size = 16;

EX ld maxstep_sol = .05;
EX ld maxstep_nil = .1;
EX ld maxstep_pro = .5;
EX ld minstep = .001;

EX ld reflect_val = 0;

static const int NO_LIMIT = 999999;

EX ld hard_limit = NO_LIMIT;

EX int max_iter_sol = 600;
EX int max_iter_iso = 60;
EX int max_iter_eyes = 200;

EX int max_cells = 2048;
EX bool rays_generate = true;

EX ld& exp_decay_current() {
  if(fake::in()) return *FPIU(&exp_decay_current());
  return (sn::in() || hyperbolic || sl2) ? exp_decay_exp : exp_decay_poly;
  }

EX int& max_iter_current() {
  if(nonisotropic || stretch::in()) return max_iter_sol;
  else if(is_eyes()) return max_iter_eyes;
  else return max_iter_iso;
  }

EX bool is_eyes() {
  #if CAP_VR
  return vrhr::active() && vrhr::eyes == vrhr::eEyes::equidistant;
  #else
  return false;
  #endif
  }

EX bool is_stepbased() {
  return nonisotropic || stretch::in() || is_eyes();
  }

ld& maxstep_current() {
  if(sn::in() || stretch::in()) return maxstep_sol;
  #if CAP_VR
  if(vrhr::active() && vrhr::eyes == vrhr::eEyes::equidistant)
    return maxstep_pro;
  #endif
  return maxstep_nil;
  }

#define IN_ODS 0

eGeometry last_geometry;

bool need_many_cell_types() {
  return isize(hybrid::gen_sample_list()) > 2;
  }

/** is the raycaster available? */
EX bool available() {
  #if CAP_VR
  /* would need a completely different implementation */
  if(vrhr::active() && vrhr::eyes == vrhr::eEyes::equidistant) {
    if(reflect_val) return false;
    if(sol || stretch::in() || sl2) return false;
    }
  #endif
  if(noGUI) return false;
  if(!vid.usingGL) return false;
  if(GDIM == 2) return false;
  if(WDIM == 2 && (kite::in() || bt::in())) return false;
  #ifdef GLES_ONLY
  if(need_many_cell_types()) return false;
  #endif
  if(hyperbolic && pmodel == mdPerspective && !kite::in())
    return true;
  if(sphere && pmodel == mdPerspective && !rotspace)
    return true;
  if(nil && S7 == 8)
    return false;
  if((sn::in() || nil || sl2) && pmodel == mdGeodesic)
    return true;
  if(euclid && pmodel == mdPerspective && !bt::in())
    return true;
  if(prod)
    return true;
  if(pmodel == mdPerspective && stretch::in())
    return true;
  return false;
  }

/** do we want to use the raycaster? */
EX bool requested() {
  if(cgflags & qRAYONLY) return true;
  if(!want_use) return false;
  if(stretch::in() && sphere) return true;
  #if CAP_TEXTURE
  if(texture::config.tstate == texture::tsActive) return false;
  #endif
  if(!available()) return false;
  if(want_use == 2) return true;
  if(rotspace) return false; // not very good
  return racing::on || quotient || fake::in();
  }

#if HDR
struct raycaster : glhr::GLprogram {
  GLint uStart, uStartid, uM, uLength, uIPD;
  GLint uWallstart, uWallX, uWallY;
  GLint tConnections, tWallcolor, tTextureMap, tVolumetric;
  GLint uBinaryWidth, uPLevel, uLP, uStraighten, uReflectX, uReflectY;
  GLint uLinearSightRange, uExpStart, uExpDecay;
  GLint uBLevel;
  GLint uWallOffset, uSides;
  GLint uITOA, uATOI;
  GLint uToOrig, uFromOrig;
  GLint uProjection;
  GLint uEyeShift, uAbsUnit;
  
  raycaster(string vsh, string fsh);
  };
#endif

raycaster::raycaster(string vsh, string fsh) : GLprogram(vsh, fsh) {
    println(hlog, "assigning");
    uStart = glGetUniformLocation(_program, "uStart");
    uStartid = glGetUniformLocation(_program, "uStartid");
    uM = glGetUniformLocation(_program, "uM");
    uLength = glGetUniformLocation(_program, "uLength");
    uProjection = glGetUniformLocation(_program, "uProjection");
    uIPD = glGetUniformLocation(_program, "uIPD");

    uWallstart = glGetUniformLocation(_program, "uWallstart");
    uWallX = glGetUniformLocation(_program, "uWallX");
    uWallY = glGetUniformLocation(_program, "uWallY");
    
    uBinaryWidth = glGetUniformLocation(_program, "uBinaryWidth");
    uStraighten =  glGetUniformLocation(_program, "uStraighten");
    uPLevel = glGetUniformLocation(_program, "uPLevel");
    uLP = glGetUniformLocation(_program, "uLP");
    uReflectX = glGetUniformLocation(_program, "uReflectX");
    uReflectY = glGetUniformLocation(_program, "uReflectY");

    uLinearSightRange = glGetUniformLocation(_program, "uLinearSightRange");
    uExpDecay = glGetUniformLocation(_program, "uExpDecay");
    uExpStart = glGetUniformLocation(_program, "uExpStart");

    uBLevel = glGetUniformLocation(_program, "uBLevel");
  
    tConnections = glGetUniformLocation(_program, "tConnections");
    tWallcolor = glGetUniformLocation(_program, "tWallcolor");
    tTextureMap = glGetUniformLocation(_program, "tTextureMap");
    tVolumetric = glGetUniformLocation(_program, "tVolumetric");
    
    uWallOffset = glGetUniformLocation(_program, "uWallOffset");
    uSides = glGetUniformLocation(_program, "uSides");
    
    uITOA = glGetUniformLocation(_program, "uITOA");
    uATOI = glGetUniformLocation(_program, "uATOI");
    uToOrig = glGetUniformLocation(_program, "uToOrig");
    uFromOrig = glGetUniformLocation(_program, "uFromOrig");

    uEyeShift = glGetUniformLocation(_program, "uEyeShift");
    uAbsUnit  = glGetUniformLocation(_program, "uAbsUnit");
    }

shared_ptr<raycaster> our_raycaster;

int deg, irays;

#ifdef GLES_ONLY
void add(string& tgt, string type, string name, int min_index, int max_index) {
  if(min_index >= max_index) ;
  else
  if(min_index + 1 == max_index)
    tgt += "{ return " + name + "[" + its(min_index) + "]; }";
  else {
    int mid = (min_index + max_index) / 2;
    tgt += "{ if(i<" + its(mid) + ") "; 
    add(tgt, type, name, min_index, mid); 
    tgt += " else ";
    add(tgt, type, name, mid, max_index); 
    tgt += " }";
    }
  }

string build_getter(string type, string name, int index) {
  string s = type + " get_" + name + "(int i) \n";
  add(s, type, name, 0, index);
  return s + "\n";
  }

#define GET(array, index) "get_" array "(" index ")"
#else
#define GET(array, index) array "[" index "]"
#endif

EX hookset<void(string&, string&)> hooks_rayshader;
EX hookset<bool(shared_ptr<raycaster>)> hooks_rayset;

tuple<
  #if CAP_VR
  int, vrhr::eEyes, 
  #endif
  string
  > raycaster_state() {
  return make_tuple(
    #if CAP_VR
    vrhr::state, 
    vrhr::eyes,
    #endif
    cgi_string()
    );
  }

decltype(raycaster_state()) saved_state;

void enable_raycaster() {
  using glhr::to_glsl;
  auto state = raycaster_state();
  if(state != saved_state) {
    reset_raycaster();
    saved_state = state;
    }
  
  wall_offset(centerover); /* so raywall is not empty and deg is not zero */

  deg = 0;

  auto samples = hybrid::gen_sample_list();
  for(int i=0; i<isize(samples)-1; i++)
    deg = max(deg, samples[i+1].first - samples[i].first);
  
  last_geometry = geometry;
  if(!our_raycaster) { 
    bool asonov = hr::asonov::in();
    bool use_reflect = reflect_val && !nil && !levellines;
    
    bool many_cell_types = need_many_cell_types();
    
    string vsh = 
      "attribute mediump vec4 aPosition;\n"
      "uniform mediump mat4 uProjection;\n"
      "varying mediump vec4 at;\n"
      "void main() { \n"
      "  gl_Position = aPosition; at = uProjection * aPosition; \n"
      "  }\n";
  
    irays = isize(cgi.raywall);
    string rays = its(irays);
  
    string fsh = 
    "varying mediump vec4 at;\n"
    "uniform mediump int uLength;\n"
    "uniform mediump float uIPD;\n"
    "uniform mediump mat4 uStart;\n"
    "uniform mediump mat4 uM[" + its(gms_limit) + "];\n"
    "uniform mediump vec2 uStartid;\n"
    "uniform mediump sampler2D tConnections;\n"
    "uniform mediump sampler2D tWallcolor;\n"
    "uniform mediump sampler2D tVolumetric;\n"
    "uniform mediump sampler2D tTexture;\n"
    "uniform mediump sampler2D tTextureMap;\n"
    "uniform mediump vec4 uWallX["+rays+"];\n"
    "uniform mediump vec4 uWallY["+rays+"];\n"
    "uniform mediump vec4 uFogColor;\n"
    "uniform mediump int uWallstart["+its(isize(cgi.wallstart))+"];\n"
    "uniform mediump float uLinearSightRange, uExpStart, uExpDecay;\n";
    
    #ifdef GLES_ONLY
    fsh += build_getter("mediump vec4", "uWallX", irays);
    fsh += build_getter("mediump vec4", "uWallY", irays);
    fsh += build_getter("mediump int", "uWallstart", deg+1);
    fsh += build_getter("mediump mat4", "uM", gms_limit);
    #endif
    
    if(prod) fsh += 
      "uniform mediump float uPLevel;\n"
      "uniform mediump mat4 uLP;\n";
    
    if(many_cell_types) fsh += 
      "uniform int uWallOffset, uSides;\n";
    
    int flat1 = 0, flat2 = deg;
    if(prod || rotspace) flat2 -= 2;

#if CAP_BT
    if(hyperbolic && bt::in()) {
      fsh += "uniform mediump float uBLevel;\n";
      flat1 = bt::dirs_outer();
      flat2 -= bt::dirs_inner();
      }
#endif
    
    if(hyperbolic) fsh += 

    "mediump mat4 xpush(float x) { return mat4("
         "cosh(x), 0., 0., sinh(x),\n"
         "0., 1., 0., 0.,\n"
         "0., 0., 1., 0.,\n"
         "sinh(x), 0., 0., cosh(x)"
         ");}\n";

    if(sphere) fsh += 

    "mediump mat4 xpush(float x) { return mat4("
         "cos(x), 0., 0., sin(x),\n"
         "0., 1., 0., 0.,\n"
         "0., 0., 1., 0.,\n"
         "-sin(x), 0., 0., cos(x)"
         ");}\n";
        
    if(IN_ODS) fsh += 

    "mediump mat4 xzspin(float x) { return mat4("
         "cos(x), 0., sin(x), 0.,\n"
         "0., 1., 0., 0.,\n"
         "-sin(x), 0., cos(x), 0.,\n"
         "0., 0., 0., 1."
         ");}\n"
      
    "mediump mat4 yzspin(float x) { return mat4("
         "1., 0., 0., 0.,\n"
         "0., cos(x), sin(x), 0.,\n"
         "0., -sin(x), cos(x), 0.,\n"
         "0., 0., 0., 1."
         ");}\n";    
    
   if(many_cell_types) {
     fsh += "int walloffset, sides;\n";
     }
   else {
     fsh += "const int walloffset = 0;\n"
       "const int sides = " + its(centerover->type+(WDIM == 2 ? 2 : 0)) + ";\n";
     }
     
    
   fsh += 
     "mediump vec2 map_texture(mediump vec4 pos, int which) {\n";
   if(nil) fsh += "if(which == 2 || which == 5) pos.z = 0.;\n";
   else if(hyperbolic && bt::in()) fsh += 
       "pos = vec4(-log(pos.w-pos.x), pos.y, pos.z, 1);\n"
       "pos.yz *= exp(pos.x);\n";
   else if(hyperbolic || sphere) fsh += 
       "pos /= pos.w;\n";
   else if(prod) fsh +=
     "pos = vec4(pos.x/pos.z, pos.y/pos.z, pos.w, 0);\n";
   
   fsh += 
       "int s = " GET("uWallstart", "which") ";\n"
       "int e = " GET("uWallstart", "which+1") ";\n"
       "for(int ix=0; ix<16; ix++) {\n"
         "int i = s+ix; if(i >= e) break;\n"
         "mediump vec2 v = vec2(dot(" GET("uWallX", "i") ", pos), dot(" GET("uWallY", "i") ", pos));\n"
         "if(v.x >= 0. && v.y >= 0. && v.x + v.y <= 1.) return vec2(v.x+v.y, v.x-v.y);\n"
         "}\n"
       "return vec2(1, 1);\n"
       "}\n";

   bool eyes = is_eyes();
   
   bool stepbased = is_stepbased();
    
   string fmain = "void main() {\n";
   
   if(use_reflect) fmain += "  bool depthtoset = true;\n";
    
    if(IN_ODS) fmain +=
    "  mediump float lambda = at[0];\n" // -PI to PI
    "  mediump float phi;\n"
    "  mediump float eye;\n"
    "  if(at.y < 0.) { phi = at.y + PI/2.; eye = uIPD / 2.; }\n" // right
    "  else { phi = at.y - PI/2.; eye = -uIPD / 2.; }\n"
    "  mediump mat4 vw = uStart * xzspin(-lambda) * xpush(eye) * yzspin(phi);\n"
    "  mediump vec4 at0 = vec4(0., 0., 1., 0.);\n";
    
    else {
      fmain += 
        "  mediump mat4 vw = uStart;\n"
        "  mediump vec4 at0 = at;\n"
        "  gl_FragColor = vec4(0,0,0,1);\n"
        "  mediump float left = 1.;\n"
        "  at0.y = -at.y;\n"
        "  at0.w = 0.;\n";
      
      if(panini_alpha) fmain += 
          "mediump float hr = at0.x*at0.x;\n"
          "mediump float alpha = " + to_glsl(panini_alpha) + ";\n"
          "mediump float A = 1. + hr;\n"
          "mediump float B = -2.*hr*alpha;\n"
          "mediump float C = 1. - hr*alpha*alpha;\n"
          "B /= A; C /= A;\n"
    
          "mediump float hz = B / 2. + sqrt(C + B*B/4.);\n"
          "if(abs(hz) > 1e-3) {"
          "at0.xyz *= hz+alpha;\n"
          "at0.z = hz;\n}"
          " else at0.z = 0.;\n"
          "\n"
          ;

      else if(stereo_alpha) fmain += 
          "mediump float hr = at0.x*at0.x+at0.y*at0.y;\n"
          "mediump float alpha = " + to_glsl(stereo_alpha) + ";\n"
          "mediump float A = 1. + hr;\n"
          "mediump float B = -2.*hr*alpha;\n"
          "mediump float C = 1. - hr*alpha*alpha;\n"
          "B /= A; C /= A;\n"
    
          "mediump float hz = B / 2. + sqrt(C + B*B/4.);\n"
          "if(abs(hz) > 1e-3) {"
          "at0.xyz *= hz+alpha;\n"
          "at0.z = hz;\n}"
          " else at0.z = 0.;\n"
          "\n"
          ;

      fmain +=
        "  at0.xyz = at0.xyz / length(at0.xyz);\n";   

      if(eyes) fmain += "  at0.xyz /= uAbsUnit;\n";
      }
      
    if(hyperbolic) fsh += "  mediump float len(mediump vec4 x) { return x[3]; }\n";
    else if(sphere && rotspace) fsh += "  mediump float len(mediump vec4 x) { return 1.+x.x*x.x+x.y*x.y-x.z*x.z-x.w*x.w; }\n";
    else if(sl2) fsh += "  mediump float len(mediump vec4 x) { return 1.+x.x*x.x+x.y*x.y; }\n";
    else if(sphere) fsh += "  mediump float len(mediump vec4 x) { return 1.-x[3]; }\n";

    else fsh += "  mediump float len(mediump vec4 x) { return length(x.xyz); }\n";
    
    ld s = 1;
    #if CAP_VR
    if(eyes) s *= vrhr::absolute_unit_in_meters;
    #endif
    
    if(stepbased) fmain += 
      "  const mediump float maxstep = " + fts(maxstep_current() * s) + ";\n"
      "  const mediump float minstep = " + fts(minstep * s) + ";\n"
      "  mediump float next = maxstep;\n";          
    
    if(prod) {
      string sgn=in_h2xe() ? "-" : "+";
      fmain +=     
      "  mediump vec4 position = vw * vec4(0., 0., 1., 0.);\n"
      "  mediump vec4 at1 = uLP * at0;\n";
      if(in_e2xe()) fmain +=
      "  mediump float zpos = log(position.z);\n";
      else fmain +=
      "  mediump float zpos = log(position.z*position.z"+sgn+"position.x*position.x"+sgn+"position.y*position.y)/2.;\n";
      if(eyes) fmain +=
      "  vw *= exp(-zpos);\n";
      else fmain +=
      "  position *= exp(-zpos);\n"
      "  mediump float zspeed = at1.z;\n"
      "  mediump float xspeed = length(at1.xy);\n"
      "  mediump vec4 tangent = vw * exp(-zpos) * vec4(at1.xy, 0, 0) / xspeed;\n";
      }
    else if(!eyes) {
      fmain +=
        "  mediump vec4 position = vw * vec4(0., 0., 0., 1.);\n"
        "  mediump vec4 tangent = vw * at0;\n";
      }
    
    if(eyes) {
      fsh += "mediump uniform mat4 uEyeShift;\n";
      fsh += "mediump uniform float uAbsUnit;\n";
      }
      
    if(stretch::in()) {
      if(stretch::mstretch) {
        fsh += "mediump uniform mat4 uITOA;\n";
        fsh += "mediump uniform mat4 uATOI;\n";
        fsh += "mediump uniform mat4 uToOrig;\n";
        fsh += "mediump uniform mat4 uFromOrig;\n";
        fsh += "mediump mat4 toOrig;\n";
        fsh += "mediump mat4 fromOrig;\n";
        fmain +=
          "toOrig = uToOrig;\n"
          "fromOrig = uFromOrig;\n";
        fmain += 
          "tangent = s_itranslate(toOrig * position) * toOrig * tangent;\n";
        fmain += 
          "tangent = uITOA * tangent;\n";
        fmain += 
          "tangent = fromOrig * s_translate(toOrig * position) * tangent;\n";
        }
      else {
        fmain += 
          "tangent = s_itranslate(position) * tangent;\n";
        fmain +=
          "tangent[2] /= " + to_glsl(stretch::not_squared()) + ";\n";
        fmain +=
          "tangent = s_translate(position) * tangent;\n";
        }
      }
    
    if(many_cell_types) fmain += "  walloffset = uWallOffset; sides = uSides;\n";
    
    fmain +=     
      "  mediump float go = 0.;\n"
      "  mediump vec2 cid = uStartid;\n"
      "  for(int iter=0; iter<" + its(max_iter_current()) + "; iter++) {\n";
    
    fmain +=
      "  mediump float dist = 100.;\n";
    
    fmain +=
      "  int which = -1;\n";
    
    if(in_e2xe() && !eyes) fmain += "tangent.w = position.w = 0.;\n";
      
    if(IN_ODS) fmain += 
      "  if(go == 0.) {\n"
      "    mediump float best = len(position);\n"
      "    for(int i=0; i<sides; i++) {\n"
      "      mediump float cand = len(uM[i] * position);\n"
      "      if(cand < best - .001) { dist = 0.; best = cand; which = i; }\n"
      "      }\n"
      "    }\n";
    
    if(!stepbased) {
    
      fmain +=
        "  if(which == -1) {\n";
      
      fmain += "for(int i="+its(flat1)+"; i<"+(prod ? "sides-2" : WDIM == 2 ? "sides" : its(flat2))+"; i++) {\n";
      
      // fmain += "int woi = walloffset+i;\n";
      
      if(in_h2xe()) fmain +=
          "    mediump float v = ((position - uM[woi] * position)[2] / (uM[woi] * tangent - tangent)[2]);\n"
          "    if(v > 1. || v < -1.) continue;\n"
          "    mediump float d = atanh(v);\n"
          "    mediump vec4 next_tangent = position * sinh(d) + tangent * cosh(d);\n"
          "    if(next_tangent[2] < (uM[woi] * next_tangent)[2]) continue;\n"
          "    d /= xspeed;\n";
      else if(in_s2xe()) fmain +=
          "    mediump float v = ((position - uM[woi] * position)[2] / (uM[woi] * tangent - tangent)[2]);\n"
          "    mediump float d = atan(v);\n"
          "    mediump vec4 next_tangent = tangent * cos(d) - position * sin(d);\n"
          "    if(next_tangent[2] > (uM[woi] * next_tangent)[2]) continue;\n"
          "    d /= xspeed;\n";
      else if(in_e2xe()) fmain +=
          "    mediump float deno = dot(position, tangent) - dot(uM[woi]*position, uM[woi]*tangent);\n"
          "    if(deno < 1e-6  && deno > -1e-6) continue;\n"
          "    mediump float d = (dot(uM[woi]*position, uM[woi]*position) - dot(position, position)) / 2. / deno;\n"
          "    if(d < 0.) continue;\n"
          "    mediump vec4 next_position = position + d * tangent;\n"
          "    if(dot(next_position, tangent) < dot(uM[woi]*next_position, uM[woi]*tangent)) continue;\n"
          "    d /= xspeed;\n";
      else if(hyperbolic) fmain +=
          "    mediump float v = ((position - uM[woi] * position)[3] / (uM[woi] * tangent - tangent)[3]);\n"
          "    if(v > 1. || v < -1.) continue;\n"
          "    mediump float d = atanh(v);\n"
          "    mediump vec4 next_tangent = position * sinh(d) + tangent * cosh(d);\n"
          "    if(next_tangent[3] < (uM[woi] * next_tangent)[3]) continue;\n";
      else if(sphere) fmain +=
          "    mediump float v = ((position - uM[woi] * position)[3] / (uM[woi] * tangent - tangent)[3]);\n"
          "    mediump float d = atan(v);\n"
          "    mediump vec4 next_tangent = -position * sin(d) + tangent * cos(d);\n"
          "    if(next_tangent[3] > (uM[woi] * next_tangent)[3]) continue;\n";
      else fmain += 
          "    mediump float deno = dot(position, tangent) - dot(uM[woi]*position, uM[woi]*tangent);\n"
          "    if(deno < 1e-6  && deno > -1e-6) continue;\n"
          "    mediump float d = (dot(uM[woi]*position, uM[woi]*position) - dot(position, position)) / 2. / deno;\n"
          "    if(d < 0.) continue;\n"
          "    mediump vec4 next_position = position + d * tangent;\n"
          "    if(dot(next_position, tangent) < dot(uM[woi]*next_position, uM[woi]*tangent)) continue;\n";
      
      replace_str(fmain, "[woi]", "[walloffset+i]");
  
      fmain += 
          "  if(d < dist) { dist = d; which = i; }\n"
            "}\n";

      if(hyperbolic && reg3::ultra_mirror_in()) {
        fmain += "for(int i="+its(S7*2)+"; i<"+its(S7*2+isize(cgi.vertices_only))+"; i++) {\n";
        fmain +=
          "    mediump float v = ((position - uM[i] * position)[3] / (uM[i] * tangent - tangent)[3]);\n"
          "    if(v > 1. || v < -1.) continue;\n"
          "    mediump float d = atanh(v);\n"
          "    mediump vec4 next_tangent = position * sinh(d) + tangent * cosh(d);\n"
          "    if(next_tangent[3] < (uM[i] * next_tangent)[3]) continue;\n"
          "    if(d < dist) { dist = d; which = i; }\n"
            "}\n";
        }
      
    
      // 20: get to horosphere +uBLevel (take smaller root)
      // 21: get to horosphere -uBLevel (take larger root)
                          
      if(hyperbolic && bt::in()) {
        fmain += 
          "for(int i=20; i<22; i++) {\n"
            "mediump float sgn = i == 20 ? -1. : 1.;\n"
            "mediump vec4 zpos = xpush(uBLevel*sgn) * position;\n"
            "mediump vec4 ztan = xpush(uBLevel*sgn) * tangent;\n"
            "mediump float Mp = zpos.w - zpos.x;\n"
            "mediump float Mt = ztan.w - ztan.x;\n"
            "mediump float a = (Mp*Mp-Mt*Mt);\n"
            "mediump float b = Mp/a;\n"
            "mediump float c = (1.+Mt*Mt) / a;\n"
            "if(b*b < c) continue;\n"
            "if(sgn < 0. && Mt > 0.) continue;\n"
            "mediump float zsgn = (Mt > 0. ? -sgn : sgn);\n"
            "mediump float u = sqrt(b*b-c)*zsgn + b;\n"
            "mediump float v = -(Mp*u-1.) / Mt;\n"
            "mediump float d = asinh(v);\n"
            "if(d < 0. && abs(log(position.w*position.w-position.x*position.x)) < uBLevel) continue;\n"
            "if(d < dist) { dist = d; which = i; }\n"
            "}\n";
        }
          
      if(prod) fmain += 
        "if(zspeed > 0.) { mediump float d = (uPLevel - zpos) / zspeed; if(d < dist) { dist = d; which = sides-1; }}\n"
        "if(zspeed < 0.) { mediump float d = (-uPLevel - zpos) / zspeed; if(d < dist) { dist = d; which = sides-2; }}\n";
      
      fmain += "}\n";

      fmain += 
        "  if(dist < 0.) { dist = 0.; }\n";
      
      fmain +=
        "  if(which == -1 && dist == 0.) return;";    
      }
    
    // shift d units
    if(use_reflect) fmain += 
      "bool reflect = false;\n";
      
    if(in_h2xe() && !stepbased) fmain +=
      "  mediump float ch = cosh(dist*xspeed); mediump float sh = sinh(dist*xspeed);\n"
      "  mediump vec4 v = position * ch + tangent * sh;\n"
      "  tangent = tangent * ch + position * sh;\n"
      "  position = v;\n"
      "  zpos += dist * zspeed;\n";
    else if(in_s2xe() && !stepbased) fmain +=
      "  mediump float ch = cos(dist*xspeed); mediump float sh = sin(dist*xspeed);\n"
      "  mediump vec4 v = position * ch + tangent * sh;\n"
      "  tangent = tangent * ch - position * sh;\n"
      "  position = v;\n"
      "  zpos += dist * zspeed;\n";
    else if(in_e2xe() && !stepbased) fmain +=
      "  position = position + tangent * dist * xspeed;\n"
      "  zpos += dist * zspeed;\n";
    else if(hyperbolic && !stepbased) fmain += 
      "  mediump float ch = cosh(dist); mediump float sh = sinh(dist);\n"
      "  mediump vec4 v = position * ch + tangent * sh;\n"
      "  tangent = tangent * ch + position * sh;\n"
      "  position = v;\n";
    else if(sphere && !stepbased) fmain += 
      "  mediump float ch = cos(dist); mediump float sh = sin(dist);\n"
      "  mediump vec4 v = position * ch + tangent * sh;\n"
      "  tangent = tangent * ch - position * sh;\n"
      "  position = v;\n";
    else if(stepbased) {
    
      bool use_christoffel = true;
    
      if(sol && nih) fsh += 
        "mediump vec4 christoffel(mediump vec4 pos, mediump vec4 vel, mediump vec4 tra) {\n"
        "  return vec4(-(vel.z*tra.x + vel.x*tra.z)*log(2.), (vel.z*tra.y + vel.y * tra.z)*log(3.), vel.x*tra.x * exp(2.*log(2.)*pos.z)*log(2.) - vel.y * tra.y * exp(-2.*log(3.)*pos.z)*log(3.), 0.);\n"
        "  }\n";
      else if(nih) fsh += 
        "mediump vec4 christoffel(mediump vec4 pos, mediump vec4 vel, mediump vec4 tra) {\n"
        "  return vec4((vel.z*tra.x + vel.x*tra.z)*log(2.), (vel.z*tra.y + vel.y * tra.z)*log(3.), -vel.x*tra.x * exp(-2.*log(2.)*pos.z)*log(2.) - vel.y * tra.y * exp(-2.*log(3.)*pos.z)*log(3.), 0.);\n"
        "  }\n";
      else if(sol) fsh += 
        "mediump vec4 christoffel(mediump vec4 pos, mediump vec4 vel, mediump vec4 tra) {\n"
        "  return vec4(-vel.z*tra.x - vel.x*tra.z, vel.z*tra.y + vel.y * tra.z, vel.x*tra.x * exp(2.*pos.z) - vel.y * tra.y * exp(-2.*pos.z), 0.);\n"
        "  }\n";
      else if(nil) {
        fsh +=
        "mediump vec4 christoffel(mediump vec4 pos, mediump vec4 vel, mediump vec4 tra) {\n"
        "  mediump float x = pos.x;\n"
        "  return vec4(x*vel.y*tra.y - 0.5*dot(vel.yz,tra.zy), -.5*x*dot(vel.yx,tra.xy) + .5 * dot(vel.zx,tra.xz), -.5*(x*x-1.)*dot(vel.yx,tra.xy)+.5*x*dot(vel.zx,tra.xz), 0.);\n"
//        "  return vec4(0.,0.,0.,0.);\n"
        "  }\n";
        use_christoffel = false;
        }
      else if(sl2 || stretch::in()) {
        if(sl2) {
          fsh += "mediump mat4 s_translate(vec4 h) {\n"
            "return mat4(h.w,h.z,h.y,h.x,-h.z,h.w,-h.x,h.y,h.y,-h.x,h.w,-h.z,h.x,h.y,h.z,h.w);\n"
            "}\n";
          }
        else {
          fsh += "mediump mat4 s_translate(vec4 h) {\n"
            "return mat4(h.w,h.z,-h.y,-h.x,-h.z,h.w,h.x,-h.y,h.y,-h.x,h.w,-h.z,h.x,h.y,h.z,h.w);\n"
            "}\n";
          }
        fsh += "mediump mat4 s_itranslate(vec4 h) {\n"
          "h.xyz = -h.xyz; return s_translate(h);\n"
          "}\n";
        if(stretch::mstretch) {
          fsh += "mediump vec4 christoffel(mediump vec4 pos, mediump vec4 vel, mediump vec4 tra) {\n"
            "vel = s_itranslate(toOrig * pos) * toOrig * vel;\n"
            "tra = s_itranslate(toOrig * pos) * toOrig * tra;\n"
            "return fromOrig * s_translate(toOrig * pos) * vec4(\n";
          
          for(int i=0; i<3; i++) {
            auto &c = stretch::ms_christoffel;
            fsh += "  0.";
            for(int j=0; j<3; j++) 
            for(int k=0; k<3; k++) 
              if(c[i][j][k])
                fsh += "  + vel["+its(j)+"]*tra["+its(k)+"]*" + to_glsl(c[i][j][k]);
            fsh += "  ,\n";
            }
          fsh += "  0);\n"
            "}\n";
          }
        else
          use_christoffel = false;
        }
      else use_christoffel = false;

      if(use_christoffel) fsh += "mediump vec4 get_acc(mediump vec4 pos, mediump vec4 vel) {\n"
        "  return christoffel(pos, vel, vel);\n"
        "  }\n";
      
      if(sn::in() && !asonov) fsh += "uniform mediump float uBinaryWidth;\n";
      
      fmain += 
        "  dist = next < minstep ? 2.*next : next;\n";

      if(nil && !use_christoffel) fsh += 
        "mediump vec4 translate(mediump vec4 a, mediump vec4 b) {\n"
          "return vec4(a[0] + b[0], a[1] + b[1], a[2] + b[2] + a[0] * b[1], b[3]);\n"
          "}\n"
        "mediump vec4 translatev(mediump vec4 a, mediump vec4 t) {\n"
          "return vec4(t[0], t[1], t[2] + a[0] * t[1], 0.);\n"
          "}\n"
        "mediump vec4 itranslate(mediump vec4 a, mediump vec4 b) {\n"
          "return vec4(-a[0] + b[0], -a[1] + b[1], -a[2] + b[2] - a[0] * (b[1]-a[1]), b[3]);\n"
          "}\n"
        "mediump vec4 itranslatev(mediump vec4 a, mediump vec4 t) {\n"
          "return vec4(t[0], t[1], t[2] - a[0] * t[1], 0.);\n"
          "}\n";
                
      // if(nil) fmain += "tangent = translate(position, itranslate(position, tangent));\n";
      
      if(use_christoffel) fmain +=
        "mediump vec4 vel = tangent * dist;\n"
        "mediump vec4 acc1 = get_acc(position, vel);\n"
        "mediump vec4 acc2 = get_acc(position + vel / 2., vel + acc1/2.);\n"
        "mediump vec4 acc3 = get_acc(position + vel / 2. + acc1/4., vel + acc2/2.);\n"
        "mediump vec4 acc4 = get_acc(position + vel + acc2/2., vel + acc3/2.);\n"
        "mediump vec4 nposition = position + vel + (acc1+acc2+acc3)/6.;\n";

      if((sl2 || stretch::in()) && use_christoffel) {
        if(sl2) fmain += 
          "nposition = nposition / sqrt(dot(position.zw, position.zw) - dot(nposition.xy, nposition.xy));\n";

        else if(stretch::in()) fmain += 
          "nposition = nposition / sqrt(dot(nposition, nposition));\n";
        }
      
      if((sl2 || stretch::in()) && !use_christoffel) {
        ld SV = stretch::not_squared();
        ld mul = (sphere?1:-1)-1/SV/SV;
        fmain += 
          "vec4 vel = s_itranslate(position) * tangent * dist;\n"
          "vec4 vel1 = vel; vel1.z *= " + to_glsl(stretch::not_squared()) + ";\n"
          "mediump float vlen = length(vel1.xyz);\n"
          "if(vel.z<0.) vlen=-vlen;\n"
          "float z_part = vel1.z/vlen;\n"
          "float x_part = sqrt(1.-z_part*z_part);\n"
          "const float SV = " + to_glsl(SV) + ";\n"
          "float rparam = x_part / z_part / SV;\n"
          "float beta = atan2(vel.y,vel.x);\n"
          "if(vlen<0.) beta += PI;\n"
          "mediump vec4 nposition, ntangent;\n";
        
        if(sl2) fmain +=
          "if(rparam > 1.) {\n"
            "float cr = 1./sqrt(rparam*rparam-1.);\n"
            "float sr = rparam*cr;\n"
            "float z = cr * " + to_glsl(mul) + ";\n"
            "float a = vlen / length(vec2(sr, cr/SV));\n"
            "float k = -a;\n"
            "float u = z*a;\n"
            "float xy = sr * sinh(k);\n"
            "float zw = cr * sinh(k);\n"
            "nposition = vec4("
              "-xy*cos(u+beta),"
              "-xy*sin(u+beta),"
              "zw*cos(u)-cosh(k)*sin(u),"
              "zw*sin(u)+cosh(k)*cos(u)"
              ");\n"

            "ntangent = vec4("
              "-sr*cosh(k)*k*cos(u+beta) + u*xy*sin(u+beta),"
              "-sr*cosh(k)*k*sin(u+beta) - u*xy*cos(u+beta),"
              "k*cr*cosh(k)*cos(u)-zw*sin(u)*u-k*sinh(k)*sin(u)-u*cosh(k)*cos(u),"
              "k*cr*cosh(k)*sin(u)+u*zw*cos(u)+k*sinh(k)*cos(u)-u*cosh(k)*sin(u)"
              ");\n"
            "}\n"
          "else {\n"
            "float r = atanh(rparam);\n"
            "float cr = cosh(r);\n"
            "float sr = sinh(r);\n"
            "float z = cr * "+to_glsl(mul)+";\n"
            "float a = vlen / length(vec2(sr, cr/SV));\n"
            "float k = -a;\n"
            "float u = z*a;\n"
            "float xy = sr * sin(k);\n"
            "float zw = cr * sin(k);\n"
            "ntangent = vec4("
               "-sr*cos(k)*k*cos(u+beta) + u*xy*sin(u+beta),"
               "-sr*cos(k)*k*sin(u+beta) - u*xy*cos(u+beta),"
               "k*cr*cos(k)*cos(u)-zw*sin(u)*u+k*sin(k)*sin(u)-u*cos(k)*cos(u),"
               "k*cr*cos(k)*sin(u)+zw*cos(u)*u-k*sin(k)*cos(u)-u*cos(k)*sin(u)"
               ");\n"
            "nposition = vec4("
               "-xy * cos(u+beta),"
               "-xy * sin(u+beta),"
               "zw * cos(u) - cos(k) * sin(u),"
               "zw * sin(u) + cos(k)*cos(u)"
               ");\n"
            "}\n";
          
        else fmain += 
          "if(true) {\n"
            "float r = atan(rparam);\n"
            "float cr = cos(r);\n"
            "float sr = sin(r);\n"
            "float z = cr * "+to_glsl(mul)+";\n"
            "float a = vlen / length(vec2(sr, cr/SV));\n"
            "float k = a;\n"
            "float u = z*a;\n"
            "float xy = sr * sin(k);\n"
            "float zw = cr * sin(k);\n"
            "ntangent = vec4("
               "sr*cos(k)*k*cos(u+beta) - u*xy*sin(u+beta),"
               "sr*cos(k)*k*sin(u+beta) + u*xy*cos(u+beta),"
               "k*cr*cos(k)*cos(u)-zw*sin(u)*u+k*sin(k)*sin(u)-u*cos(k)*cos(u),"
               "k*cr*cos(k)*sin(u)+zw*cos(u)*u-k*sin(k)*cos(u)-u*cos(k)*sin(u)"
               ");\n"
            "nposition = vec4("
               "xy * cos(u+beta),"
               "xy * sin(u+beta),"
               "zw * cos(u) - cos(k) * sin(u),"
               "zw * sin(u) + cos(k)*cos(u)"
               ");\n"
            "}\n";          
        
        fmain +=    
          "ntangent = ntangent / dist;\n"
          "ntangent = s_translate(position) * ntangent;\n"
          "nposition = s_translate(position) * nposition;\n";
        }
      
      if(nil && !use_christoffel && !eyes) {
        fmain +=
          "mediump vec4 xp, xt;\n"
          "mediump vec4 back = itranslatev(position, tangent);\n"
          "if(back.x == 0. && back.y == 0.) {\n"
          "  xp = vec4(0., 0., back.z*dist, 1.);\n"
          "  xt = back;\n"
          "  }\n"
          "else if(abs(back.z) == 0.) {\n"
          "  xp = vec4(back.x*dist, back.y*dist, back.x*back.y*dist*dist/2., 1.);\n"
          "  xt = vec4(back.x, back.y, dist*back.x*back.y, 0.);\n"
          "  }\n"
          "else if(abs(back.z) < 1e-1) {\n"
// we use the midpoint method here, because the formulas below cause glitches due to mediump float precision
          "  mediump vec4 acc = christoffel(vec4(0,0,0,1), back, back);\n"
          "  mediump vec4 pos2 = back * dist / 2.;\n"
          "  mediump vec4 tan2 = back + acc * dist / 2.;\n"
          "  mediump vec4 acc2 = christoffel(pos2, tan2, tan2);\n"
          "  xp = vec4(0,0,0,1) + back * dist + acc2 / 2. * dist * dist;\n"
          "  xt = back + acc * dist;\n"
          "  }\n"
          "else {\n"
          "  mediump float alpha = atan2(back.y, back.x);\n"
          "  mediump float w = back.z * dist;\n"
          "  mediump float c = length(back.xy) / back.z;\n"
          "  xp = vec4(2.*c*sin(w/2.) * cos(w/2.+alpha), 2.*c*sin(w/2.)*sin(w/2.+alpha), w*(1.+(c*c/2.)*((1.-sin(w)/w)+(1.-cos(w))/w * sin(w+2.*alpha))), 1.);\n"
          "  xt = back.z * vec4("
               "c*cos(alpha+w),"
               "c*sin(alpha+w),"
               "1. + c*c*2.*sin(w/2.)*sin(alpha+w)*cos(alpha+w/2.),"
               "0.);\n"
          "  }\n"
          "mediump vec4 nposition = translate(position, xp);\n";
        }
      
      if(asonov) {
        fsh += "uniform mediump mat4 uStraighten;\n";
        fmain += "mediump vec4 sp = uStraighten * nposition;\n";
        }
      
      if(eyes) {
        fmain +=
        "  mediump float t = go + dist;\n";
        fmain += prod ? 
        "  mediump vec4 v = at1 * t;\n" :
        "  mediump vec4 v = at0 * t;\n";
        fmain +=
        "  v[3] = 1.;\n"
        "  mediump vec4 azeq = uEyeShift * v;\n";
        if(nil) fmain +=
          "  mediump float alpha = atan2(azeq.y, azeq.x);\n"
          "  mediump float w = azeq.z;\n"
          "  mediump float c = length(azeq.xy) / azeq.z;\n"
          "  mediump vec4 xp = vec4(2.*c*sin(w/2.) * cos(w/2.+alpha), 2.*c*sin(w/2.)*sin(w/2.+alpha), w*(1.+(c*c/2.)*((1.-sin(w)/w)+(1.-cos(w))/w * sin(w+2.*alpha))), 1.);\n"
          "  mediump vec4 orig_position = vw * vec4(0., 0., 0., 1.);\n"
          "  mediump vec4 nposition = translate(orig_position, xp);\n";
        else if(prod) {
          fmain +=
            "  mediump float alen_xy = length(azeq.xy);\n";
          fmain += "  mediump float nzpos = zpos + azeq.z;\n";
          if(in_h2xe()) {
            fmain += "  azeq.xy *= sinh(alen_xy) / alen_xy;\n";
            fmain += "  azeq.z = cosh(alen_xy);\n";
            }
          else if(in_s2xe()) {
            fmain += "  azeq.xy *= sin (alen_xy) / alen_xy;\n";
            fmain += "  azeq.z = cos(alen_xy);\n";
            }
          else {
            /* euclid */
            fmain += "  azeq.z = 1.;\n";
            }
          fmain += "azeq.w = 0.;\n";
          fmain +=
          "  mediump vec4 nposition = vw * azeq;\n";
          }
        else {
          fmain +=
            "  mediump float alen = length(azeq.xyz);\n";
          if(hyperbolic) fmain +=         
            "  azeq *= sinh(alen) / alen;\n"        
            "  azeq[3] = cosh(alen);\n";
          else if(sphere) fmain += 
            "  azeq *= sin(alen) / alen;\n"
            "  azeq[3] = cos(alen);\n";
          else /* euclid */ fmain +=
            "  azeq[3] = 1;\n";
          fmain +=
          "  mediump vec4 nposition = vw * azeq;\n";
          }
        }
      
      else if(hyperbolic) {
        fmain += 
        "  mediump float ch = cosh(dist); mediump float sh = sinh(dist);\n"
        "  mediump vec4 v = position * ch + tangent * sh;\n"
        "  mediump vec4 ntangent = tangent * ch + position * sh;\n"
        "  mediump vec4 nposition = v;\n";
        }

      else if(sphere && !stretch::in()) {
        fmain += 
        "  mediump float ch = cos(dist); mediump float sh = sin(dist);\n"
        "  mediump vec4 v = position * ch + tangent * sh;\n"
        "  mediump vec4 ntangent = tangent * ch - position * sh;\n"
        "  mediump vec4 nposition = v;\n";
        }

      bool reg = hyperbolic || sphere || euclid || sl2 || prod;

      if(reg) {
        fsh += "mediump float len_h(vec4 h) { return 1. - h[3]; }\n";
        string s = (rotspace || prod) ? "-2" : "";
        fmain +=
      "    mediump float best = len(nposition);\n"
      "    for(int i=0; i<sides"+s+"; i++) {\n"
      "      mediump float cand = len(uM[walloffset+i] * nposition);\n"
      "      if(cand < best) { best = cand; which = i; }\n"
      "      }\n";
        if(rotspace) fmain +=
      "   if(which == -1) {\n"
      "     best = len_h(nposition);\n"
      "     mediump float cand1 = len_h(uM[walloffset+sides-2]*nposition);\n"
      "     if(cand1 < best) { best = cand1; which = sides-2; }\n"
      "     mediump float cand2 = len_h(uM[walloffset+sides-1]*nposition);\n"
      "     if(cand2 < best) { best = cand2; which = sides-1; }\n"
      "     }\n";
        if(prod) {
          fmain +=
          "if(nzpos > uPLevel) which = sides-1;\n"
          "if(nzpos <-uPLevel) which = sides-2;\n";
          }
        }
        
      if(nil) fmain +=
        "mediump float rz = (abs(nposition.x) > abs(nposition.y) ?  -nposition.x*nposition.y : 0.) + nposition.z;\n";
      
      fmain +=
        "if(next >= minstep) {\n";
        
      string hnilw = to_glsl(nilv::nilwidth / 2);
      string hnilw2 = to_glsl(nilv::nilwidth * nilv::nilwidth / 2);
      
      if(reg) fmain += "if(which != -1) {\n";
      else if(asonov) fmain +=
          "if(abs(sp.x) > 1. || abs(sp.y) > 1. || abs(sp.z) > 1.) {\n";      
      else if(nih) fmain +=
          "if(abs(nposition.x) > uBinaryWidth || abs(nposition.y) > uBinaryWidth || abs(nposition.z) > .5) {\n";
      else if(sol) fmain +=
          "if(abs(nposition.x) > uBinaryWidth || abs(nposition.y) > uBinaryWidth || abs(nposition.z) > log(2.)/2.) {\n";
      else fmain +=
          "if(abs(nposition.x) > "+hnilw+" || abs(nposition.y) > "+hnilw+" || abs(rz) > "+hnilw2+") {\n";
      
      fmain +=
            "next = dist / 2.; continue;\n"
            "}\n"
          "if(next < maxstep) next = next / 2.;\n"
          "}\n"
        "else {\n";
      
      if(sn::in()) {
        if(asonov) fmain +=
          "if(sp.x > 1.) which = 4;\n"
          "if(sp.y > 1.) which = 5;\n"
          "if(sp.x <-1.) which = 10;\n"
          "if(sp.y <-1.) which = 11;\n"
          "if(sp.z > 1.) {\n"
            "mediump float best = 999.;\n"
            "for(int i=0; i<4; i++) {\n"
              "mediump float cand = len(uStraighten * uM[i] * position);\n"
              "if(cand < best) { best = cand; which = i;}\n"
              "}\n"
            "}\n"
          "if(sp.z < -1.) {\n"
            "mediump float best = 999.;\n"
            "for(int i=6; i<10; i++) {\n"
              "mediump float cand = len(uStraighten * uM[i] * position);\n"
              "if(cand < best) { best = cand; which = i;}\n"
              "}\n"
            "}\n";
        else if(sol && !nih) fmain +=
          "if(nposition.x > uBinaryWidth) which = 0;\n"
          "if(nposition.x <-uBinaryWidth) which = 4;\n"
          "if(nposition.y > uBinaryWidth) which = 1;\n"
          "if(nposition.y <-uBinaryWidth) which = 5;\n";
        if(nih) fmain += 
          "if(nposition.x > uBinaryWidth) which = 0;\n"
          "if(nposition.x <-uBinaryWidth) which = 2;\n"
          "if(nposition.y > uBinaryWidth) which = 1;\n"
          "if(nposition.y <-uBinaryWidth) which = 3;\n";
        if(sol && nih) fmain += 
          "if(nposition.z > .5) which = nposition.x > 0. ? 5 : 4;\n"
          "if(nposition.z <-.5) which = nposition.y > uBinaryWidth/3. ? 8 : nposition.y < -uBinaryWidth/3. ? 6 : 7;\n";
        if(nih && !sol) fmain += 
          "if(nposition.z > .5) which = 4;\n"
          "if(nposition.z < -.5) which = (nposition.y > uBinaryWidth/3. ? 9 : nposition.y < -uBinaryWidth/3. ? 5 : 7) + (nposition.x>0.?1:0);\n";
        if(sol && !nih && !asonov) fmain += 
          "if(nposition.z > log(2.)/2.) which = nposition.x > 0. ? 3 : 2;\n"
          "if(nposition.z <-log(2.)/2.) which = nposition.y > 0. ? 7 : 6;\n";
        }
      else if(nil) fmain +=
          "if(nposition.x > "+hnilw+") which = 3;\n"
          "if(nposition.x <-"+hnilw+") which = 0;\n"
          "if(nposition.y > "+hnilw+") which = 4;\n"
          "if(nposition.y <-"+hnilw+") which = 1;\n"
          "if(rz > "+hnilw2+") which = 5;\n"
          "if(rz <-"+hnilw2+") which = 2;\n";
      
      fmain += 
          "next = maxstep;\n"
          "}\n";
      
      if(use_christoffel) fmain +=
        "tangent = tangent + (acc1+2.*acc2+2.*acc3+acc4)/(6.*dist);\n";
      else if(nil && !eyes) fmain +=
        "tangent = translatev(position, xt);\n";
      else if(!eyes)
        fmain +=
        "tangent = ntangent;\n";

      if(!eyes) fmain +=
        "position = nposition;\n";
      else fmain += "vec4 position = nposition;\n";
      
      if((stretch::in() || sl2) && use_christoffel) {
        fmain += 
          "tangent = s_itranslate(toOrig * position) * toOrig * tangent;\n"
          "tangent[3] = 0.;\n";
        if(stretch::mstretch)
          fmain +=
            "float nvelsquared = dot(tangent.xyz, (uATOI * tangent).xyz);\n";
        else
          fmain +=
            "float nvelsquared = tangent.x * tangent.x + tangent.y * tangent.y + "
              + to_glsl(stretch::squared()) + " * tangent.z * tangent.z;\n";
        fmain +=      
          "tangent /= sqrt(nvelsquared);\n"
          "tangent = fromOrig * s_translate(toOrig * position) * tangent;\n";
        }
      }
    else fmain += 
      "position = position + tangent * dist;\n";
    
    if(!eyes) {
      if(hyperbolic) fmain +=
        "position /= sqrt(position.w*position.w - dot(position.xyz, position.xyz));\n"
        "tangent -= dot(vec4(-position.xyz, position.w), tangent) * position;\n"
        "tangent /= sqrt(dot(tangent.xyz, tangent.xyz) - tangent.w*tangent.w);\n";
      
      if(in_h2xe()) fmain +=
        "position /= sqrt(position.z*position.z - dot(position.xy, position.xy));\n"
        "tangent -= dot(vec3(-position.xy, position.z), tangent.xyz) * position;\n"
        "tangent /= sqrt(dot(tangent.xy, tangent.xy) - tangent.z*tangent.z);\n";
      }
    
    if(hyperbolic && bt::in()) {
      fmain += 
        "if(which == 20) {\n"
        "  mediump float best = 999.;\n"
        "  for(int i="+its(flat2)+"; i<"+its(S7)+"; i++) {\n"
          "  mediump float cand = len(uM[i] * position);\n"
          "  if(cand < best) { best = cand; which = i; }\n"
          "  }\n"
          "}\n"
        "if(which == 21) {\n"
          "mediump float best = 999.;\n"
          "for(int i=0; i<"+its(flat1)+"; i++) {\n"
          "  mediump float cand = len(uM[i] * position);\n"
          "  if(cand < best) { best = cand; which = i; }\n"
          "  }\n"
//          "gl_FragColor = vec4(.5 + .5 * sin((go+dist)*100.), 1, float(which)/3., 1); return;\n"
          "}\n";
      }
    
    if(volumetric::on) fmain += 
      "if(dist > 0. && go < " + to_glsl(hard_limit) + ") {\n"
      "   if(dist > "+to_glsl(hard_limit)+" - go) dist = "+to_glsl(hard_limit)+" - go;\n"
      "   mediump vec4 col = texture2D(tVolumetric, cid);\n"
      "   mediump float factor = col.w; col.w = 1.;\n"
      "   mediump float frac = exp(-(factor + 1. / uExpDecay) * dist);\n"
      "   gl_FragColor += left * (1.-frac) * col;\n"
      "   left *= frac;\n"
      "   }\n;";
        
    fmain += "  go = go + dist;\n";          

    fmain += "if(which == -1) continue;\n";

    if(prod && eyes) fmain += "position.w = -nzpos;\n";
    else if(prod) fmain += "position.w = -zpos;\n";
    
    if(reg3::ultra_mirror_in()) fmain += 
      "if(which >= " + its(S7) + ") {"
      "  tangent = uM[which] * tangent;\n"
      "  continue;\n"
      "  }\n";
      
    // apply wall color
    fmain +=
      "  mediump vec2 u = cid + vec2(float(which) / float(uLength), 0);\n"
      "  mediump vec4 col = texture2D(tWallcolor, u);\n"
      "  if(col[3] > 0.0) {\n";
    
    if(eyes)
      fmain += "    mediump float gou = go / uAbsUnit;\n";
    else
      fmain += "    mediump float gou = go;\n";
    
    if(hard_limit < NO_LIMIT)
      fmain += "    if(gou > " + to_glsl(hard_limit) + ") { gl_FragDepth = 1.; return; }\n";
    
    if(!(levellines && disable_texture)) fmain +=
      "    mediump vec2 inface = map_texture(position, which+walloffset);\n"
      "    mediump vec3 tmap = texture2D(tTextureMap, u).rgb;\n"
      "    if(tmap.z == 0.) col.xyz *= min(1., (1.-inface.x)/ tmap.x);\n"
      "    else {\n"
      "      mediump vec2 inface2 = tmap.xy + tmap.z * inface;\n"
      "      col.xyz *= texture2D(tTexture, inface2).rgb;\n"
      "      }\n";

    if(volumetric::on)
      fmain += "    mediump float d = uExpStart * exp(-gou / uExpDecay);\n";

    else
      fmain +=
      "    mediump float d = max(1. - gou / uLinearSightRange, uExpStart * exp(-gou / uExpDecay));\n";
    
    if(!volumetric::on) fmain +=
      "    col.xyz = col.xyz * d + uFogColor.xyz * (1.-d);\n";
    
    if(nil) fmain +=
      "    if(abs(abs(position.x)-abs(position.y)) < .005) col.xyz /= 2.;\n";
    
    if(use_reflect) fmain +=
      "  if(col.w == 1.) {\n"
      "    col.w = " + to_glsl(1-reflect_val)+";\n"
      "    reflect = true;\n"
      "    }\n";
    
    ld vnear = glhr::vnear_default;
    ld vfar = glhr::vfar_default;

    fmain +=
      "    gl_FragColor.xyz += left * col.xyz * col.w;\n";

    if(use_reflect) fmain +=
      "    if(reflect && depthtoset) {\n";
    else fmain +=
      "    if(col.w == 1.) {\n";
    
    if(hyperbolic && !eyes) fmain +=
      "      mediump vec4 t = at0 * sinh(go);\n";
    else fmain +=
      "      mediump vec4 t = at0 * go;\n";

    fmain += 
      "      t.w = 1.;\n";

    if(levellines) {
      if(hyperbolic && !eyes) 
        fmain += "gl_FragColor.xyz *= 0.5 + 0.5 * cos(z/cosh(go) * uLevelLines * 2. * PI);\n";
      else
        fmain += "gl_FragColor.xyz *= 0.5 + 0.5 * cos(z * uLevelLines * 2. * PI);\n";
      fsh += "uniform mediump float uLevelLines;\n";
      }
    
    if(panini_alpha) 
      fmain += panini_shader();

    else if(stereo_alpha) 
      fmain += stereo_shader();

    #ifndef GLES_ONLY
    fmain +=    
      "      gl_FragDepth = (" + to_glsl(-vnear-vfar)+"+t.w*" + to_glsl(2*vnear*vfar)+"/t.z)/" + to_glsl(vnear-vfar)+";\n"
      "      gl_FragDepth = (gl_FragDepth + 1.) / 2.;\n";
    #endif
    
    if(!use_reflect) fmain +=
      "      return;\n";
    else fmain +=
      "      depthtoset = false;\n";

    fmain +=    
      "      }\n"
      "    left *= (1. - col.w);\n"
      "    }\n";

    if(use_reflect) {
      if(prod) fmain += "if(reflect && which >= "+its(deg-2)+") { zspeed = -zspeed; continue; }\n";
      if(hyperbolic && bt::in()) fmain +=
        "if(reflect && (which < "+its(flat1)+" || which >= "+its(flat2)+")) {\n"
        "  mediump float x = -log(position.w - position.x);\n"
        "  mediump vec4 xtan = xpush(-x) * tangent;\n"
        "  mediump float diag = (position.y*position.y+position.z*position.z)/2.;\n"
        "  mediump vec4 normal = vec4(1.-diag, -position.y, -position.z, -diag);\n"
        "  mediump float mdot = dot(xtan.xyz, normal.xyz) - xtan.w * normal.w;\n"
        "  xtan = xtan - normal * mdot * 2.;\n"
        "  tangent = xpush(x) * xtan;\n"
        "  continue;\n"
        "  }\n";
      if(asonov) {
        fmain += 
          "  if(reflect) {\n"
          "    if(which == 4 || which == 10) tangent = refl(tangent, position.z, uReflectX);\n"
          "    else if(which == 5 || which == 11) tangent = refl(tangent, position.z, uReflectY);\n"
          "    else tangent.z = -tangent.z;\n"
          "    }\n";
        fsh += 
          "uniform mediump vec4 uReflectX, uReflectY;\n"
          "mediump vec4 refl(mediump vec4 t, float z, mediump vec4 r) {\n"
            "t.x *= exp(z); t.y /= exp(z);\n"
            "t -= dot(t, r) * r;\n"
            "t.x /= exp(z); t.y *= exp(z);\n"
            "return t;\n"
            "}\n";           
        }
      else if(sol && !nih && !asonov) fmain += 
        "  if(reflect) {\n"
        "    if(which == 0 || which == 4) tangent.x = -tangent.x;\n"
        "    else if(which == 1 || which == 5) tangent.y = -tangent.y;\n"
        "    else tangent.z = -tangent.z;\n"
        "    continue;\n"
        "    }\n";
      else if(nih) fmain += 
        "  if(reflect) {\n"
        "    if(which == 0 || which == 2) tangent.x = -tangent.x;\n"
        "    else if(which == 1 || which == 3) tangent.y = -tangent.y;\n"
        "    else tangent.z = -tangent.z;\n"
        "    continue;\n"
        "    }\n";
      else fmain += 
        "  if(reflect) {\n"
        "    tangent = uM["+its(deg)+"+which] * tangent;\n"
        "    continue;\n"
        "    }\n";
      }
    
    // next cell
    fmain += 
      "  mediump vec4 connection = texture2D(tConnections, u);\n"
      "  cid = connection.xy;\n";
    
    if(prod) fmain +=
      "  if(which == sides-2) { zpos += uPLevel+uPLevel; }\n"
      "  if(which == sides-1) { zpos -= uPLevel+uPLevel; }\n";
    
    fmain +=
      "  int mid = int(connection.z * 1024.);\n"
      "  mediump mat4 m = " GET("uM", "mid") " * " GET("uM", "walloffset+which") ";\n";
    
    if(eyes) 
      fmain += "  vw = m * vw;\n";
    
    else fmain +=
      "  position = m * position;\n"
      "  tangent = m * tangent;\n";
    
    if(stretch::mstretch) fmain += 
      "  m = s_itranslate(m*vec4(0,0,0,1)) * m;"
      "  fromOrig = m * fromOrig;\n"
      "  m[0][1] = -m[0][1]; m[1][0] = -m[1][0];\n" // inverse
      "  toOrig = toOrig * m;\n";
    
    if(many_cell_types) {
      fmain += 
        "walloffset = int(connection.w * 256.);\n"
        "sides = int(connection.w * 4096.) - 16 * walloffset;\n";
      
      // fmain += "if(sides != 8) { gl_FragColor = vec4(.5,float(sides)/8.,.5,1); return; }";
      }

    fmain += 
      "  }\n"
      "  gl_FragColor.xyz += left * uFogColor.xyz;\n";

    #ifndef GLES_ONLY
    if(use_reflect) fmain +=
      "  if(depthtoset) gl_FragDepth = 1.;\n";
    else fmain +=
      "  gl_FragDepth = 1.;\n";
    #endif

    fmain += 
      "  }";

    fsh += fmain;    

    callhooks(hooks_rayshader, vsh, fsh);
      
    our_raycaster = make_shared<raycaster> (vsh, fsh);
    }
  full_enable(our_raycaster);
  }

void bind_array(vector<array<float, 4>>& v, GLint t, GLuint& tx, int id, int length) {
  if(t == -1) println(hlog, "bind to nothing");
  glUniform1i(t, id);

  if(tx == 0) glGenTextures(1, &tx);

  glActiveTexture(GL_TEXTURE0 + id);
  GLERR("activeTexture");

  glBindTexture(GL_TEXTURE_2D, tx);
  GLERR("bindTexture");

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  GLERR("texParameteri");
  
  #ifdef GLES_ONLY
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, length, isize(v)/length, 0, GL_RGBA, GL_FLOAT, &v[0]);  
  #else
  glTexImage2D(GL_TEXTURE_2D, 0, 0x8814 /* GL_RGBA32F */, length, isize(v)/length, 0, GL_RGBA, GL_FLOAT, &v[0]);  
  #endif
  GLERR("bind_array");
  }

void uniform2(GLint id, array<float, 2> fl) {
  glUniform2f(id, fl[0], fl[1]);
  }

color_t color_out_of_range = 0x0F0800FF;

transmatrix get_ms(cell *c, int a, bool mirror) {
  int z = a ? 1 : -1;
  
  if(c->type == 3) {
    hyperpoint h = 
      project_on_triangle(
        hybrid::get_corner(c, a, 0, z),
        hybrid::get_corner(c, a, 1, z),
        hybrid::get_corner(c, a, 2, z)
        );
    transmatrix T = rspintox(h);
    if(mirror) T = T * MirrorX;
    return T * xpush(-2*hdist0(h)) * spintox(h);
    }
  else {
    hyperpoint h = Hypc;
    for(int a=0; a<c->type; a++) {
      hyperpoint corner = hybrid::get_corner(c, a, 0, z);
      h += corner;
      }
    h = normalize(h);
    ld d = hdist0(h);
    if(h[2] > 0) d = -d;
    if(mirror) return MirrorZ * zpush(2*d);
    return zpush(2*d);
    }
  }

int nesting;

struct raycast_map {

  int saved_frameid;
  
  vector<cell*> lst;
  map<cell*, int> ids;

  vector<transmatrix> ms;

  int length, per_row, rows;

  vector<array<float, 4>> connections, wallcolor, texturemap, volumetric;
  
  void apply_shape() {
    length = 4096;
    per_row = length / deg;  
    rows = next_p2((isize(lst)+per_row-1) / per_row);  
    int q = length * rows;
    connections.resize(q);
    wallcolor.resize(q);
    texturemap.resize(q);
    volumetric.resize(q);
    }
  
  void generate_initial_ms(cell *cs) {
    auto sa = hybrid::gen_sample_list();
    
    ms.clear();
    ms.resize(sa.back().first, Id);
    
    for(auto& p: sa) {
      int id = p.first;
      cell *c = p.second;
      if(!c) continue;
      for(int j=0; j<c->type; j++)
        ms[id+j] = hybrid::ray_iadj(c, j);
      if(WDIM == 2) for(int a: {0, 1}) {
        ms[id+c->type+a] = get_ms(c, a, false);
        }
      }
    
    // println(hlog, ms);
    
    if(!sol && !nil && (reflect_val || reg3::ultra_mirror_in())) {
      if(BITRUNCATED) exit(1);
      for(int j=0; j<cs->type; j++) {
        transmatrix T = inverse(ms[j]);
        hyperpoint h = tC0(T);
        ld d = hdist0(h);
        transmatrix U = rspintox(h) * xpush(d/2) * MirrorX * xpush(-d/2) * spintox(h);
        ms.push_back(U);
        }
      
      if(WDIM == 2) 
        for(int a: {0, 1}) {
          ms.push_back(get_ms(cs, a, true));
          }
      
      if(reg3::ultra_mirror_in()) {
        for(auto v: cgi.ultra_mirrors) 
          ms.push_back(v);
        }
      }    

    if(prod) {
      for(auto p: sa) {
        int id =p.first;
        if(id == 0) continue;
        ms[id-2] = Id;
        ms[id-1] = Id;
        }
      }
    }
  
  void generate_cell_listing(cell *cs) {
    manual_celllister cl;
    cl.add(cs);
    bool optimize = !isWall3(cs);
    // vector<int> legaldir = { -1 };
    for(int i=0; i<isize(cl.lst); i++) {
      cell *c = cl.lst[i];
      if(racing::on && i > 0 && c->wall == waBarrier) continue;
      if(optimize && isWall3(c)) continue;
      forCellIdCM(c2, d, c) {
        // if(reflect_val == 0 && !((1<<d) & legaldir[i])) continue;
        if(rays_generate) setdist(c2, 7, c);
        /* if(!cl.listed(c2))
          legaldir.push_back(legaldir[i] &~ (1<<((d+3)%6)) ); */
        cl.add(c2);
        if(isize(cl.lst) >= max_cells) goto finish;
        }
      }
    finish:
    lst = cl.lst;
    ids.clear();
    for(int i=0; i<isize(lst); i++) ids[lst[i]] = i;
    }

  array<float, 2> enc(int i, int a) { 
    array<float, 2> res;
    res[0] = ((i%per_row) * deg + a + .5) / length;
    res[1] = ((i / per_row) + .5) / rows;
    return res;
    }

  void generate_connections(cell *c, int id) {
    auto& vmap = volumetric::vmap;
    if(volumetric::on) {
      celldrawer dd;
      dd.c = c;
      dd.setcolors();
      int u = (id/per_row*length) + (id%per_row * deg);
      color_t vcolor;
      if(vmap.count(c))
        vcolor = vmap[c];
      else 
        vcolor = (backcolor << 8);
      volumetric[u] = glhr::acolor(vcolor);
      }
    forCellIdEx(c1, i, c) {
      int u = (id/per_row*length) + (id%per_row * deg) + i;
      if(!ids.count(c1)) {
        wallcolor[u] = glhr::acolor(color_out_of_range | 0xFF);
        texturemap[u] = glhr::makevertex(0.1,0,0);
        continue;
        }
      auto code = enc(ids[c1], 0);
      connections[u][0] = code[0];
      connections[u][1] = code[1];
      if(isWall3(c1)) {
        celldrawer dd;
        dd.c = c1;
        dd.setcolors();
        shiftmatrix Vf;
        dd.set_land_floor(Vf);
        color_t wcol = darkena(dd.wcol, 0, 0xFF);
        int dv = get_darkval(c1, c->c.spin(i));
        float p = 1 - dv / 16.;
        wallcolor[u] = glhr::acolor(wcol);
        for(int a: {0,1,2}) wallcolor[u][a] *= p;
        if(qfi.fshape) {
          texturemap[u] = floor_texture_map[qfi.fshape->id];
          }
        else
          texturemap[u] = glhr::makevertex(0.1,0,0);
        }
      else {
        color_t col = transcolor(c, c1, winf[c->wall].color) | transcolor(c1, c, winf[c1->wall].color);
        if(col == 0)
          wallcolor[u] = glhr::acolor(0);
        else {
          int dv = get_darkval(c1, c->c.spin(i));
          float p = 1 - dv / 16.;
          wallcolor[u] = glhr::acolor(col);
          for(int a: {0,1,2}) wallcolor[u][a] *= p;
          texturemap[u] = glhr::makevertex(0.001,0,0);
          }
        }
      
      int wo = wall_offset(c);
      if(wo >= irays) {
        println(hlog, "wo=", wo, " irays = ", irays);
        reset_raycaster();
        return;
        }
      transmatrix T = currentmap->iadj(c, i) * inverse(ms[wo + i]);
      if(in_e2xe() && i >= c->type-2)
        T = Id;
      for(int k=0; k<=isize(ms); k++) {
        if(k < isize(ms) && !eqmatrix(ms[k], T)) continue;
        if(k == isize(ms)) ms.push_back(T);
        connections[u][2] = (k+.5) / 1024.;
        break;
        }
      connections[u][3] = (wall_offset(c1) / 256.) + (c1->type + (WDIM == 2 ? 2 : 0) + .5) / 4096.;
      }
    if(WDIM == 2) for(int a: {0, 1}) {
      celldrawer dd;
      dd.c = c;
      dd.setcolors();
      shiftmatrix Vf;
      dd.set_land_floor(Vf);
      int u = (id/per_row*length) + (id%per_row * deg) + c->type + a;
      wallcolor[u] = glhr::acolor(darkena(dd.fcol, 0, 0xFF));
      if(qfi.fshape) 
        texturemap[u] = floor_texture_map[qfi.fshape->id];
      else
        texturemap[u] = glhr::makevertex(0.1,0,0);
      }
    }
  
  void generate_connections() {
    int id = 0;
    for(cell* c: lst)
      generate_connections(c, id++);
    }
  
  bool gms_exceeded() {
    return isize(ms) > gms_array_size;
    }

  void assign_uniforms(raycaster* o) {
    glUniform1i(o->uLength, length);
    GLERR("uniform mediump length");
    
    vector<glhr::glmatrix> gms;
    for(auto& m: ms) gms.push_back(glhr::tmtogl_transpose3(m));
    glUniformMatrix4fv(o->uM, isize(gms), 0, gms[0].as_array());
    
    bind_array(wallcolor, o->tWallcolor, txWallcolor, 4, length);
    bind_array(connections, o->tConnections, txConnections, 3, length);
    bind_array(texturemap, o->tTextureMap, txTextureMap, 5, length);
    if(volumetric::on) bind_array(volumetric, o->tVolumetric, txVolumetric, 6, length);
    }
  
  void create_all(cell *cs) {
    saved_frameid = frameid;
    generate_initial_ms(cs);
    generate_cell_listing(cs);
    apply_shape();
    generate_connections();
    }
  
  bool need_to_create(cell *cs) {
    if(!fixed_map && frameid != saved_frameid) return true;
    return !ids.count(cs);
    }
  };

unique_ptr<raycast_map> rmap;

EX void reset_raycaster() { 
  our_raycaster = nullptr; rmap = nullptr; 
  rots::saved_matrices_ray = {};
  }

EX void reset_raycaster_map() { 
  rmap = nullptr;
  }

EX void cast() {
  // may call itself recursively in case of bugs -- just in case...
  dynamicval<int> dn(nesting, nesting+1);
  if(nesting > 10) return;
  
  if(isize(cgi.raywall) > irays) reset_raycaster();
    
  enable_raycaster();

  auto& o = our_raycaster;
  
  if(need_many_cell_types() && o->uWallOffset == -1) {
    reset_raycaster();
    cast();
    return;
    }  
  
  if(comparison_mode) 
    glColorMask( GL_TRUE,GL_FALSE,GL_FALSE,GL_TRUE );

  vector<glvertex> screen = {
    glhr::makevertex(-1, -1, 1),
    glhr::makevertex(-1, +1, 1),
    glhr::makevertex(+1, -1, 1),
    glhr::makevertex(-1, +1, 1),
    glhr::makevertex(+1, -1, 1),
    glhr::makevertex(+1, +1, 1)
    };

  ld d = current_display->eyewidth();
  if(vid.stereo_mode == sLR) d = 2 * d - 1;
  else d = -d;

  auto& cd = current_display;
  cd->set_viewport(global_projection);
  cd->set_mask(global_projection);
  
  #if CAP_VR
  if(o->uEyeShift != -1) {
    transmatrix T = vrhr::eyeshift;
    if(nonisotropic)
      T = inverse(NLP) * T;
    glUniformMatrix4fv(o->uEyeShift, 1, 0, glhr::tmtogl_transpose3(T).as_array());
    glUniform1f(o->uAbsUnit, vrhr::absolute_unit_in_meters);
    }
  if(vrhr::rendering_eye()) {
    glUniformMatrix4fv(o->uProjection, 1, 0, glhr::tmtogl_transpose3(vrhr::eyeproj).as_array());
    }
  #else
  if(0) ;
  #endif
  else {
    transmatrix proj = Id;
    proj = eupush(-global_projection * d, 0) * proj;
    proj = euscale(cd->tanfov / (vid.stereo_mode == sLR ? 2 : 1), cd->tanfov * cd->ysize / cd->xsize) * proj;
    proj = eupush(-((cd->xcenter-cd->xtop)*2./cd->xsize - 1), -((cd->ycenter-cd->ytop)*2./cd->ysize - 1)) * proj;
    glUniformMatrix4fv(o->uProjection, 1, 0, glhr::tmtogl_transpose3(proj).as_array());
    }
  
  if(!callhandlers(false, hooks_rayset, o)) {
  
  cell *cs = centerover;

  transmatrix T = cview().T;
  
  if(global_projection)
    T = xpush(vid.ipd * global_projection/2) * T;

  if(nonisotropic) T = NLP * T;
  T = inverse(T);

  virtualRebase(cs, T);
  
  int ray_fixes = 0;
  
  transmatrix msm = stretch::mstretch_matrix;

  back:
  for(int a=0; a<cs->type; a++)
    if(hdist0(hybrid::ray_iadj(cs, a) * tC0(T)) < hdist0(tC0(T))) {
      T = currentmap->iadj(cs, a) * T;
      if(o->uToOrig != -1) {
        transmatrix HT = currentmap->adj(cs, a);
        HT = stretch::itranslate(tC0(HT)) * HT;
        msm = HT * msm;
        }
      cs = cs->move(a);
      ray_fixes++;
      if(ray_fixes > 100) {
        println(hlog, "major ray error");
        return;
        }
      goto back;
      }
  if(ray_fixes) println(hlog, "ray error x", ray_fixes);

  
  glUniformMatrix4fv(o->uStart, 1, 0, glhr::tmtogl_transpose3(T).as_array());
  if(o->uLP != -1) glUniformMatrix4fv(o->uLP, 1, 0, glhr::tmtogl_transpose3(inverse(NLP)).as_array());
  GLERR("uniform mediump startid");
  glUniform1f(o->uIPD, vid.ipd);
  GLERR("uniform mediump IPD");
  
  if(o->uITOA != -1) {
    glUniformMatrix4fv(o->uITOA, 1, 0, glhr::tmtogl_transpose3(stretch::m_itoa).as_array());   
    glUniformMatrix4fv(o->uATOI, 1, 0, glhr::tmtogl_transpose3(stretch::m_atoi).as_array());   
    }

  if(o->uToOrig != -1) {
    glUniformMatrix4fv(o->uToOrig, 1, 0, glhr::tmtogl_transpose3(msm).as_array());   
    glUniformMatrix4fv(o->uFromOrig, 1, 0, glhr::tmtogl_transpose3(inverse(msm)).as_array());   
    }
  
  if(o->uWallOffset != -1) {
    glUniform1i(o->uWallOffset, wall_offset(cs));
    glUniform1i(o->uSides, cs->type + (WDIM == 2 ? 2 : 0));
    }

  vector<GLint> wallstart;
  for(auto i: cgi.wallstart) wallstart.push_back(i);
  glUniform1iv(o->uWallstart, isize(wallstart), &wallstart[0]);  
  
  vector<glvertex> wallx, wally;
  for(auto& m: cgi.raywall) {
    wallx.push_back(glhr::pointtogl(m[0]));
    wally.push_back(glhr::pointtogl(m[1]));
    }
  
  glUniform4fv(o->uWallX, isize(wallx), &wallx[0][0]);
  glUniform4fv(o->uWallY, isize(wally), &wally[0][0]);

  if(o->uLevelLines != -1)
    glUniform1f(o->uLevelLines, levellines);
  if(o->uBinaryWidth != -1)
    glUniform1f(o->uBinaryWidth, vid.binary_width/2 * (nih?1:log(2)));
  #if CAP_SOLV
  if(o->uStraighten != -1) {
    glUniformMatrix4fv(o->uStraighten, 1, 0, glhr::tmtogl_transpose(asonov::straighten).as_array());
    }
  if(o->uReflectX != -1) {
    auto h = glhr::pointtogl(tangent_length(spin(90*degree) * asonov::ty, 2));
    glUniform4fv(o->uReflectX, 1, &h[0]);
    h = glhr::pointtogl(tangent_length(spin(90*degree) * asonov::tx, 2));
    glUniform4fv(o->uReflectY, 1, &h[0]);
    }
  #endif
  if(o->uPLevel != -1)
    glUniform1f(o->uPLevel, cgi.plevel / 2);
  
  #if CAP_BT
  if(o->uBLevel != -1)
    glUniform1f(o->uBLevel, log(bt::expansion()) / 2);
  #endif
  
  if(o->uLinearSightRange != -1)
    glUniform1f(o->uLinearSightRange, sightranges[geometry]);

  glUniform1f(o->uExpDecay, exp_decay_current());
  
  glUniform1f(o->uExpStart, exp_start);

  auto cols = glhr::acolor(darkena(backcolor, 0, 0xFF));
  if(o->uFogColor != -1)
    glUniform4f(o->uFogColor, cols[0], cols[1], cols[2], cols[3]);

  if(!rmap) rmap = (unique_ptr<raycast_map>) new raycast_map;
  
  if(rmap->need_to_create(cs)) {
    rmap->create_all(cs);  
    if(rmap->gms_exceeded()) {
      gms_array_size = isize(rmap->ms);
      println(hlog, "changing gms_array_size to ", gms_array_size);
      reset_raycaster();
      cast();
      return;
      }
    rmap->assign_uniforms(&*o);
    }
  GLERR("uniform mediump start");
  uniform2(o->uStartid, rmap->enc(rmap->ids[cs], 0));
  }

  #if CAP_VERTEXBUFFER
  glhr::bindbuffer_vertex(screen);
  glVertexAttribPointer(hr::aPosition, 4, GL_FLOAT, GL_FALSE, sizeof(glvertex), 0);
  #else
  glVertexAttribPointer(hr::aPosition, 4, GL_FLOAT, GL_FALSE, sizeof(glvertex), &screen[0]);
  #endif
  
  if(ray::comparison_mode)
    glhr::set_depthtest(false);
  else {
    glhr::set_depthtest(true);
    glhr::set_depthwrite(true);
    }
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glActiveTexture(GL_TEXTURE0 + 0);
  glBindTexture(GL_TEXTURE_2D, floor_textures->renderedTexture);

  GLERR("bind");
  glDrawArrays(GL_TRIANGLES, 0, 6);
  GLERR("finish");
  }

EX namespace volumetric {

EX bool on;

EX map<cell*, color_t> vmap;

int intensity = 16;

EX void enable() {
  if(!on) {
    on = true;
    reset_raycaster();
    }
  }

EX void random_fog() {
  enable();
  for(cell *c: currentmap->allcells())
    vmap[c] = ((rand() % 0x1000000) << 8) | intensity;
  }

EX void menu() {
  cmode = sm::SIDE | sm::MAYDARK;
  gamescreen(0);
  dialog::init(XLAT("volumetric raycasting"));  

  if(!cheater) {
    dialog::addItem(XLAT("enable the cheat mode for additional options"), 'X');
    dialog::add_action(enable_cheat);
    dialog::addBack();
    dialog::display();
    return;
    }
  
  dialog::addBoolItem(XLAT("active"), on, 'a');
  dialog::add_action([&] {
    on = !on;
    reset_raycaster();
    });
  
  dialog::addSelItem(XLAT("intensity of random coloring"), its(intensity), 'i');
  dialog::add_action([] { 
    dialog::editNumber(intensity, 0, 255, 5, 15, "", "");
    dialog::reaction = random_fog;
    });

  dialog::addItem(XLAT("color randomly"), 'r');
  dialog::add_action(random_fog);

  dialog::addColorItem("color cell under cursor", vmap.count(centerover) ? vmap[centerover] : 0, 'c');
  dialog::add_action([&] {
    enable();
    dialog::openColorDialog(vmap[centerover]); 
    dialog::dialogflags |= sm::SIDE;      
    });

  dialog::addColorItem("color cell under player", vmap.count(cwt.at) ? vmap[cwt.at] : 0, 'p');
  dialog::add_action([&] {
    enable();
    dialog::openColorDialog(vmap[cwt.at]); 
    dialog::dialogflags |= sm::SIDE;      
    });
  
  dialog::addBreak(150);
  dialog::addHelp("This fills all the cells with glowing fog, for cool visualizations");
  dialog::addBreak(150);

  dialog::addBack();
  dialog::display();
  }
  
EX }

EX void configure() {
  cmode = sm::SIDE | sm::MAYDARK;
  gamescreen(0);
  dialog::init(XLAT("raycasting configuration"));
  
  dialog::addBoolItem(XLAT("available in current geometry"), available(), 0);
  
  dialog::addBoolItem(XLAT("use raycasting?"), want_use == 2 ? true : in_use, 'u');
  if(want_use == 1) dialog::lastItem().value = XLAT("SMART");
  
  dialog::add_action([] {
    want_use++; want_use %= 3;
    });

  dialog::addBoolItem_action(XLAT("comparison mode"), comparison_mode, 'c');
  
  dialog::addSelItem(XLAT("exponential range"), fts(exp_decay_current()), 'r');
  dialog::add_action([&] {
    dialog::editNumber(exp_decay_current(), 0, 40, 0.25, 5, XLAT("exponential range"), 
      XLAT("brightness formula: max(1-d/sightrange, s*exp(-d/r))")
      );
    });

  dialog::addSelItem(XLAT("exponential start"), fts(exp_start), 's');
  dialog::add_action([&] {
    dialog::editNumber(exp_start, 0, 1, 0.1, 1, XLAT("exponential start"), 
      XLAT("brightness formula: max(1-d/sightrange, s*exp(-d/r))\n")
      );
    });

  if(hard_limit < NO_LIMIT)
    dialog::addSelItem(XLAT("hard limit"), fts(hard_limit), 'H');
  else
    dialog::addBoolItem(XLAT("hard limit"), false, 'H');
  dialog::add_action([&] {
    if(hard_limit >= NO_LIMIT) hard_limit = 10;
    dialog::editNumber(hard_limit, 0, 100, 1, 10, XLAT("hard limit"), "");
    dialog::reaction = reset_raycaster;
    dialog::extra_options = [] {
      dialog::addItem("no limit", 'N');
      dialog::add_action([] { hard_limit = NO_LIMIT; reset_raycaster(); });
      };
    });
  
  if(!nil) {
    dialog::addSelItem(XLAT("reflective walls"), fts(reflect_val), 'R');
    dialog::add_action([&] {
      dialog::editNumber(reflect_val, 0, 1, 0.1, 0, XLAT("reflective walls"), "");
      dialog::reaction = reset_raycaster;
      });
    }

  if(is_stepbased()) {
    dialog::addSelItem(XLAT("max step"), fts(maxstep_current()), 'x');
    dialog::add_action([] {
      auto& ms = maxstep_current();
      dialog::editNumber(maxstep_current(), 1e-6, 1, .1,
        &ms == &maxstep_pro ? .05 :
        &ms == &maxstep_nil ? .1 : .5,
        XLAT("max step"), "affects the precision of solving the geodesic equation in Solv");
      dialog::scaleLog();
      dialog::bound_low(1e-9);
      dialog::reaction = reset_raycaster;
      });

    dialog::addSelItem(XLAT("min step"), fts(minstep), 'n');
    dialog::add_action([] {
      dialog::editNumber(minstep, 1e-6, 1, 0.1, 0.001, XLAT("min step"), "how precisely should we find out when do cross the cell boundary");
      dialog::scaleLog();
      dialog::bound_low(1e-9);
      dialog::reaction = reset_raycaster;
      });
    }

  dialog::addBoolItem(XLAT("volumetric raytracing"), volumetric::on, 'v');
  dialog::add_action_push(volumetric::menu);

  dialog::addSelItem(XLAT("iterations"), its(max_iter_current()), 's');
  dialog::add_action([&] {
    dialog::editNumber(max_iter_current(), 0, 600, 1, 60, XLAT("iterations"), "in H3/H2xE/E3 this is the number of cell boundaries; in nonisotropic, the number of simulation steps");
    dialog::reaction = reset_raycaster;
    });

  dialog::addSelItem(XLAT("max cells"), its(max_cells), 's');
  dialog::add_action([&] {
    dialog::editNumber(max_cells, 16, 131072, 0.1, 4096, XLAT("max cells"), "");
    dialog::scaleLog();
    dialog::extra_options = [] {
      dialog::addBoolItem_action("generate", rays_generate, 'G');
      dialog::addColorItem(XLAT("out-of-range color"), color_out_of_range, 'X');
      dialog::add_action([] { 
        dialog::openColorDialog(color_out_of_range); 
        dialog::dialogflags |= sm::SIDE;
        });
      };
    });
  
  if(gms_array_size > gms_limit && ray::in_use) {
    dialog::addBreak(100);
    dialog::addHelp(XLAT("unfortunately this honeycomb is too complex for the current implementation (%1>%2)", its(gms_array_size), its(gms_limit)));
    }

  edit_levellines('L');
  
  dialog::addBack();
  dialog::display();
  }

#if CAP_COMMANDLINE  
int readArgs() {
  using namespace arg;
           
  if(0) ;
  else if(argis("-ray-do")) {
    PHASEFROM(2);
    want_use = 2;
    }
  else if(argis("-ray-dont")) {
    PHASEFROM(2);
    want_use = 0;
    }
  else if(argis("-ray-smart")) {
    PHASEFROM(2);
    want_use = 1;
    }
  else if(argis("-ray-range")) {
    PHASEFROM(2);
    shift_arg_formula(exp_start, reset_raycaster);
    shift_arg_formula(exp_decay_current(), reset_raycaster);
    }
  else if(argis("-ray-hard")) {
    PHASEFROM(2);
    shift_arg_formula(hard_limit);
    }
  else if(argis("-ray-out")) {
    PHASEFROM(2); shift(); color_out_of_range = arghex();
    }
  else if(argis("-ray-comp")) {
    PHASEFROM(2);
    comparison_mode = true;
    }
  else if(argis("-ray-sol")) {
    PHASEFROM(2);
    shift(); max_iter_sol = argi();
    shift_arg_formula(maxstep_sol, reset_raycaster);
    reset_raycaster();
    }
  else if(argis("-ray-iter")) {
    PHASEFROM(2);
    shift(); max_iter_current() = argi();
    }
  else if(argis("-ray-step")) {
    PHASEFROM(2);
    println(hlog, "maxstep_current() is ", maxstep_current());
    shift_arg_formula(maxstep_current());
    }
  else if(argis("-ray-cells")) {
    PHASEFROM(2); shift();
    rays_generate = true;
    max_cells = argi();
    }
  else if(argis("-ray-reflect")) {
    PHASEFROM(2); 
    shift_arg_formula(reflect_val, reset_raycaster);
    }
  else if(argis("-ray-cells-no")) {
    PHASEFROM(2); shift();
    rays_generate = false;
    max_cells = argi();
    }
  else if(argis("-ray-random")) {
    start_game();
    shift(); volumetric::intensity = argi();
    volumetric::random_fog();
    }
  else if(argis("-ray-cursor")) {
    start_game();
    volumetric::enable();
    shift(); volumetric::vmap[centerover] = arghex();
    }
  else return 1;
  return 0;
  }

auto hook = addHook(hooks_args, 100, readArgs)
 + addHook(hooks_clearmemory, 40, [] { rmap = {}; });
#endif

#if CAP_CONFIG
void addconfig() {
  param_f(exp_start, "ray_exp_start");
  param_f(exp_decay_exp, "ray_exp_decay_exp");
  param_f(maxstep_sol, "ray_maxstep_sol");
  param_f(maxstep_nil, "ray_maxstep_nil");
  param_f(minstep, "ray_minstep");
  param_f(reflect_val, "ray_reflect_val");
  param_f(hard_limit, "ray_hard_limit");
  addsaver(want_use, "ray_want_use");
  param_f(exp_decay_poly, "ray_exp_decay_poly");
  addsaver(max_iter_iso, "ray_max_iter_iso");
  addsaver(max_iter_sol, "ray_max_iter_sol");
  addsaver(max_cells, "ray_max_cells");
  addsaver(rays_generate, "ray_generate");
  param_b(fixed_map, "ray_fixed_map");
  }
auto hookc = addHook(hooks_configfile, 100, addconfig);
#endif

#endif

#if !CAP_RAY
EX always_false in_use;
EX always_false comparison_mode;
EX void reset_raycaster() { }
EX void cast() { }
#endif
EX }
}
