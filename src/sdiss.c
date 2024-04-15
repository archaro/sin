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
#include "vm.h"

VM_t vm; // Not needed for disassembler, but needed for linking.

uint8_t *process_item(uint8_t *opcodeptr);
uint8_t *process_dereference(uint8_t *opcodeptr);

// This is used by a few functions to calculate the current opcode location
uint8_t *bytecode;

void usage() {
  logmsg("Sinistra disassembler.\nSyntax: sdiss <options>\n");
  logmsg("Options:\n");
  logmsg(" -h, --help\t\tThis message.\n");
  logmsg(" -o, --object <file>\tObject code to disassemble.\n");
}

int main(int argc, char **argv) {
  FILE *in;
  int filesize = 0;
  uint8_t *opcodeptr;
  bytecode = NULL;

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
    switch (*opcodeptr++) {
      case 'a':
        logmsg("ADD\n");
        break;
      case 'c':
        logmsg("SAVE LOCAL %d\n", *opcodeptr);
        opcodeptr++;
        break;
      case 'd':
        logmsg("DIVIDE\n");
        break;
      case 'e':
        logmsg("RETRIEVE LOCAL %d\n", *opcodeptr);
        opcodeptr++;
        break;
      case 'f':
        logmsg("INCREMENT LOCAL %d\n", *opcodeptr);
        opcodeptr++;
        break;
      case 'g':
        logmsg("DECREMENT LOCAL %d\n", *opcodeptr);
        opcodeptr++;
        break;
      case 'j':
        offset = *(int16_t*)opcodeptr;
        opcodeptr += 2;
        logmsg("JUMP %d\n", offset);
        break;
      case 'k':
        offset = *(int16_t*)opcodeptr;
        opcodeptr += 2;
        logmsg("JUMP IF FALSE %d\n", offset);
        break;
      case 'l':
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
        break;
      case 'n':
        logmsg("NEGATE\n");
        break;
      case 'o':
        logmsg("BOOL EQ\n");
        break;
      case 'p':
        ival = *(int64_t*)opcodeptr;
        logmsg("INTEGER %ld\n", ival);
        opcodeptr += 8;
        break;
      case 'q':
        logmsg("BOOL NOTEQ\n");
        break;
      case 'r':
        logmsg("BOOL LT\n");
        break;
      case 's':
        logmsg("SUBTRACT\n");
        break;
      case 't':
        logmsg("BOOL GT\n");
        break;
      case 'u':
        logmsg("BOOL LTEQ\n");
        break;
      case 'v':
        logmsg("BOOL GTEQ\n");
        break;
      case 'x':
        logmsg("LOGICAL NOT\n");
        break;
      case 'B': {
        uint16_t len = *(uint16_t*)opcodeptr;
        opcodeptr += 2;
        logmsg("EMBEDDED CODE (%d bytes):\n", len);
        for (uint16_t s = 0; s < len; s++) {
          logmsg("%c", *opcodeptr);
          opcodeptr++;
        }
        logmsg("\n");
        break;
      }
      case 'I':
        logmsg("BEGIN ITEM ASSEMBLY\n");
        opcodeptr = process_item(opcodeptr);
        break;
      case 'C':
        logmsg("SAVE ITEM\n");
        break;
      case 'F':
        logmsg("FETCH ITEM\n");
        break;
      default:
        logerr("Undefined opcode: %c (%d)\n", *opcodeptr-1, *opcodeptr-1);
    }
  }
  logmsg("Byte %05u: ", opcodeptr - bytecode - 1); // -1 for the locals
  logmsg("HALT\n");

  // Clean up
  logmsg("Shutting down.\n");
  FREE_ARRAY(unsigned char, bytecode, filesize);
  exit(EXIT_SUCCESS);
}

uint8_t *process_item(uint8_t *opcodeptr) {
  // Recursive sub-processor to handle items.  Called whenever an I opcode
  // is encountered.  Returns when an E opcode is encountered.
  while (*opcodeptr != 'E') {
    switch (*opcodeptr++) {
      case 'L':
        // Standard layer
        logmsg("Byte %05u: ", opcodeptr - bytecode - 2);
        uint8_t len = *opcodeptr++;
        char layer[256];
        strncpy(layer, (const char*)opcodeptr, len);
        opcodeptr += len;
        layer[len] = '\0';
        logmsg("LAYER: %s\n", layer);
        break;
      case 'D':
        // Dereference!
        logmsg("Byte %05u: ", opcodeptr - bytecode - 2);
        logmsg("BEGIN DEREFERENCE LAYER\n");
        opcodeptr = process_dereference(opcodeptr);
        break;
      default:
        logmsg("Unknown opcode in item assembly %c (%d)\n",
                                                  opcodeptr-1, opcodeptr-1);
    }
  }
  // End of the item
  logmsg("Byte %05u: ", opcodeptr - bytecode - 1);
  logmsg("END ITEM LAYER ASSEMBLY\n");
  return ++opcodeptr;
}

uint8_t *process_dereference(uint8_t *opcodeptr) {
  // Process a dereference layer.  This can recurse through process_item().
  // This can either be a local variable or an item.
  uint8_t layertype = *opcodeptr++;
  if (layertype == 'V') {
    logmsg("Byte %05u: ", opcodeptr - bytecode - 2);
    uint8_t localvar = *opcodeptr++;
    logmsg("LOCALVAR %d\n", localvar);
  } else if (layertype == 'I') {
    logmsg("Byte %05u: ", opcodeptr - bytecode - 2);
    logmsg("BEGIN ITEM ASSEMBLY\n");
    opcodeptr = process_item(opcodeptr);
  } else {
    logmsg("Byte %05u: ", opcodeptr - bytecode - 2);
    logmsg ("Unknown dereference type: %c (%d)\n",  layertype, layertype);
  }
  return opcodeptr;
}

