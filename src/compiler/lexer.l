%{
  // The Sinistra lexer - tokenise the input.
  // Licensed under the MIT License - see LICENSE file for details.
  #include <limits.h>
  #include <stdbool.h>
  #include "log.h"
  #include "memory.h"
  #include "parser.h"
  #include "string_limits.h"

  SCANNER_STATE_t *yyget_extra(yyscan_t yyscanner);
  static void yy_fatal_error(const char *message, yyscan_t yyscanner);

  struct scanner_alloc_s {
    struct scanner_alloc_s *next;
    SCANNER_STATE_t *owner;
    size_t size;
  };

  void *yyalloc(yy_size_t size, yyscan_t yyscanner);
  void yyfree(void *ptr, yyscan_t yyscanner);

  static void sin_lexer_fatal(const char *message, yyscan_t yyscanner) {
    (void)message;
    SCANNER_STATE_t *state = yyget_extra(yyscanner);
    if (state) {
      state->scanner_failed = true;
      if (state->scanner_fatal_jmp_active) {
        longjmp(state->scanner_fatal_jmp, 1);
      }
    }
  }

  #define YY_FATAL_ERROR(message) \
    do { (void)yy_fatal_error; sin_lexer_fatal(message, yyscanner); } while (0)

  void *yyalloc(yy_size_t size, yyscan_t yyscanner) {
    SCANNER_STATE_t *state = yyget_extra(yyscanner);
    size_t total = 0;
    if (alloc_add_overflow(sizeof(struct scanner_alloc_s), (size_t)size, &total)) {
      return NULL;
    }
    struct scanner_alloc_s *block = alloc_malloc(total);
    if (!block) return NULL;
    block->owner = state;
    block->size = (size_t)size;
    if (state) {
      block->next = state->scanner_allocs;
      state->scanner_allocs = block;
    } else {
      block->next = NULL;
    }
    return block + 1;
  }

  void *yyrealloc(void *ptr, yy_size_t size, yyscan_t yyscanner) {
    if (!ptr) return yyalloc(size, yyscanner);
    if (size == 0) {
      yyfree(ptr, yyscanner);
      return NULL;
    }
    struct scanner_alloc_s *old = ((struct scanner_alloc_s *)ptr) - 1;
    void *replacement = yyalloc(size, yyscanner);
    if (!replacement) return NULL;
    size_t copy_size = old->size < (size_t)size ? old->size : (size_t)size;
    memcpy(replacement, ptr, copy_size);
    yyfree(ptr, yyscanner);
    return replacement;
  }

  void yyfree(void *ptr, yyscan_t yyscanner) {
    (void)yyscanner;
    if (!ptr) return;
    struct scanner_alloc_s *block = ((struct scanner_alloc_s *)ptr) - 1;
    SCANNER_STATE_t *state = block->owner;
    bool destroying_scanner = state && state->scanner_handle == ptr;
    if (state) {
      struct scanner_alloc_s **link = &state->scanner_allocs;
      while (*link && *link != block) link = &(*link)->next;
      if (*link == block) *link = block->next;
    }
    free(block);
    if (destroying_scanner) {
      while (state->scanner_allocs) {
        struct scanner_alloc_s *leftover = state->scanner_allocs;
        state->scanner_allocs = leftover->next;
        free(leftover);
      }
    }
  }

  static int sin_utf8_columns(const char *text, int len) {
    int cols = 0;
    for (int i = 0; i < len; ++i) {
      unsigned char c = (unsigned char)text[i];
      if (c == '\n') continue;
      if ((c & 0xC0) != 0x80) cols++;
    }
    return cols > 0 ? cols : 1;
  }

  static void sin_update_location(YYLTYPE *loc, SCANNER_STATE_t *state, const char *text, int len) {
    if (!state || !loc) return;
    loc->first_line = state->line;
    loc->first_column = state->column;
    int line = state->line;
    int column = state->column;
    int token_first_column = column;
    int token_last_line = line;
    int token_last_column = column;
    for (int i = 0; i < len; ++i) {
      unsigned char c = (unsigned char)text[i];
      token_last_line = line;
      token_last_column = column;
      if (c == '\n') {
        line++;
        column = 1;
      } else if ((c & 0xC0) != 0x80) {
        column++;
      }
    }
    loc->last_line = token_last_line;
    loc->last_column = token_last_column;
    state->line = line;
    state->column = column;
    state->span = token_last_line == loc->first_line ? token_last_column - token_first_column + 1 : sin_utf8_columns(text, len);
  }

  #define YY_USER_ACTION sin_update_location(yylloc, yyextra, yytext, yyleng);

  static bool str_append_bytes(char **buf, char **bufptr, int *len, int *max,
                               const char *append, size_t append_len) {
    // A helper function for string handling.
    // Should only be called from string-building lexer rules.
    if (*len < 0) return false;
    size_t current_len = (size_t)*len;
    if (append_len > SIN_MAX_STRING_BYTES - current_len) {
      *len = -1;
      return false;
    }
    size_t needed = current_len + append_len + 1;
    if (needed > (size_t)*max) {
      size_t newmax = (size_t)*max;
      while (newmax < needed) {
        size_t grown = (newmax * 2) + append_len + 1;
        if (grown <= newmax || grown > SIN_MAX_STRING_BYTES + 1) {
          grown = SIN_MAX_STRING_BYTES + 1;
        }
        newmax = grown;
      }
      char *newbuf = (char *)realloc(*buf, newmax);
      if (!newbuf) {
        *len = -1;
        return false;
      }
      *max = (int)newmax;
      *buf = newbuf;
      *bufptr = *buf + *len;
    }
    memcpy(*bufptr, append, append_len);
    *bufptr += append_len;
    *len += (int)append_len;
    return true;
  }

  void str_append_str(char **buf, char **bufptr,
                      int *len, int *max, char *append) {
    (void)str_append_bytes(buf, bufptr, len, max, append, strlen(append));
  }

  void str_append_char(char **buf, char **bufptr,
                      int *len, int *max, char append) {
    (void)str_append_bytes(buf, bufptr, len, max, &append, 1);
  }

  static char *sin_strdup(const char *s) {
    size_t len = strlen(s);
    size_t size = 0;
    if (alloc_add_overflow(len, 1, &size)) return NULL;
    char *copy = alloc_malloc(size);
    if (!copy) return NULL;
    memcpy(copy, s, size);
    return copy;
  }

  static char *strbuf_too_long_msg(void) {
    return sin_strdup("String literal too long.");
  }

  char *strdup_lc(const char *s) {
    size_t n = strlen(s);
    char *d = alloc_malloc(n + 1);
    if (!d) return NULL;
    const unsigned char *src = (const unsigned char *)s;
    unsigned char *dst = (unsigned char *)d;
    for (size_t i = 0; i < n; ++i) {
      unsigned char c = src[i];
      // Branchless ASCII lowercase:
      // if ('A' <= c <= 'Z') -> c + 32
      dst[i] = c + ((c >= 'A' && c <= 'Z') << 5);
    }
    dst[n] = '\0';
    return d;
  }
%}

%option noinput nounput noyywrap noyyalloc noyyrealloc noyyfree reentrant bison-bridge bison-locations case-insensitive extra-type="SCANNER_STATE_t *"
%x STRINGLIT
%x CODE
%x CODEBODY
%x CODEPARAMS
%x CODESTREMBED
%x COMMENT

local       @[a-z_][a-z0-9_]*
layer       [a-z0-9_]+
integer     [0-9]+
float       [0-9]+\.[0-9]+([eE][+-]?[0-9]+)?
%%
                // String literal payloads are capped by SIN_MAX_STRING_BYTES.
                char *strbuf, *strbuf_ptr;
                int strbuf_size, strbuf_capacity;
                int strbuf_start_line = 1;
                int strbuf_start_column = 1;
                int code_paren = 0;

<INITIAL>{
  "and"         { return TAND; }
  "break"       { return TBREAK; }
  "code"        { BEGIN(CODE); return TCODE; }
  "continue"    { return TCONTINUE; }
  "do"          { return TDO; }
  "else"        { return TELSE; }
  "elsif"       { return TELSIF; }
  "endfor"      { return TENDFOR; }
  "endif"       { return TENDIF; }
  "endwhile"    { return TENDWHILE; }
  "foreach"     { return TFOREACH; }
  "if"          { return TIF; }
  "in"          { return TIN; }
  "list"        { yylval->TSTRINGLIT = strdup_lc(yytext); return TLIBNAME; }
  "net"         { yylval->TSTRINGLIT = strdup_lc(yytext); return TLIBNAME; }
  "or"          { return TOR; }
  "return"      { return TRETURN; }
  "str"         { yylval->TSTRINGLIT = strdup_lc(yytext); return TLIBNAME; }
  "sys"         { yylval->TSTRINGLIT = strdup_lc(yytext); return TLIBNAME; }
  "task"        { yylval->TSTRINGLIT = strdup_lc(yytext); return TLIBNAME; }
  "then"        { return TTHEN; }
  "true"        { return TTRUE; }
  "false"       { return TFALSE; }
  "nil"         { return TNIL; }
  "while"       { return TWHILE; }
  "="           { return TASSIGN; }
  "=="          { return TEQUAL; }
  "!"           { return TNOT; }
  "!="          { return TNOTEQUAL; }
  "<"           { return TLT; }
  "<="          { return TLTEQ; }
  ">"           { return TGT; }
  ">="          { return TGTEQ; }
  "++"          { return TINC; }
  "+"           { return TPLUS; }
  "--"          { return TDEC; }
  "-"           { return TMINUS; }
  "*"           { return TMULT; }
  "%"           { return TMOD; }
  "/*"          { BEGIN(COMMENT); }
  "/"           { return TDIV; }
  "("           { return TLPAREN; }
  ")"           { return TRPAREN; }
  ","           { return TCOMMA; }
  "{"           { return TLBRACE; }
  "}"           { return TRBRACE; }
  ";"           { return TSEMI; }
  "\""          {
                  strbuf_start_line = yylloc->first_line;
                  strbuf_start_column = yylloc->first_column;
                  strbuf_capacity = 4;
                  strbuf = (char *)malloc(strbuf_capacity + 1);
                  if (!strbuf) {
                    yylval->TSTRINGLIT = sin_strdup("Out of memory.");
                    return TUNKNOWNCHAR;
                  }
                  strbuf_ptr = strbuf;
                  strbuf_size = 0;
                  BEGIN(STRINGLIT);
                }
  [ \t\n]+      ; // Inline whitespace handling
  {float}       { yylval->TSTRINGLIT = sin_strdup(yytext); return TFLOAT; }
  {integer}     { yylval->TSTRINGLIT = sin_strdup(yytext); return TINTEGER; }
  {local}       { yylval->TSTRINGLIT = strdup_lc(yytext); return TLOCAL; }
  {layer}       { yylval->TSTRINGLIT = strdup_lc(yytext); return TLAYER; }
  "\."          { return TLAYERSEP; }
  "#["           { return TLISTSTART; }
  "&"            { return TITEMREF; }
  "\["          { return TDEREFSTART; }
  "\]"          { return TDEREFEND; }
}
<STRINGLIT>{
  \"            { /* saw closing quote - all done */
                  BEGIN(INITIAL);
                  if (strbuf_size < 0) {
                    yylval->TSTRINGLIT = strbuf_too_long_msg();
                    free(strbuf);
                    return TUNKNOWNCHAR;
                  }
                  *strbuf_ptr = '\0';
                  yylval->TSTRINGLIT = sin_strdup(strbuf);
                  free(strbuf);
                  return TSTRINGLIT;
                }
  \n            {
                  BEGIN(INITIAL);
                  yylloc->first_line = strbuf_start_line;
                  yylloc->first_column = strbuf_start_column;
                  yylloc->last_line = strbuf_start_line;
                  yylloc->last_column = strbuf_start_column;
                  yylval->TSTRINGLIT = sin_strdup("Newline in string.");
                  free(strbuf);
                  return TUNKNOWNCHAR;
                }
  \\0[0-7]{2}   {
                  /* octal escape sequence */
                  unsigned int result;
                  (void) sscanf( yytext + 1, "%o", &result );
                  if ( result == 0 ) {
                    BEGIN(INITIAL);
                    yylval->TSTRINGLIT = sin_strdup("NUL byte escape \\000 is not allowed.");
                    free(strbuf);
                    return TUNKNOWNCHAR;
                  }
                  if ( result > 0xff ) {
                    BEGIN(INITIAL);
                    yylval->TSTRINGLIT = sin_strdup("Escape sequence too big.");
                    free(strbuf);
                    return TUNKNOWNCHAR;
                  }
                  str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                                 &strbuf_capacity, (int)result);
                }
  \\n           { str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                                &strbuf_capacity, '\n'); }
  \\t           { str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                                &strbuf_capacity, '\t'); }
  \\r           { str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                                &strbuf_capacity, '\r'); }
  \\b           { str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                                &strbuf_capacity, '\b'); }
  \\f           { str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                                &strbuf_capacity, '\f'); }
  \\(.|\n)      { str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                           &strbuf_capacity, yytext[1]); }
  [^\\\n\"]+    { str_append_str(&strbuf, &strbuf_ptr, &strbuf_size,
                                              &strbuf_capacity, yytext); }
}
<CODE>{
  "("           { code_paren = 1;
                  strbuf_capacity = 512;
                  strbuf = (char *)malloc(strbuf_capacity + 1);
                  if (!strbuf) {
                    yylval->TSTRINGLIT = sin_strdup("Out of memory.");
                    return TUNKNOWNCHAR;
                  }
                  strbuf_ptr = strbuf;
                  strbuf_size = 0;
                  BEGIN(CODEBODY);
                }
  "{"           { BEGIN(CODEPARAMS); return TLBRACE; }
  [ \t\n]+      ; // Inline whitespace handling
}
<CODEPARAMS>{
  {local}       { yylval->TSTRINGLIT = strdup_lc(yytext); return TLOCAL; }
  ","           { return TCOMMA; }
  "}"           { BEGIN(CODE); return TRBRACE; }
  [ \t\n]+      ; // Inline whitespace handling
}
<CODEBODY>{
  "("           { // Keep track of parentheses so that we know when
                  // the code segment actually ends.
                  code_paren++;
                  str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                            &strbuf_capacity, '(');
                }
  ")"           { code_paren--;
                  if (code_paren > 0) {
                    // This is just an ordinary close parenthesis
                    str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                            &strbuf_capacity, ')');
                  } else {
                    // This ends the code segment.  Return the buffer.
                    BEGIN(INITIAL);
                    if (strbuf_size < 0) {
                      yylval->TSTRINGLIT = strbuf_too_long_msg();
                      free(strbuf);
                      return TUNKNOWNCHAR;
                    }
                    *strbuf_ptr = '\0';
                    yylval->TSTRINGLIT = sin_strdup(strbuf);
                    free(strbuf);
                    return TCODEBODY;
                  }
                }
  "\""          {
                  // Begin an embedded string.  Copy the " then switch
                  // to another state to eat the rest of the string.
                  str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                            &strbuf_capacity, '"');
                  BEGIN(CODESTREMBED);
                }
  [\t\n]        { str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                                &strbuf_capacity, ' '); }
  .             { str_append_str(&strbuf, &strbuf_ptr, &strbuf_size,
                                              &strbuf_capacity, yytext); }
}
<CODESTREMBED>{
  "\\\""        {
                  // Embedded quote.  Copy it!
                  str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                            &strbuf_capacity, '\\');
                  str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                            &strbuf_capacity, '"');
                }
  "\""          {
                  // End of embedded quote.  Copy it and change state.
                  str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                            &strbuf_capacity, '"');
                  BEGIN(CODEBODY);
                }
  [^\"]+        { str_append_str(&strbuf, &strbuf_ptr, &strbuf_size,
                                              &strbuf_capacity, yytext); }
}
<COMMENT>{
  "*/"        { BEGIN(INITIAL); }
  .|\n        { /* Eat everything else */ }
}
<INITIAL,CODE>{
  .             { yylval->TSTRINGLIT = sin_strdup(yytext); return TUNKNOWNCHAR; }
}
<STRINGLIT><<EOF>> {
  BEGIN(INITIAL);
  yylloc->first_line = strbuf_start_line;
  yylloc->first_column = strbuf_start_column;
  yylloc->last_line = strbuf_start_line;
  yylloc->last_column = strbuf_start_column;
  yylval->TSTRINGLIT = strbuf_size < 0 ? strbuf_too_long_msg() :
      sin_strdup("EOF in string.");
  free(strbuf);
  return TUNKNOWNCHAR;
}
<CODEBODY><<EOF>> {
  BEGIN(INITIAL);
  yylval->TSTRINGLIT = strbuf_size < 0 ? strbuf_too_long_msg() :
      sin_strdup("Unterminated code(...) body.");
  free(strbuf);
  return TUNKNOWNCHAR;
}
<CODESTREMBED><<EOF>> {
  BEGIN(INITIAL);
  yylval->TSTRINGLIT = strbuf_size < 0 ? strbuf_too_long_msg() :
      sin_strdup("Unterminated quoted string inside code(...) body.");
  free(strbuf);
  return TUNKNOWNCHAR;
}
<COMMENT><<EOF>> {
  BEGIN(INITIAL);
  yylval->TSTRINGLIT = sin_strdup("Unterminated comment.");
  return TUNKNOWNCHAR;
}
%%
