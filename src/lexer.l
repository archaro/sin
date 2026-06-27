%{
  // The Sinistra lexer - tokenise the input.
  // Licensed under the MIT License - see LICENSE file for details.
  #include <limits.h>
  #include "log.h"
  #include "parser.h"

  void str_append_str(char **buf, char **bufptr,
                      int *len, int *max, char *append) {
    // A helper function for string handling.
    // Should only be called from <STRINGLIT> rules.
    int l = strlen(append);
    if (l + *len >= *max) {
      *max = (*max * 2) + l + 1;
      *buf = (char *)realloc(*buf, *max);
      *bufptr = *buf + *len;
    }
    memcpy(*bufptr, append, l);
    *bufptr += l;
    *len += l;
  }

  void str_append_char(char **buf, char **bufptr,
                      int *len, int *max, char append) {
    // A helper function for string handling.
    // Should only be called from <STRINGLIT> rules.
    // Same as str_append_str, but only appends a single character
    if (*len + 1 >= *max) {
      *max = (*max * 2) + 1;
      *buf = (char *)realloc(*buf, *max);
      *bufptr = *buf + *len;
    }
    **bufptr = append;
    *bufptr += 1;
    *len += 1;
  }

  char *strdup_lc(const char *s) {
    size_t n = strlen(s);
    char *d = malloc(n + 1);
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

%option noinput nounput noyywrap reentrant bison-bridge case-insensitive
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
                // Maximum length of string literal is uint16_t
                char *strbuf, *strbuf_ptr;
                int strbuf_size, strbuf_capacity;
                int code_paren;

<INITIAL>{
  "and"         { return TAND; }
  "code"        { BEGIN(CODE); return TCODE; }
  "delete"      { return TDELETE; }
  "do"          { return TDO; }
  "else"        { return TELSE; }
  "elsif"       { return TELSIF; }
  "endif"       { return TENDIF; }
  "endwhile"    { return TENDWHILE; }
  "exists"      { return TEXISTS; }
  "if"          { return TIF; }
  "net"         { yylval->TSTRINGLIT = strdup_lc(yytext); return TLIBNAME; }
  "nthname"     { return TNTHNAME; }
  "or"          { return TOR; }
  "return"      { return TRETURN; }
  "rootname"    { return TROOTNAME; }
  "str"         { yylval->TSTRINGLIT = strdup_lc(yytext); return TLIBNAME; }
  "sys"         { yylval->TSTRINGLIT = strdup_lc(yytext); return TLIBNAME; }
  "task"        { yylval->TSTRINGLIT = strdup_lc(yytext); return TLIBNAME; }
  "then"        { return TTHEN; }
  "true"        { return TTRUE; }
  "false"       { return TFALSE; }
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
  "/*"          { BEGIN(COMMENT); }
  "/"           { return TDIV; }
  "("           { return TLPAREN; }
  ")"           { return TRPAREN; }
  ","           { return TCOMMA; }
  "{"           { return TLBRACE; }
  "}"           { return TRBRACE; }
  ";"           { return TSEMI; }
  "\""          { 
                  strbuf_capacity = 4;
                  strbuf = (char *)malloc(strbuf_capacity + 1);
                  strbuf_ptr = strbuf;
                  strbuf_size = 0;
                  BEGIN(STRINGLIT);
                }
  [ \t\n]+      ; // Inline whitespace handling
  {float}       { yylval->TSTRINGLIT = strdup(yytext); return TFLOAT; }
  {integer}     { yylval->TSTRINGLIT = strdup(yytext); return TINTEGER; }
  {local}       { yylval->TSTRINGLIT = strdup_lc(yytext); return TLOCAL; }
  {layer}       { yylval->TSTRINGLIT = strdup_lc(yytext); return TLAYER; }
  "\."          { return TLAYERSEP; }
  "\["          { return TDEREFSTART; }
  "\]"          { return TDEREFEND; }
}
<STRINGLIT>{
  \"            { /* saw closing quote - all done */
                  BEGIN(INITIAL);
                  *strbuf_ptr = '\0';
                  yylval->TSTRINGLIT = strdup(strbuf);
                  free(strbuf);
                  return TSTRINGLIT;
                }
  \n            {
                  BEGIN(INITIAL);
                  yylval->TSTRINGLIT = strdup("Newline in string.");
                  free(strbuf);
                  return TUNKNOWNCHAR;
                }
  \\0[0-7]{2}   {
                  /* octal escape sequence */
                  int result;
                  (void) sscanf( yytext + 1, "%o", &result );
                  if ( result > 0xff ) {
                    BEGIN(INITIAL);
                    yylval->TSTRINGLIT = strdup("Escape sequence too big.");
                    free(strbuf);
                    return TUNKNOWNCHAR;
                  }
                  str_append_char(&strbuf, &strbuf_ptr, &strbuf_size,
                                                 &strbuf_capacity, result);
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
                    *strbuf_ptr = '\0';
                    yylval->TSTRINGLIT = strdup(strbuf);
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
  .             { yylval->TSTRINGLIT = strdup(yytext); return TUNKNOWNCHAR; }
}
<STRINGLIT><<EOF>> {
  BEGIN(INITIAL);
  yylval->TSTRINGLIT = strdup("Unterminated string literal.");
  free(strbuf);
  return TUNKNOWNCHAR;
}
<CODEBODY><<EOF>> {
  BEGIN(INITIAL);
  yylval->TSTRINGLIT = strdup("Unterminated code(...) body.");
  free(strbuf);
  return TUNKNOWNCHAR;
}
<CODESTREMBED><<EOF>> {
  BEGIN(INITIAL);
  yylval->TSTRINGLIT = strdup("Unterminated quoted string inside code(...) body.");
  free(strbuf);
  return TUNKNOWNCHAR;
}
<COMMENT><<EOF>> {
  BEGIN(INITIAL);
  yylval->TSTRINGLIT = strdup("Unterminated comment.");
  return TUNKNOWNCHAR;
}
%%


