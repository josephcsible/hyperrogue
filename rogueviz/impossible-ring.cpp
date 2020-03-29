#include "../hyper.h"

// Impossible Ring visualization

// used in: 
// https://youtu.be/3WejR74o6II
// https://youtu.be/ztodGQDK810

namespace hr {

ld cscale = .2;

struct iring {

  static const int frames = 32;
  
  static const int cols = 256;

  static const int steps = 2048;    

  array<array<hpcshape, cols>, frames> ptriangle[2];

  vector<color_t> huess[2];
  
  vector<transmatrix> path;
  
  void init() {
  
    unsigned difh = 256;
    int mh = 255;
    color_t base = 0xFF;
    
    auto& hues = huess[0];

    for(unsigned y=0; y<difh; y++)
      hues.push_back(base + 0x1000000*mh + 0x10000 * y);
    for(unsigned y=0; y<difh; y++)
      hues.push_back(base + 0x1010000*mh - 0x1000000 * y);
    for(unsigned y=0; y<difh; y++)
      hues.push_back(base + 0x0010000*mh + 0x100 * y);
    for(unsigned y=0; y<difh; y++)
      hues.push_back(base + 0x0010100*mh - 0x10000 * y);
    for(unsigned y=0; y<difh; y++)
      hues.push_back(base + 0x0000100*mh + 0x1000000 * y);
    for(unsigned y=0; y<difh; y++)
      hues.push_back(base + 0x1000100*mh - 0x100 * y);      
      
    for(color_t h: hues) huess[1].push_back(((h & 0xFEFEFE00) >> 1) | 0xFF);
  
    ld delta = 0.004;
    
    transmatrix T = cspin(1, 2, 45*degree);
    
    int switchat = nil ? 1024 : 2048;
    
    auto step = [&] (int id) {
      ld alpha = id * 1. / steps * 2 * M_PI;
      if(id < switchat)
        return T * point3(cos(alpha) * delta, sin(alpha) * delta, 0);
      else
        return T * point3(cos(alpha) * delta, 0, sin(alpha) * delta);
      };
    
    ld dmin = 0, dmax = 1;
    
    auto shift = [&] (ld d) {
      delta = d;

      hyperpoint start = C0;
      
      for(int a=0; a<steps; a++) {
        hyperpoint s = step(a);
        start = rgpushxto0(start) * (C0+s);
        }
      
      println(hlog, "start[", delta, "] = ", kz(start));
      
      return start[2];
      };
    
    println(hlog, shift(0.0001), shift(1));          
    
    if(nil) for(int it=0; it<50; it++) {
      delta = (dmin + dmax) / 2;
      
      ld val = shift(delta);

      if(val > 0) dmax = delta;
      else dmin = delta;
      }
    
    println(hlog, "delta = ", delta);
    
    vector<array<hyperpoint, 4> > pipe;
    
    hyperpoint start = C0;
    
    path.resize(2 * steps);
    
    for(int a=0; a<steps; a++) {

      hyperpoint uds[3];  

      ld alpha = a * 1. / steps * 2 * M_PI;
      if(a < switchat) {
        uds[0] = T * point31(sin(alpha) * cscale, -cos(alpha) * cscale, 0) - C0;
        uds[1] = T * point31(0, 0, cscale) - C0;
        uds[2] = T * point31(-cos(alpha) * cscale, -sin(alpha) * cscale, 0) - C0;
        }
      else {
        uds[0] = T * point31(0, cscale, 0) - C0;
        uds[1] = T * point31(sin(alpha) * cscale, 0, -cos(alpha) * cscale) - C0;
        uds[2] = T * point31(-cos(alpha) * cscale, 0, -sin(alpha) * cscale) - C0;
        }
  
      // compute cube vertices
      
      array<hyperpoint, 4> verts;      
    
      for(int a=0; a<4; a++) {
        verts[a] = C0;
        for(int k=0; k<2; k++) 
          verts[a] += (a&(1<<k)) ? uds[k] : -uds[k];
        verts[a] = nisot::translate(start) * verts[a];
        }
      
      pipe.push_back(verts);

      path[a] = inverse(build_matrix(uds[0], uds[1], uds[2], C0)) * inverse(rgpushxto0(start));
      path[a+steps] = inverse(build_matrix(-uds[0], -uds[1], uds[2], C0)) * inverse(rgpushxto0(start));
      
      // println(hlog, "gs = ", gpushxto0(start));

      println(hlog, "start @ ", inverse(rgpushxto0(start)) * start);

      hyperpoint s = step(a);
      start = rgpushxto0(start) * (C0 + s);      
      
       // * );
      }
    
    pipe.resize(steps + frames);
    for(int i=0; i<frames; i++)
    for(int j=0; j<4; j++)
      pipe[i+steps][j] = pipe[i][j^3];

    for(int fr=0; fr<frames; fr++)
    for(int sa=0; sa<2; sa++)
    for(int si=0; si<cols; si++) {
      auto textured_square = [&] (auto f) {
        texture_order([&] (ld ix, ld iy) { f(.5 + ix/2 + iy/2, .5 + ix/2 - iy/2); });
        texture_order([&] (ld ix, ld iy) { f(.5 - ix/2 - iy/2, .5 - ix/2 + iy/2); });
        texture_order([&] (ld ix, ld iy) { f(.5 + ix/2 - iy/2, .5 - ix/2 - iy/2); });
        texture_order([&] (ld ix, ld iy) { f(.5 - ix/2 + iy/2, .5 + ix/2 + iy/2); });
        };
  
      auto pipesquare = [&] (hyperpoint a00, hyperpoint a01, hyperpoint a10, hyperpoint a11) {
        textured_square( [&] (ld ix, ld iy) {
          hyperpoint shf = lerp(lerp(a00, a01, ix), lerp(a10, a11, ix), iy);
          // if(cscale) shf = shf * cscale - C0 * (cscale-1);
          cgi.hpcpush(shf);
          });
        };
      
      cgi.bshape(ptriangle[sa][fr][si], PPR::WALL);

      for(int i=fr; i<steps; i+=frames) {
        auto& pi = pipe[i];
        auto& pj = pipe[i+frames];
        
        int val = i * cols / steps / 2;
        
        if(si == val && sa == 0) pipesquare(pi[0], pi[2], pj[0], pj[2]);
        if(si == (val+cols/2)%cols && sa == 0) pipesquare(pi[1], pi[3], pj[1], pj[3]);
        if(si == (val+cols/4)%cols && sa == 1) pipesquare(pi[0], pi[1], pj[0], pj[1]);
        if(si == (val+cols*3/4)%cols && sa == 1) pipesquare(pi[2], pi[3], pj[2], pj[3]);
        }
      
      cgi.last->flags |= POLY_TRIANGLES;
      cgi.last->tinf = &floor_texture_vertices[0];
      cgi.last->texture_offset = 0;
      cgi.finishshape();
      cgi.extra_vertices();
      }
    }
    
  };

iring *ir;

bool draw_ptriangle(cell *c, const transmatrix& V) {
  
  if(!ir) { ir = new iring; ir->init(); 
    // growthrate();
    }
  
  if(c == cwt.at) {
    int frid = (ticks % 1000) * ir->frames / 1000;
    for(int sa: {0, 1})
    for(int side=0; side<ir->cols; side++) {
      auto &s = queuepoly(V, ir->ptriangle[sa][frid][side], ir->huess[sa][256*6/ir->cols * side]);
      ensure_vertex_number(*s.tinf, s.cnt);

      /* auto& s1 = queuepoly(V * nisot::translate(td.at), ir->pcube[side], gradient(tcolors[td.tcolor], magiccolors[side], 0, .2, 1));
      ensure_vertex_number(*s1.tinf, s1.cnt); */
      }
    }

  return false;
  }

bool cylanim = false;

auto hchook = addHook(hooks_drawcell, 100, draw_ptriangle)

+ addHook(hooks_args, 100, [] {
  using namespace arg;
           
  if(0) ;
  else if(argis("-cyls")) {
    shift_arg_formula(cscale);
    }
  else if(argis("-cylanim")) {
    cylanim = !cylanim;
    }
  else return 1;
  return 0;
  })

+ addHook(anims::hooks_anim, 100, [] {
  if(!ir || !cylanim) return;
  centerover = currentmap->gamestart();
  long long isp = isize(ir->path);
  View = ir->path[isp-1 - (ticks * isp / int(anims::period)) % isp];
  shift_view(point3(0, 0.3, 0));
  anims::moved();
  });
  



}