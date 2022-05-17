#include <stdio.h>
#include <stdlib.h>

#include "help.h"

void usage(int exitcode) {
    fwrite(help_txt, help_txt_len, 1, stdout);
    exit(exitcode);
}
