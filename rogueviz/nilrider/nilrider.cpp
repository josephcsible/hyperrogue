// Nil Rider
// Copyright (C) 2022-2025 Zeno Rogue, see '../../hyper.cpp' for details

// compile with: ./mymake -O3 -rv rogueviz/nilrider/nilrider.cpp and then launch with -nilrider
// add -DNILRIDER for standalone Nil Rider

#if NILRIDER
#define CUSTOM_CAPTION "Nil Rider 2.0"
#define MAXMDIM 4
#define CAP_INV 0
#define CAP_COMPLEX2 0
#define CAP_EDIT 0
#define CAP_TEXTURE 1
#define CAP_BT 0
#define CAP_SOLV 0
#define CAP_THREAD 0
#define CAP_RUG 0
#define CAP_SVG 0
#define CAP_TOUR 0
#define CAP_IRR 0
#define CAP_CRYSTAL 0
#define CAP_ARCM 0
#define CAP_HISTORY 0
#define CAP_STARTANIM 0
#define CAP_SAVE 0
#define CAP_TRANS 0

#ifdef BWEB
#include "../../hyperweb.cpp"
#else
#include "../../hyper.cpp"
#endif
#include "../simple-impossible.cpp"
#include "../rogueviz.cpp"
#endif

#include "nilrider.h"
#include "statues.cpp"
#include "timestamp.cpp"
#include "levels.cpp"
#include "level.cpp"
#include "planning.cpp"
#include "solver.cpp"
#include "save.cpp"

namespace nilrider {

/** is the game paused? */
bool paused = false;

bool planning_mode = false;
bool view_replay = false;
int simulation_start_tick;

ld aimspeed_key_x = 1, aimspeed_key_y = 1, aimspeed_mouse_x = 1, aimspeed_mouse_y = 1;

vector<string> move_names = { "", "", "", "", "camera down", "move left", "camera up", "move right", "fine control", "pause", "reverse time", "view simulation", "menu" };

enum nilrider_moves { nrCameraUp=4, nrMoveRight=5, nrCameraDown=6, nrMoveLeft=7, nrFineControl=8, nrPause=9, nrReverseTime=10, nrViewSimulation=11, nrMenu=12 };

enum pcmds {
  pcForward, pcBackward, pcTurnLeft, pcTurnRight,
  pcMoveUp, pcMoveRight, pcMoveDown, pcMoveLeft,
  pcFire, pcFace, pcFaceFire,
  pcDrop, pcCenter, pcOrbPower, pcOrbKey
  };

int reversals = 0;
bool loaded_or_planned = false;

void frame() {
  if(planning_mode && !view_replay) return;

  shiftmatrix V = ggmatrix(cwt.at);  
  
  curlev->draw_level_rec(V);

  curlev->current.draw_unilcycle(V, my_scheme);

  for(auto g: curlev->ghosts) {
    ld t = curlev->current.timer;
    ld maxt = g.history.back().timer;
    auto gr = ghost_repeat;
    if(gr <= 0) gr = 1e6;
    t -= floor(t / gr) * gr;
    for(; t < maxt; t += gr) {
      int a = 0, b = isize(g.history);
      while(a != b) {
        int s = (a + b) / 2;
        if(g.history[s].timer < t) a = s + 1;
        else b = s;
        }
      if(b < isize(g.history) && g.history[b].where != curlev->current.where) g.history[b].draw_unilcycle(V, g.cs);
      }
    }
  }

bool crash_sound = true;
bool running;
bool backing;

/* land for music */

eLand music_nilrider = eLand(400);
eLand music_nilrider_back = eLand(401);
eLand music_nilrider_paused = eLand(402);
eLand music_nilrider_planning = eLand(403);
eLand music_nilrider_nonrunning = eLand(404);

void sync_music(eLand l) {
  musicpos[music_nilrider] = zgmod(curlev->current.timer * 1000, musiclength[music_nilrider]);
  musicpos[music_nilrider_back] = zgmod(-curlev->current.timer * 1000, musiclength[music_nilrider_back]);
  }

bool turn(int delta) {
  if(planning_mode && !view_replay) return false;

  multi::get_actions(multi::scfg_default);
  auto& act = multi::action_states[1];

  ld mul = 1;
  if(act[nrFineControl]) mul /= 5;
  if(act[nrMoveRight] && !paused) curlev->current.heading_angle -= aimspeed_key_x * mul * delta / 1000.;
  if(act[nrMoveLeft] && !paused) curlev->current.heading_angle += aimspeed_key_x * mul * delta / 1000.;
  if(act[nrCameraDown] && !paused) min_gfx_slope -= aimspeed_key_y * mul * delta / 1000.;
  if(act[nrCameraUp] && !paused) min_gfx_slope += aimspeed_key_y * mul * delta / 1000.;

  curlev->current.heading_angle -= aimspeed_mouse_x * mouseaim_x * mul;
  min_gfx_slope += aimspeed_mouse_y * mouseaim_y * mul;

  #if CAP_VR
  if(vrhr::active()) {
    curlev->current.heading_angle -= aimspeed_mouse_x * vrhr::vraim_x * mul * delta / 400;
    min_gfx_slope -= aimspeed_mouse_y * vrhr::vraim_y * mul * delta / 400;
    }
  #endif

  if(min_gfx_slope < -90._deg) min_gfx_slope = -90._deg;
  if(min_gfx_slope > +90._deg) min_gfx_slope = +90._deg;
  
  backing = false;

  if(act[nrReverseTime]) {
    if(!act[nrReverseTime].last) reversals++;
    if(planning_mode)
      simulation_start_tick += 2*delta;
    else for(int i=0; i<delta; i++) {
      if(isize(curlev->history) > 1) {
        backing = true;
        curlev->history.pop_back();
        curlev->current = curlev->history.back();
        crash_sound = true;
        }
      else {
        reversals = 0;
        loaded_or_planned = false;
        crash_sound = true;
        }
      }
    }

  if(!paused && !view_replay && !backing) {

    auto t = curlev->current.collected_triangles;
    bool fail = false;

    for(int i=0; i<delta * simulation_speed; i++) {
      curlev->history.push_back(curlev->current);
      curlev->current.be_consistent();
      #if RVCOL
      auto goals = curlev->current.goals;
      #endif
      bool b = curlev->current.tick(curlev);
      running = b;
      if(!b) {
        curlev->history.pop_back();
        fail = true;
        break;
        }
      #if RVCOL
      if(b) {
        goals = curlev->current.goals &~goals;
        int gid = 0;
        for(auto& g: curlev->goals) {
          if(goals & Flag(gid)) {
            if(g.achievement_name != "") rogueviz::rv_achievement(g.achievement_name);
            if(g.leaderboard_name != "") {
              auto res = curlev->current_score[gid];
              rogueviz::rv_leaderboard((planning_mode ? "Nil Rider planning: " : "Nil Rider manual: ") + g.leaderboard_name, abs(res) * 1000, -1, rvlc::ms);
              }
            }
          gid++;
          }
        }
      #endif
      }

    if(t != curlev->current.collected_triangles)
      playSound(cwt.at, "pickup-gold");

    if(fail && crash_sound) {
      char ch = curlev->mapchar(curlev->current.where);
      if(ch == 'r') {
        playSound(cwt.at, "closegate");
        crash_sound = false;
        }
      if(ch == '!') {
        playSound(cwt.at, "seen-air");
        crash_sound = false;
        }
      }
    }
 
  if(!paused) curlev->current.centerview(curlev);
  return false;
  }

void main_menu();
void layer_selection_screen();

#define PSEUDOKEY_PAUSE 2511
#define PSEUDOKEY_SIM 2512

void toggle_replay() {
  view_replay = !view_replay;
  paused = false;
  simulation_start_tick = ticks;
  if(!view_replay && !planning_mode) {
    paused = true;
    curlev->current = curlev->history.back();
    }
  }

void run() {
  cmode = sm::PANNING | sm::NORMAL;
  emptyscreen();
  clearMessages();
  dialog::init();
  if(view_replay && !paused && !isize(curlev->history)) {
    int ttick = gmod(ticks - simulation_start_tick, isize(curlev->history));
    curlev->current = curlev->history[ttick];  
    curlev->current.centerview(curlev);
    }
  if(planning_mode && !view_replay)
    cmode |= sm::SHOWCURSOR;
  if(aimspeed_mouse_x == 0 && aimspeed_mouse_y == 0)
    cmode |= sm::SHOWCURSOR;
  gamescreen();
  if(planning_mode && !view_replay) {
    curlev->draw_planning_screen();
    if(!holdmouse) {
      auto t0 = SDL_GetTicks();
      while(SDL_GetTicks() < t0 + 100) {
        if(!curlev->simulate()) break;
        }  
      }
    }
  curlev->current.draw_instruments(curlev);
  
  if(paused && !planning_mode) {
    displayButton(current_display->xcenter, current_display->ycenter, mousing ? XLAT("paused -- click to unpause") : XLAT("paused -- press p to continue"), 'p', 8);
    }
  
  int x = vid.fsize;
  auto show_button = [&] (int c, string s, color_t col = dialog::dialogcolor) {
    if(displayButtonS(x, vid.yres - vid.fsize, s, col, 0, vid.fsize))
      getcstat = c;
    x += textwidth(vid.fsize, s) + vid.fsize;
    };
  
  if(planning_mode && !view_replay) {
    for(auto& b: buttons) show_button(b.first, b.second, planmode == b.first ? 0xFFD500 : dialog::dialogcolor);
    show_button(PSEUDOKEY_SIM, "simulation");
    if(curlev->sublevels.size() && layer_edited) {
      show_button('L', "layer: " + layer_edited->name);
      }
    }
  
  bool pause_av = view_replay || !planning_mode;
  if(pause_av) {
    show_button(PSEUDOKEY_SIM, planning_mode ? "return" : "replay", (view_replay  && !planning_mode) ? 0xFF0000 : dialog::dialogcolor);
    show_button(PSEUDOKEY_PAUSE, "pause", paused ? 0xFF0000 : dialog::dialogcolor);
    }
  
  show_button(PSEUDOKEY_MENU, "menu");

  dialog::add_key_action(PSEUDOKEY_MENU, [] {
    if(tour::on) { tour::next_slide(); return; }
    if(curlev->current.timer) paused = true;
    game_keys_scroll = true;
    pushScreen(main_menu);
    });
  if(pause_av) dialog::add_key_action(PSEUDOKEY_PAUSE, [] {
    paused = !paused;
    game_keys_scroll = true;
    if(view_replay && !paused)
      simulation_start_tick = ticks - curlev->current.timer * tps;
    });
  dialog::add_key_action('-', [] {
    paused = false;
    });
  dialog::add_key_action(PSEUDOKEY_SIM, toggle_replay);
  dialog::display();

  if(planning_mode && !view_replay && curlev->sublevels.size()) {
    dialog::add_key_action('L', [] { pushScreen(layer_selection_screen); });
    }

  int* t = multi::scfg_default.keyaction;
  for(int i=1; i<multi::SCANCODES; i++) {
    auto& ka = dialog::key_actions;
    auto match = [&] (int nr, int pseudokey) {
      if(t[i] == 16+nr) {
        auto key = SDL12(i, SDL_GetKeyFromScancode(SDL_Scancode(i)/*, 0, true*/));
        ka[key] = ka[pseudokey];
        }
      };
    match(nrPause, PSEUDOKEY_PAUSE);
    match(nrViewSimulation, PSEUDOKEY_SIM);
    match(nrMenu, PSEUDOKEY_MENU);
    }
  
  keyhandler = [] (int sym, int uni) {
    if(paused) handlePanning(sym, uni);
    if(planning_mode && !view_replay && curlev->handle_planning(sym, uni)) return;
    dialog::handleNavigation(sym, uni);
    };
  }

void clear_path(level *l) {
  l->history.clear();
  l->current = l->start;
  l->history.push_back(l->start);
  paused = false;
  reversals = 0;
  loaded_or_planned = false;
  crash_sound = true;
  }

string fname = "horizontal.nrl";

ld total_stars = 0;

void pick_level() {
  cmode = sm::VR_MENU | sm::NOSCR; gamescreen();
  dialog::init(XLAT("select the track"), 0xC0C0FFFF, 150, 100);
  ld cur_stars = 0;
  for(auto l: all_levels) {
    ld score_here = 0;
    for(int gid=0; gid<isize(l->goals); gid++) {
      if(isize(l->records[0])) {
        auto man = l->records[0][gid];
        if(man) score_here += l->goals[gid].sa(man) * 2;
        }
      if(isize(l->records[1])) {
        auto plan = l->records[1][gid];
        if(plan) score_here += l->goals[gid].sa(plan);
        }
      }
    cur_stars += score_here;

    if(l->stars_needed > total_stars && !unlock_all) {
      dialog::addSelItem(l->name, "stars needed: " + its(l->stars_needed), l->hotkey);
      }
    else {
      dialog::addSelItem(l->name, its(score_here), l->hotkey);
      dialog::add_action([l] {
        curlev = l;
        recompute_plan_transform = true;
        l->init();
        clear_path(l);
        popScreen();
        });
      }
    }
  total_stars = cur_stars;
  dialog::addBreak(100);
  dialog::addSelItem("stars collected", its(total_stars), 0);
  dialog::addItem("load a level from a file", '0');
  dialog::add_action([] {
    dialog::openFileDialog(fname, XLAT("level to load:"), ".nrl", [] () {
      try {
        load_level(fname, true);
        return true;
        }
      catch(hr_exception& e) {
        addMessage(e.what());
        return false;
        }
      });
    });
  dialog::addItem(XLAT("play this track"), SDLK_ESCAPE);
  dialog::display();
  }

void layer_selection_screen() {
  poly_outline = 0xFF;
  cmode = sm::VR_MENU | sm::NOSCR; gamescreen();
  dialog::init(XLAT("layer selection"), 0xC0C0FFFF, 150, 100);
  dialog::addBreak(50);
  auto layers = curlev->gen_layer_list();
  char key = 'a';
  for(auto l: layers) {
    dialog::addBoolItem(l->name, l == layer_edited, key++);
    dialog::add_action([l] { layer_edited = l; popScreen(); });
    }
  dialog::addBack();
  dialog::display();
  }

void pick_game() {
  clearMessages();
  cmode = sm::VR_MENU | sm::NOSCR; gamescreen();
  dialog::init();
  poly_outline = 0xFF;
  dialog::addBigItem(curlev->name, 't');
  dialog::addBreak(50);
  dialog::addHelp(curlev->longdesc);

  int gid = 0;
  for(auto& g: curlev->goals) {
    dialog::addBreak(50);
    auto man = curlev->records[0][gid];
    auto plan = curlev->records[1][gid];
    if(man && plan)
      dialog::addInfo("manual: " + format_timer_goal(man, g, false) + " planning: " + format_timer_goal(plan, g, true), g.color);
    else if(man)
      dialog::addInfo("manual: " + format_timer_goal(man, g, false), g.color);
    else if(plan)
      dialog::addInfo("planning: " + format_timer_goal(plan, g, true), g.color);
    else
      dialog::addInfo("goal not obtained:", g.color);
    dialog::addBreak(50);
    dialog::addHelp(g.desc);
    gid++;
    }

  dialog::addBreak(100);
  dialog::addItem("change the track", 't');
  dialog::add_action_push(pick_level);
  dialog::addBreak(100);
  add_edit(planning_mode);
  dialog::addItem(XLAT("play this track"), SDLK_ESCAPE);
  dialog::addItem(XLAT("Nil Rider main menu"), 'm');
  dialog::add_action([] { popScreen(); pushScreen(main_menu); });
  /* dialog::addItem(XLAT("quit Nil Rider"), 'q');
  dialog::add_action([] { quitmainloop = true; }); */
  dialog::display();
  }

void nil_set_geodesic() {
  pmodel = mdGeodesic;
  nisot::geodesic_movement = true;
  popScreen();
  }

void nil_set_perspective() {
  pmodel = mdPerspective;
  nisot::geodesic_movement = false;
  pconf.rotational_nil = 0;
  }

void nil_projection() {
  cmode = sm::VR_MENU | sm::NOSCR; gamescreen();
  dialog::init(XLAT("projection of Nil"), 0xC0C0FFFF, 150, 100);
  dialog::addBoolItem("geodesics", pmodel == mdGeodesic, 'g');
  dialog::add_action([] { popScreen(); nil_set_geodesic(); });
  dialog::addInfo("In this mode, the light is assumed to travel along the geodesics (the shortest paths in Nil).");
  dialog::addBreak(100);
  dialog::addBoolItem("constant direction", pmodel == mdPerspective, 'c');
  dialog::add_action([] { popScreen(); nil_set_perspective(); });
  dialog::addInfo("In this mode, the light is assumed to travel along the lines of constant direction.");
  dialog::addBreak(100);
  dialog::addBack();
  dialog::display();
  }

void settings() {
  cmode = sm::VR_MENU | sm::NOSCR; gamescreen();
  dialog::init(XLAT("settings"), 0xC0C0FFFF, 150, 100);
  add_edit(aimspeed_key_x);
  add_edit(aimspeed_key_y);
  add_edit(aimspeed_mouse_x);
  add_edit(aimspeed_mouse_y);
  add_edit(whrad);
  add_edit(whdist);
  add_edit(min_gfx_slope);
  add_edit(stepped_display);
  add_edit(simulation_speed);
  add_edit(ghost_repeat);
  dialog::addBreak(100);
  add_edit(my_scheme.wheel1);
  add_edit(my_scheme.wheel2);
  add_edit(my_scheme.seat);
  add_edit(my_scheme.seatpost);
  dialog::addBreak(100);
  dialog::addItem("projection", 'P');
  dialog::add_action_push(nil_projection);
  dialog::addItem("configure keys", 'k');
  dialog::add_action_push(multi::get_key_configurer(1, move_names, "Nilrider keys", multi::scfg_default));

  #if CAP_AUDIO
  add_edit(effvolume);
  add_edit(musicvolume);
  #endif

  #if CAP_VR
  vrhr::enable_button();
  vrhr::reference_button();
  #endif

  dialog::addItem("RogueViz settings", 'r');
  dialog::add_key_action('r', [] {
    pushScreen(showSettings);
    });

  #if CAP_FILES && !ISWEB
  dialog::addItem("save the current config", 's');
  dialog::add_action([] {
    dynamicval<eGeometry> g(geometry, gNormal);
    saveConfig();
    });
  #endif

  dialog::addBreak(100);
  dialog::addBack();
  dialog::display();
  }

template<class T, class U, class V> void replays_of_type(vector<T>& v, const U& loader, const V& ghost_loader) {
  int i = 0;
  for(auto& r: v) {
    dialog::addItem(r.name, 'a');
    dialog::add_action([&v, i, loader, ghost_loader] {
      pushScreen([&v, i, loader, ghost_loader] {
        cmode = sm::VR_MENU | sm::NOSCR; gamescreen();
        dialog::init(XLAT(planning_mode ? "saved plan" : "replay"), 0xC0C0FFFF, 150, 100);
        dialog::addInfo(v[i].name);

        dialog::addItem(planning_mode ? "load plan" : "load replay", 'l');
        dialog::add_action([&v, i, loader] { popScreen(); loader(v[i]); });

        dialog::addItem(planning_mode ? "load plan as ghost" : "load replay as ghost", 'g');
        dialog::add_action([&v, i, ghost_loader] { popScreen(); ghost_loader(v[i]); });

        dialog::addItem("rename", 'r');
        dialog::add_action([&v, i] {
          popScreen();
          dialog::edit_string(v[i].name, planning_mode ? "name plan" : "name replay", "");
          dialog::get_di().reaction_final = [] { save(); };
          });

        dialog::addItem("delete", 'd');
        dialog::add_action([&v, i] {
          popScreen();
          dialog::push_confirm_dialog(
            [&v, i] { v.erase(v.begin() + i); save(); },
            "Are you sure you want to delete '" + v[i].name + "'?"
            );
          });

        dialog::display();
        });
      });
    i++;
    }
  }

#if CAP_SAVE

void replays() {
  cmode = sm::VR_MENU | sm::NOSCR; gamescreen();
  dialog::init(XLAT(planning_mode ? "saved plans" : "replays"), 0xC0C0FFFF, 150, 100);
  if(!planning_mode) replays_of_type(curlev->manual_replays, [] (manual_replay& r) {
    view_replay = false;
    loaded_or_planned = true;
    curlev->history = curlev->headings_to_history(r);
    toggle_replay();
    popScreen();
    }, [] (manual_replay& r) { curlev->load_manual_as_ghost(r); });
  if(planning_mode) replays_of_type(curlev->plan_replays, [] (plan_replay& r) {
    view_replay = false;
    curlev->history.clear();
    curlev->plan = r.plan;
    popScreen();
    }, [] (plan_replay& r) { curlev->load_plan_as_ghost(r); });
  dialog::addBreak(100);
  if(isize(curlev->ghosts)) {
    dialog::addSelItem("forget all ghosts", its(isize(curlev->ghosts)), 'G');
    dialog::add_action([] { curlev->ghosts.clear(); });
    }
  else if(isize(curlev->manual_replays) || isize(curlev->plan_replays)) {
    dialog::addSelItem("load all plans and replays as ghosts", its(isize(curlev->manual_replays) + isize(curlev->plan_replays)), 'G');
    dialog::add_action([] { curlev->load_all_ghosts(); });
    }
  else dialog::addBreak(100);

  if(planning_mode) {
    dialog::addItem("save the current plan", 's');
    dialog::add_action([] {
      curlev->plan_replays.emplace_back(plan_replay{new_replay_name(), my_scheme, curlev->plan});
      save();
      });
    }
  else {
    dialog::addItem("save the current replay", 's');
    dialog::add_action(save_manual_replay);
    }

  dialog::addBack();
  dialog::display();
  }

void pop_and_push_replays() {
  popScreen();
  pushScreen(replays);
  }
#endif

reaction_t on_quit = [] { quitmainloop = true; };

void restart() {
  clear_path(curlev);
  }

string nilrider_help =
  "You ride a unicycle and need to reach the goal triangles. The unicycle is powered only by the gravity.\n\n"
  "The twist is that the game takes place in a world with Nil geometry! You know those impossible staircases and "
  "waterfalls and triangles; Nil makes these possible. You gain speed simply by going in circles!\n\n"

  "More precisely, every point in Nil has well-defined North, South, East, West, Up, and Down directions. "
  "However, the up/down direction works different than in the Euclidean geometry: when you make a loop which "
  "would return you to the same place in the Euclidean geometry, its counterpart in Nil changes your vertical "
  "coordinate by the value proportional to the signed area of the loop projected on the NESW plane.\n\n"

  "Nil Rider has two modes: manual (control the unicycle manually) and planning (try to construct the best path possible).\n\n"

  "The world is viewed as it would be seen by a camera that was in our simulated world, assuming Fermat's principle (that the "
  "light travels along geodesics, i.e., locally shortest lines). Most geodesics in Nil are helical.";

string nilrider_instruments_help =

  "The instrument with the blue arrow is the compass. It shows the current compass direction (NESW).\n\n"

  "The instrument with the green arrow is the clinometer. If it points up, you are going up slope (and thus slowing down), if it points down, you are going down (and thus accelerating).\n\n"

  "The gray line is the minimum camera angle (but the camera never goes below the current slope).\n\n"

  "The instrument with the red arrow is the speed meter. It shows the current kinetic energy (proportional to speed squared).\n\n"

  "If you return to the same location, your kinetic energy changes by the signed area of the loop (projected on the NESW plane). In most levels, the unit of energy (as shown on the speed meter) is a square (16x16 pixels).\n\n";

void help_instruments();

void help_main() {
  gotoHelp(nilrider_help);
  help_extensions.push_back(help_extension{'v', "video about Nil geometry", [] () {
    open_url("https://youtu.be/FNX1rZotjjI");
    }});
  help_extensions.push_back(help_extension{'i', "what do the instruments do?", [] () { popScreen(); help_instruments(); }});
  }

void help_instruments() {
  gotoHelp(nilrider_instruments_help);
  help_extensions.push_back(help_extension{'r', "return", [] () { popScreen(); help_main(); }});
  }

void main_menu() {
  clearMessages();
  poly_outline = 0xFF;
  cmode = sm::VR_MENU | sm::NOSCR; gamescreen();
  dialog::init(XLAT("Nil Rider"), 0xC0C0FFFF, 150, 100);

  dialog::addItem("continue", 'c');
  dialog::add_action(popScreen);

  if(!planning_mode) {
    dialog::addItem("restart", 'r');
    dialog::add_action([] {
      clear_path(curlev);
      popScreen();
      });
  
    dialog::addItem("view the replay", 'v');
    dialog::add_action(toggle_replay);
  
    #if CAP_SAVE
    dialog::addItem("saved replays", 's');
    dialog::add_action(pop_and_push_replays);
    #endif
    }
  else {
    #if CAP_SAVE
    dialog::addItem("saved plans", 's');
    dialog::add_action(pop_and_push_replays);
    #endif
    }

  dialog::addItem("track / mode / goals", 't');
  dialog::add_action([] { popScreen(); pushScreen(pick_game); });

  dialog::addItem("help", 'h');
  dialog::add_action(help_main);

  dialog::addItem("change settings", 'o');
  dialog::add_action_push(settings);

  #if CAP_VIDEO
  dialog::addItem("record video", 'v');
  dialog::add_action([] {
    dialog::openFileDialog(anims::videofile, XLAT("record to video file"),
      ".mp4", [] {
        anims::period = isize(curlev->history);
        anims::noframes = anims::period * 60 / 1000;
        int a = addHook(anims::hooks_anim, 100, [] {
          int ttick = ticks % isize(curlev->history);
          curlev->current = curlev->history[ttick];
          curlev->current.centerview(curlev);
          anims::moved();
          });
        int af = addHook(hooks_frame, 100, [] {
          int ttick = ticks % isize(curlev->history);
          curlev->current = curlev->history[ttick];
          if(planning_mode && !view_replay) curlev->draw_planning_screen();
          });
        bool b = anims::record_video_std();
        delHook(anims::hooks_anim, a);
        delHook(hooks_frame, af);
        return b;
        });
    });
  #endif

  dialog::addItem("quit", 'q');
  dialog::add_action([] { 
    on_quit();
    });
  
  dialog::display();
  }

bool on;

local_parameter_set lps_nilrider("nilrider:");

void nilrider_keys() {
  multi::change_default_key(lps_nilrider, SDL12(SDLK_LCTRL, SDL_SCANCODE_LCTRL), 16 + nrFineControl);
  multi::change_default_key(lps_nilrider, SDL12('p', SDL_SCANCODE_P), 16 + nrPause);
  multi::change_default_key(lps_nilrider, SDL12('b', SDL_SCANCODE_B), 16 + nrReverseTime);
  multi::change_default_key(lps_nilrider, SDL12('r', SDL_SCANCODE_R), 16 + nrViewSimulation);
  multi::change_default_key(lps_nilrider, SDL12('v', SDL_SCANCODE_V), 16 + nrMenu);
  }

bool nilrider_music(eLand& l) {
  if(planning_mode && !view_replay)
    l = music_nilrider_planning;
  else if(paused)
    l = music_nilrider_paused;
  else if(!running)
    l = music_nilrider_nonrunning;
  else if(backing)
    l = music_nilrider_back;
  else l = music_nilrider;
  return false;
  }

void default_settings() {
  nilrider_keys();

  lps_add(lps_nilrider, vid.cells_drawn_limit, 1);
  lps_add(lps_nilrider, ccolor::plain.ctab, colortable{0});
  lps_add(lps_nilrider, smooth_scrolling, true);
  lps_add(lps_nilrider, mapeditor::drawplayer, false);
  lps_add(lps_nilrider, backcolor, 0xC0C0FFFF);
  lps_add(lps_nilrider, logfog, 1);
  lps_add(lps_nilrider, ccolor::which, &ccolor::plain);
  lps_add(lps_nilrider, ccolor::rwalls, 0);
  lps_add(lps_nilrider, game_keys_scroll);

  #if CAP_VR
  lps_add(lps_nilrider, vrhr::hsm, vrhr::eHeadset::reference);
  lps_add(lps_nilrider, vrhr::eyes, vrhr::eEyes::equidistant);
  lps_add(lps_nilrider, vrhr::absolute_unit_in_meters, 6);
  #endif
  }

void vrqm_ext() {
  dialog::addItem("restart Nil Rider", 'r');
  dialog::add_action([] {
    println(hlog, "nilrider restart");
    nilrider::restart();
    });
  dialog::addBoolItem("stepped Nil Rider", nilrider::stepped_display, 's');
  dialog::add_action([] {
    println(hlog, "nilrider stepped");
    nilrider::stepped_display = !nilrider::stepped_display;
    });
  }

void initialize() {
  load();

  check_cgi();
  cgi.prepare_shapes();
  
  init_statues();
  
  curlev->init();

  param_enum(planning_mode, "nil_planning", false)
    -> editable({{"manual", "control the unicycle manually"}, {"planning", "try to plan the optimal route!"}}, "game mode", 'p');

  param_enum(stepped_display, "stepped_display", false)
    -> editable({{"smooth", "ride on a smooth surface"}, {"blocky", "makes slopes more visible -- actual physics are not affected"}}, "game mode", 's');

  param_i(nilrider_tempo, "nilrider_tempo");
  param_i(nilrider_shift, "nilrider_shift");

  param_f(simulation_speed, "nilrider_simulation_speed")
  -> editable(0.1, 5, 0, "Nil Rider simulation speed",
      "If you want to go faster, make this higher.", 'z')
  -> set_sets([] { dialog::bound_low(0); dialog::scaleLog(); });

  param_f(ghost_repeat, "ghost_repeat")
  -> editable(0.01, 999, 0, "ghost repeat period",
      "will repeat ghosts every time interval (in seconds).", 'z')
  -> set_sets([] { dialog::bound_low(0.01); dialog::scaleLog(); });

  param_color(my_scheme.wheel1, "color:wheel1", true, my_scheme.wheel1)->editable("wheel color 1", "", 'w');
  param_color(my_scheme.wheel2, "color:wheel2", true, my_scheme.wheel2)->editable("wheel color 2", "", 'x');
  param_color(my_scheme.seat, "color:seat", true, my_scheme.seat)->editable("seat color", "", 'p');
  param_color(my_scheme.seatpost, "color:seatpost", true, my_scheme.seatpost)->editable("seatpost color", "", 'o');

  rv_hook(hooks_frame, 100, frame);
  rv_hook(shmup::hooks_turn, 100, turn);
  rv_hook(hooks_resetGL, 100, cleanup_textures);
  rv_hook(hooks_music, 100, nilrider_music);
  rv_hook(hooks_sync_music, 100, sync_music);
  rv_hook(vrhr::vr_quickmenu_extensions, 101, vrqm_ext);
  on = true;
  on_cleanup_or_next([] { on = false; });
  pushScreen(run);
  }

void initialize_all() {
  showstartmenu = false;
  stop_game();
  geometry = gNil;
  variation = eVariation::pure;
  nil_set_geodesic();
  enable_canvas();
  lps_enable(&lps_nilrider);
  initialize();
  poly_outline = 0xFF;
  pushScreen(pick_game);
  start_game();
  }

void initialize_for_slide(tour::presmode mode) {
  setWhiteCanvas(mode, [] { set_geometry(gNil); set_variation(eVariation::pure); });
  if(mode == tour::pmStart) {
    tour::slide_backup(pmodel, mdGeodesic);
    tour::slide_backup(nisot::geodesic_movement, true);
    lps_enable(&lps_nilrider);
    tour::slide_backup(poly_outline, 0xFF);
    stop_game();
    initialize();
    start_game();
    }
  if(mode == tour::pmStop) lps_enable(nullptr);
  }

auto celldemo = arg::add3("-unilcycle", initialize) + arg::add3("-unilplan", [] { planning_mode = true; }) + arg::add3("-viewsim", [] { view_replay = true; })
  + arg::add3("-oqc", [] { on_quit = popScreenAll; })
  + arg::add3("-nilsolve-set", [] {
    arg::shift(); solver_unit = arg::argf();
    arg::shift(); nospeed = arg::argi();
    arg::shift(); goal_id = arg::argi();
    curlev->solve(); })
  + arg::add3("-nilsolve", [] { curlev->solve(); })
  + arg::add3("-nilgeo", nil_set_geodesic)
  + arg::add3("-nilper", nil_set_perspective)
  + arg::add3("-nilrider", initialize_all)
  + arg::add3("-nilrider-q", [] { arg::shift(); reduce_quality = arg::argi(); })
  + addHook(hooks_configfile, 100, [] {
    param_f(aimspeed_key_x, "nilrider_key_x")
    ->editable(-5, 5, 0.1, "navigation sensitivity (keyboard)", "press Left/Right to navigate (lCtrl to fine-tune)", 'n');
    param_f(aimspeed_key_y, "nilrider_key_y")
    ->editable(-5, 5, 0.1, "camera sensitivity (keyboard)", "press Up/Down to set the camera angle (lCtrl to fine-tune)", 'c');
    param_f(aimspeed_mouse_x, "nilrider_mouse_x")
    ->editable(-5, 5, 0.1, "navigation sensitivity (mouse/vr)", "move mouse Left/Right to navigate (lCtrl to fine-tune)", 'N');
    param_f(aimspeed_mouse_y, "nilrider_mouse_y")
    ->editable(-5, 5, 0.1, "camera sensitivity (mouse/vr)", "move mouse Up/Down to set the camera angle (lCtrl to fine-tune)", 'C');
    param_f(whrad, "nilrider_radius")
    ->editable(0, 0.5, 0.01, "wheel radius", "note: this parameter is just visual, it does not affect the physics in any way", 'w');
    param_f(whdist, "nilrider_dist")
    ->editable(0, 5, 0.05, "camera distance", "how far is the unicycle from the camera", 'd')
    ->set_reaction([] { curlev->current.centerview(curlev); });
    param_f(min_gfx_slope, "min_gfx_slope")
    ->editable(-90._deg, 90._deg, degree, "min camera slope", "affected by up/down", 'm');
    })
  + arg::add3("-fullsim", [] {
    /* for animations */
    popScreenAll();
    rv_hook(anims::hooks_anim, 100, [] {
      int ttick = ticks % isize(curlev->history);
      curlev->current = curlev->history[ttick];  
      curlev->current.centerview(curlev);
      anims::moved();
      });
    }) + arg::add3("-unillevel", [] {
      arg::shift();
      for(auto l: all_levels) if(appears(l->name, arg::args())) curlev = l;
      if(on) curlev->init();
      })
    + arg::add3("-load-level", [] {
      arg::shift(); load_level(arg::args(), true);
      })
    + arg::add3("-simplemodel", [] {
      nisot::geodesic_movement = false;
      pmodel = mdPerspective;
      pconf.rotational_nil = 0;
      })
    + arg::add3("-ghost-all", [] {
      curlev->load_all_ghosts();
      });

auto hook0= addHook(hooks_configfile, 300, default_settings);

#ifdef NILRIDER
auto hook1=
    addHook(hooks_config, 100, [] {
      if(arg::curphase == 1)
        conffile = "nilrider.ini";
      if(arg::curphase == 2) initialize_all();
      });
#endif

}
