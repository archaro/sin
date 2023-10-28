// sdiss - Sinistra bytecode disassembler
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>

#include "memory.h"
#include "log.h"
#include "value.h"
#include "item.h"
#include "stack.h"

void usage() {
  logmsg("Sinistra disassembler.\nSyntax: sdiss <options>\n");
  logmsg("Options:\n");
  logmsg(" -h, --help\t\tThis message.\n");
  logmsg(" -o, --object <file>\tObject code to disassemble.\n");
}

int main(int argc, char **argv) {
  FILE *in;
  int filesize = 0;
  uint8_t *bytecode = NULL, *opcodeptr;

  if (argc < 2) {
    usage();
    exit(EXIT_FAILURE);
  }

  // Are there any interesting options?
  int opt;
  const struct option options[] =
  {
    {"help", no_argument, 0, 'h'},
    {"object", required_argument, 0, 'o'},
    {NULL, 0, 0, '\0'}
  };
  while ((opt = getopt_long(argc, argv, "ho:", options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage();
        exit(EXIT_SUCCESS);
        break;
      case 'o':
        // Mandatory: Name of the object code file.
        in = fopen(optarg, "r");
        if (!in) {
          logerr("Unable to open input file: %s\n", optarg);
          exit(EXIT_FAILURE);
        }
        fseek(in, 0, SEEK_END);
        filesize = ftell(in);
        fseek(in, 0, SEEK_SET);
        bytecode = GROW_ARRAY(unsigned char, bytecode, 0, filesize);
        fread(bytecode, filesize, sizeof(char), in);
        fclose(in);
        logmsg("Bytecode loaded: %d bytes.\n", filesize);
        break;
      default:
        usage();
        return EXIT_FAILURE;
    }
  }

  // Just check to see if we have been given some bytecode.
  if (!bytecode) {
    logerr("No bytecode to process!\n");
    exit(EXIT_FAILURE);
  }

  // We have some bytecode.  Step through it and output some helpful
  // disassembly.  Hopefully helpful, anyway.
  logmsg("Beginning disassembly...\n");
  opcodeptr = bytecode;


  // First, do we have any locals?
  uint8_t locals = *opcodeptr;
  opcodeptr++;
  if (locals > 0) {
    logmsg("Local variables: %d\n", locals);
  } else {
    logmsg("No local variables.\n");
  }

  // Locals processed, so now step through the bytecode until the HALT
  // instruction is found.  This is just one big switch.
  while (*opcodeptr != 'h') {
    int16_t offset;
    int64_t ival;
    logmsg("Byte %05u: ", opcodeptr - bytecode - 1); // -1 for the locals
    switch (*opcodeptr) {
      case 'a':
        logmsg("ADD\n");
        opcodeptr++;
        break;
      case 'c':
        opcodeptr++;
        logmsg("SAVE LOCAL %d\n", *opcodeptr);
        opcodeptr++;
        break;
      case 'd':
        logmsg("DIVIDE\n");
        opcodeptr++;
        break;
      case 'e':
        opcodeptr++;
        logmsg("RETRIEVE LOCAL %d\n", *opcodeptr);
        opcodeptr++;
        break;
      case 'f':
        opcodeptr++;
        logmsg("INCREMENT LOCAL %d\n", *opcodeptr);
        opcodeptr++;
        break;
      case 'g':
        opcodeptr++;
        logmsg("DECREMENT LOCAL %d\n", *opcodeptr);
        opcodeptr++;
        break;
      case 'j':
        opcodeptr++;
        offset = *(int16_t*)opcodeptr;
        opcodeptr += 2;
        logmsg("JUMP %d\n", offset);
        break;
      case 'k':
        opcodeptr++;
        offset = *(int16_t*)opcodeptr;
        opcodeptr += 2;
        logmsg("JUMP IF FALSE %d\n", offset);
        break;
      case 'l':
        opcodeptr++;
        offset = *(int16_t*)opcodeptr; // Length of string
        opcodeptr += 2;
        logmsg("STRINGLIT: ");
        for (uint16_t s = 0; s < offset; s++) {
          logmsg("%c", *opcodeptr);
          opcodeptr++;
        }
        logmsg("\n");
        break;
      case 'm':
        logmsg("MULTIPLY\n");
        opcodeptr++;
        break;
      case 'n':
        logmsg("NEGATE\n");
        opcodeptr++;
        break;
      case 'o':
        logmsg("BOOL EQ\n");
        opcodeptr++;
        break;
      case 'p':
        opcodeptr++;
        ival = *(int64_t*)opcodeptr;
        logmsg("INTEGER %ld\n", ival);
        opcodeptr += 8;
        break;
      case 'q':
        logmsg("BOOL NOTEQ\n");
        opcodeptr++;
        break;
      case 'r':
        logmsg("BOOL LT\n");
        opcodeptr++;
        break;
      case 's':
        logmsg("SUBTRACT\n");
        opcodeptr++;
        break;
      case 't':
        logmsg("BOOL GT\n");
        opcodeptr++;
        break;
      case 'u':
        logmsg("BOOL LTEQ\n");
        opcodeptr++;
        break;
      case 'v':
        logmsg("BOOL GTEQ\n");
        opcodeptr++;
        break;
      default:
        logerr("Undefined opcode: %c (%d)\n", *opcodeptr, *opcodeptr);
        opcodeptr++;
    }
  }
  logmsg("HALT\n");

  // Clean up
  logmsg("Shutting down.\n");
  FREE_ARRAY(unsigned char, bytecode, filesize);
  exit(EXIT_SUCCESS);
}

