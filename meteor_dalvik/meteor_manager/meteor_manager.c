#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/fsuid.h>

#define EXIT_FAILED 127

int main(int argc, char** argv) {
  int* uid_list = NULL;
  int uid_list_sz = 0;
  int uid_list_cap = 0;

  if(argc != 2 && argc != 3) {
    printf("cometmanager [check|exempt|clear] [cuid]\n");
    return 0;
  }

  int cuid = getuid(); // The caller's uid.
  int in_list = 0;

  setuid(0);
  setgid(0);
  umask(077);

  if(argc == 3) {
    /* If we called as root or system we can request on behalf of any uid. */
    if(cuid == 0 || cuid == 1000) {
      cuid = atoi(argv[2]);
    } else {
      return EXIT_FAILED;
    }
  }

  FILE* fin = fopen("/data/cometdata", "r");
  if(fin) {
    int uid;
    while(1 == fscanf(fin, "%d", &uid)) {
      if(uid_list_sz == uid_list_cap) {
        uid_list_cap = uid_list_cap * 2 + 2;
        uid_list = (int*)realloc(uid_list, sizeof(int) * uid_list_cap);
        if(!uid_list) {
          return EXIT_FAILED;
        }
      }
      uid_list[uid_list_sz++] = uid;
      in_list |= uid == cuid;
    }
    fclose(fin);
  }

  int i;
  int res = 0;
  const char* cmd = *++argv;
  if(!strcmp(cmd, "check")) {
    res = in_list || cuid < 10000;
  } else if(!strcmp(cmd, "exempt") || (!strcmp(cmd, "toggle") && !in_list)) {
    if(!in_list) {
      FILE* fout = fopen("/data/cometdata.tmp", "w");
      for(i = 0; i < uid_list_sz; i++) {
        fprintf(fout, "%d\n", uid_list[i]);
      }
      fprintf(fout, "%d\n", cuid);
      fclose(fout);

      if(fin) {
        unlink("/data/cometdata");
      }
      link("/data/cometdata.tmp", "/data/cometdata");
      unlink("/data/cometdata.tmp");
    }
    res = 1;
  } else if(!strcmp(cmd, "clear") || (!strcmp(cmd, "toggle") && in_list)) {
    if(in_list) {
      FILE* fout = fopen("/data/cometdata.tmp", "w");
      for(i = 0; i < uid_list_sz; i++) {
        if(uid_list[i] != cuid) {
          fprintf(fout, "%d\n", uid_list[i]);
        }
      }
      fclose(fout);

      if(fin) {
        unlink("/data/cometdata");
      }
      link("/data/cometdata.tmp", "/data/cometdata");
      unlink("/data/cometdata.tmp");
    }
    res = 0;
  }
  return res;
}
