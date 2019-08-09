#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>

using namespace std;

int indent = 2;

string ind() { string s; for(int i=0; i<indent; i++) s += ' '; return s; }

string which_file;

void mark_file() {
  if(which_file != "") {
    cout << "\n" << ind() << "// implemented in: " << which_file << "\n\n";
    which_file = "";
    }
  }

bool do_endif;


void gen(string s) {
  which_file = s;
  do_endif = false;
  ifstream in(s);
  while(getline(in, s)) {
    while(s != "" && s[0] == ' ') s = s.substr(1);
    while(s.back() == 10 || s.back() == 13) s = s.substr(0, s.size() - 1);
    if(s.substr(0, 7) == "#if EX ") {
      cout << ind() << s << "\n";
      do_endif = true;
      }
    if(s.substr(0, 4) == "//EX") {
      auto t = s.substr(4);
      while(t != "" && t[0] == ' ') t = t.substr(1);
      cout << ind() << t << "\n";
      }
    if(s.substr(0, 4) == "EX }") {
      cout << ind() << "}\n";
      indent -= 2;
      }
    else if(s.substr(0, 3) == "EX ") {
      string t = s.substr(3);
      if(t.substr(0, 10) == "namespace ") {
        mark_file();
        cout << ind() << t << "\n";
        indent += 2;
        }
      else {
        for(int i=0;; i++) {
          if(i == int(t.size())) { cerr << "Error: unrecognizable EX: " << s << "\n"; }
          else if(t[i] == '{') {
            while(i && t[i-1] == ' ') i--;
            cout << ind() << t.substr(0, i) << ";\n";
            break;
            }
          else if(t[i] == ';') {
            cout << ind() << "extern " << t << "\n";
            break;
            }
          else if(t[i] == '=') {
            while(i && t[i-1] == ' ') i--;
            cout << ind() << "extern " << t.substr(0, i) << ";\n";
            break;
            }
          }
        }
      }
    }
  
  if(do_endif) {
    cout << ind() << "#endif\n";
    do_endif = false;
    }

  while(indent > 2) {
    cout << ind() << "}\n";
    indent -= 2;
    }
  }

int main(int argc, char ** argv) {
  printf("// This file is generated automatically by makeh.cpp.\n\nnamespace hr {\n");
  indent = 2;
  
  for(int i=1; i<argc; i++) 
    gen(argv[i]);
  
  printf("  }\n");
  }