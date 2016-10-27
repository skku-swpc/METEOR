#include "Dalvik.h"

void auxValuePrint(FILE* f, JValue val, char type) {
  switch(type) {
    case 'Z':
      fprintf(f, val.z ? "true" : "false");
      break;
    case 'B':
      fprintf(f, "%d", (s4)val.b);
      break;
    case 'C': {
      u4 v = val.c;
      if(32 <= v && v < 128) {
        fprintf(f, "'%c'", (char)v);
      } else if(v == '\n') {
        fprintf(f, "'\\n'");
      } else if(v == '\t') {
        fprintf(f, "'\\t'");
      } else {
        fprintf(f, "%u", v);
      }
      break;
    } case 'S':
      fprintf(f, "%d", (s4)val.s);
      break;
    case 'I':
      fprintf(f, "%d", val.i);
      break;
    case 'F':
      fprintf(f, "%f", val.f);
      break;
    case 'J':
      fprintf(f, "%lld", val.j);
      break;
    case 'D':
      fprintf(f, "%f", val.d);
      break;
    case '[':
    case 'L': {
      Object* obj = (Object*)val.l;
      if(obj) {
        fprintf(f, "#%X", auxObjectToId(obj));
      } else {
        fprintf(f, "NULL");
      }
      break;
    } default:
      assert(0 && "Unknown type");
  }
}
