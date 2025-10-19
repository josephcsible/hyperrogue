// RogueViz -- SAG embedder: data manager
// Copyright (C) 2011-24 Zeno Rogue, see 'hyper.cpp' for details

#include "../rogueviz.h"

namespace rogueviz {
namespace sag {

using namespace cells;

edgetype *sag_edge;

/** if this is true, no nodes are allowed to be on the same subcell */
bool allow_doubles = false;

/** node i is on sagcells[sagid[i]] */
vector<int> sagid;

/** what node is on sagcells[i] (need loglik_repeat to be off) */
vector<int> sagnode;

/* separate hubs -- only for smClosest */
ld hub_penalty;
string hub_filename;
vector<int> hubval;

vector<edgeinfo> sagedges;  
vector<vector<int>> edges_yes, edges_no;
vector<vector<pair<int, double>>> edge_weights;

vector<bool> fixed_position;

ld edgepower=1, edgemul=1;

void init();
void compute_cost();

void prepare_graph() {
  int DN = isize(sagid);
  DEBBI(debug_init_sag, ("prepare_graph with DN = ", DN));

  set<pair<int, int>> alledges;
  for(auto e: sagedges) {
    if(e.i == e.j) continue;
    alledges.emplace(e.i, e.j);
    alledges.emplace(e.j, e.i);
    }
  
  edges_yes.clear(); edges_yes.resize(DN);
  edges_no.clear(); edges_no.resize(DN);

  fixed_position.clear(); fixed_position.resize(DN);
  
  for(int i=0; i<DN; i++) for(int j=0; j<DN; j++) if(i != j) {
    if(alledges.count({i, j}))
      edges_yes[i].push_back(j);
    else
      edges_no[i].push_back(j);
    }          

  edge_weights.clear(); edge_weights.resize(DN);
  for(auto& e: sagedges) {
    if(e.i == e.j) continue;
    e.weight2 = pow((double) e.weight, (double) edgepower) * edgemul;
    edge_weights[e.i].emplace_back(e.j, e.weight2);
    edge_weights[e.j].emplace_back(e.i, e.weight2);
    }

  sagnode.clear();
  sagnode.resize(isize(sagcells), -1);
  for(int i=0; i<DN; i++)
    sagnode[sagid[i]] = i;
  compute_cost();
  }

void set_inverse();

void place_correctly() {
  int DN = isize(sagid);
  vector<int> qon(isize(sagcells), 0);
  for(int i=0; i<DN; i++) qon[sagid[i]]++;
  vector<int> qsf(isize(sagcells), 0);

  ld rad = .25 * cgi.scalefactor;
  if(isize(subcell_points) > 1) rad /= pow(isize(subcell_points), WDIM);

  for(int i=0; i<DN; i++) {
    int ci = sag::sagid[i];
    vdata[i].m->base = sagcells[ci].first;
    vdata[i].m->at = Id;

    if(allow_doubles) vdata[i].m->at = 
      spin(TAU*(qsf[ci]++) / qon[ci]) * xpush(rad * (qon[ci]-1) / qon[ci]);

    if(isize(subcell_points) > 1)
      vdata[i].m->at = rgpushxto0(subcell_points[sagcells[ci].second]) * vdata[i].m->at;
    }
  }

bool visualization_active;

void forgetedges(int id) {
  for(int i=0; i<isize(vdata[id].edges); i++) 
    vdata[id].edges[i].second->orig = NULL;
  }
  
void create_viz() {
  if(distance_only) return;
  int DN = isize(sagid);

  bool vact = state & SS_GRAPH;
  state |= SS_GRAPH;

  if(!vact) for(int i=0; i<DN; i++) vdata[i].data = 0;
  if(!vact) for(auto& e: sagedges) addedge0(e.i, e.j, &e);

  if(sagcells[0].first == nullptr) return;

  if(vact) for(int i=0; i<DN; i++) forgetedges(i);
  if(!vact) for(int i=0; i<DN; i++) {
    vertexdata& vd = vdata[i];
    vd.cp = colorpair(dftcolor);

    rogueviz::createViz(i, sagcells[sagid[i]].first, Id);
    }

  place_correctly();
  if(!vact) storeall();
  if(vact) shmup::fixStorage();
  set_inverse();
  vact = true;  
  }

/** save the SAG solution (sagid) */
void save_sag_solution(const string& fname) {
  if(!(state & SS_DATA)) throw hr_exception("save_sag_solution with no data");
  DEBBI(debug_init_sag, ("Saving the sag solution to: ", fname));
  FILE *f = fopen(fname.c_str(), "wt");
  if(!f) return file_error(fname);
  for(int i=0; i<isize(sagid); i++)
    fprintf(f, "%s;%d\n", vdata[i].name.c_str(), sagid[i]);
  fclose(f);
  }

/** load the SAG solution (sagid) */
void load_sag_solution(const string& fname) {
  if(!(state & SS_DATA)) throw hr_exception("load_sag_solution with no data");
  DEBBI(debug_init_sag, ("Loading the sag solution from: ", fname));
  FILE *sf = fopen(fname.c_str(), "rt");
  if(!sf) return file_error(fname);
  int SN = isize(sagcells);
  if(sf) while(true) {
    string lab;
    while(true) {
      int c = fgetc(sf);
      if(c == EOF) goto afterload;
      else if(c == ',' || c == ';') break;
      else if(rv_ignore(c)) ;
      else lab += c;
      }
    int sid = -1;
    int err = fscanf(sf, "%d", &sid);
    if(sid < 0 || sid >= SN || err < 1) sid = -1;
    if(!labeler.count(lab)) {
      printf("unknown vertex: %s\n", lab.c_str());
      }
    else {
      int id = getid(lab);
      sagid[id] = sid;
      }
    }
  afterload: 
  if(sf) fclose(sf);

  prepare_graph();
  create_viz();
  }

void load_sag_solution_basic(const string& fname) {
  if(!(state & SS_DATA)) throw hr_exception("load_sag_solution_basic with no data");
  DEBBI(debug_init_sag, ("Loading the sag solution (basic) from: ", fname));
  FILE *f = fopen(fname.c_str(), "rt");
  if(!f) return file_error(fname);
  for(auto& i: sagid) if(fscanf(f, "%d", &i) < 1) return file_format_error(fname);
  fclose(f);
  if(debug_init_sag) println(hlog, "loaded sagid = ", sagid);

  prepare_graph();
  create_viz();
  }

void after_data() {
  state |= SS_DATA;
  init_snake_if_needed();
  int DN = isize(vdata);
  int SN = isize(sagcells);
  if(SN < DN) {
    println(hlog, "SN = ", SN, " DN = ", DN);
    throw hr_exception("not enough cells for SAG");
    }
  sagid.resize(DN);
  for(int i=0; i<DN; i++) sagid[i] = i;
  prepare_graph();
  create_viz();
  }

/** load all the edges */
void read_weighted(const char *fname) {

  if(state & SS_DATA) return;
  DEBBI(debug_init_sag, ("Loading the weighted daga for sag from: ", fname));
  state |= SS_WEIGHTED;
  init_cells();

  maxweight = 0;
  fhstream f(fname, "rt");
  if(!f.f) return file_error(fname);

  while(!feof(f.f)) {
    string l1, l2;
    while(true) {
      int c = fgetc(f.f);
      if(c == EOF) goto after;
      else if(c == ';') break;
      else if(rv_ignore(c)) ;
      else l1 += c;
      }
    while(true) {
      int c = fgetc(f.f);
      if(c == EOF) goto after;
      else if(c == ';') break;
      else if(rv_ignore(c)) ;
      else l2 += c;
      }
    ld wei;
    if(!scan(f, wei)) continue;
    edgeinfo ei(sag_edge);
    ei.i = getid(l1);
    ei.j = getid(l2);
    ei.weight = wei;
    sagedges.push_back(ei);
    }

  after:
  after_data();
  }

/** load edges, in  */
void read_unweighted(const char *fname) {

  if(state & SS_DATA) return;
  DEBBI(debug_init_sag, ("Loading the unweighted daga for sag from: ", fname));
  init_cells();  

  fhstream f(fname, "rt");
  if(!f.f) return file_error(fname);

  scanline(f);
  set<pair<int, int> > edges;
  
  int all = 0, good = 0;
  while(!feof(f.f)) {        
    string l1 = scan<string>(f);
    string l2 = scan<string>(f);
    if(l1 == "") continue;
    if(l2 == "") continue;
    edgeinfo ei(sag_edge);
    ei.i = getid(l1);
    ei.j = getid(l2);
    if(ei.i > ei.j) swap(ei.i, ei.j);
    all++;
    if(edges.count({ei.i, ei.j})) continue;
    good++;
    edges.emplace(ei.i, ei.j);
    ei.weight = 1;
    sagedges.push_back(ei);
    }

  println(hlog, "N = ", isize(vdata), " edges = ", good, "/", all);
  after_data();
  }
  
void read_hubs(const string& fname) {
  if(!(state & SS_DATA)) throw hr_exception("read_hubs with no data");
  DEBBI(debug_init_sag, ("Loading the hub daga for sag from: ", fname));
  hubval.resize(isize(vdata), -1);
  fhstream f(fname, "rt");
  if(!f.f) return file_error(fname);
  while(!feof(f.f)) {
    string l1, l2;
    while(true) {
      int c = fgetc(f.f);
      if(c == EOF) return;
      else if(c == ';') break;
      else if(rv_ignore(c)) ;
      else l1 += c;
      }
    while(true) {
      int c = fgetc(f.f);
      if(c == EOF) return;
      else if(c == ';') return;
      else if(rv_ignore(c)) break;
      else l2 += c;
      }
    if(!id_known(l1)) {
      printf("label unknown: %s\n", l1.c_str());
      throw hr_exception("unknown label in read_hubs");
      }
    hubval[getid(l1)] = atoi(l2.c_str());
    }
  }

void generate_fake_data(int n, int m) {
  if(state & SS_DATA) return;
  DEBBI(debug_init_sag, ("Generating fake data ", tie(n, m)));
  init_cells();
  state |= SS_WEIGHTED;

  sagid.resize(n);
  for(int i=0; i<n; i++) sagid[i] = i;
  hrandom_shuffle(sagid);
  if(m > n || m < 0) throw hr_exception("generate_fake_data parameters incorrect");
  sagid.resize(m);
  int DN = isize(sagid);
  vdata.resize(DN);
  for(int i=0; i<DN; i++)
    vdata[i].name = its(i) + "@" + its(sagid[i]);

  sag_edge = add_edgetype("SAG edge");
  for(int i=0; i<DN; i++)
  for(int j=i+1; j<DN; j++) {
    edgeinfo ei(sag_edge);
    ei.i = i;
    ei.j = j;
    ei.weight = 1. / sagdist[sagid[i]][sagid[j]];
    sagedges.push_back(ei);
    }

  after_data();

  for(int i=0; i<DN; i++) {
    color_t col = ccolor::formula(sagcells[sagid[i]].first);
    col <<= 8;
    col |= 0xFF;
    vdata[i].cp.color1 = vdata[i].cp.color2 = col;
    }
  }

int data_read_args() {
#if CAP_COMMANDLINE
  using namespace arg;

  if(0) ;
  else if(argis("-sagmin")) {
    auto& ed = sag_edge ? *sag_edge : default_edgetype;
    shift_arg_formula(ed.visible_from);
    ed.visible_from_hi = ed.visible_from;
    }
  else if(argis("-sagminhi")) {
    auto& ed = sag_edge ? *sag_edge : default_edgetype;
    shift_arg_formula(ed.visible_from_hi);
    }
  else if(argis("-sag-edgepower")) {
    shift_arg_formula(sag::edgepower);
    shift_arg_formula(sag::edgemul);
    }
  else if(argis("-sag-weighted")) {
    PHASE(3); 
    shift(); sag::read_weighted(argcs());
    }
  else if(argis("-sag-unweighted")) {
    PHASE(3); 
    shift(); sag::read_unweighted(argcs());
    }
  else if(argis("-saghubs")) {
    PHASE(3); 
    shift_arg_formula(sag::hub_penalty);
    shift(); sag::read_hubs(argcs());
    }
  else if(argis("-sag-generate")) {
    PHASE(3);
    shift(); int n = argi();
    shift(); int m = argi();
    sag::generate_fake_data(n, m);
    }
// (3) load the initial positioning
  else if(argis("-sag-load-sol")) {
    PHASE(3); shift(); sag::load_sag_solution(args());
    }
  else if(argis("-sag-load-solution")) {
    PHASE(3); shift(); sag::load_sag_solution_basic(args());
    }
  else if(argis("-sag-save-sol")) {
    PHASE(3); shift(); sag::save_sag_solution(args());
    }

  else if(argis("-sag-fix")) {
    shift(); int id = getid(args());
    if(id >= isize(sagid)) throw hr_exception("bad id in -sag-fix");
    fixed_position[id] = true;
    }

  else if(argis("-sag-move-to")) {
    shift(); int sid1 = getid(args());
    if(sid1 < 0 || sid1 >= isize(sagid)) throw hr_exception("bad id in -sag-move-to");
    shift(); int t2 = argi();
    if(t2 < 0 || t2 >= isize(sagnode)) throw hr_exception("bad id in -sag-move-to");
    int sid2 = sagid[t2];
    int t1 = allow_doubles ? -1 : sagnode[sid1];
    sagnode[sid1] = t2; sagid[t2] = sid1;
    if(sid2 >= 0) sagnode[sid2] = t1; sagid[t1] = sid2;
    compute_cost();
    create_viz();
    }


  else return 1;  
#endif
  return 0;
  }

int ahdata = addHook(hooks_args, 100, data_read_args);



}
}
