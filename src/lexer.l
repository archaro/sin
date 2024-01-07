%{
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
%}

%option noinput nounput noyywrap reentrant bison-bridge case-insensitive
%x DEREF
%x STRINGLIT

local       @[a-z_][a-z0-9_]*
layer       [a-z0-9_]+
integer     [0-9]+
%%
                // Maximum length of string literal is uint16_t
                char *strbuf, *strbuf_ptr;
                int strbuf_size, strbuf_capacity;

<INITIAL>{
  "and"         { return TAND; }
  "code"        { return TCODE; }
  "do"          { return TDO; }
  "else"        { return TELSE; }
  "elsif"       { return TELSIF; }
  "endif"       { return TENDIF; }
  "endwhile"    { return TENDWHILE; }
  "if"          { return TIF; }
  "or"          { return TOR; }
  "sys.compile" { return TLIBSYSCOMPILE; }
  "then"        { return TTHEN; }
  "while"       { return TWHILE; }
  "="           { return TASSIGN; }
  "=="          { return TEQUAL; }
  "!"           { return TNOT; }
  "!="          { return TNOTEQUAL; }
  "<"           { return TLESSTHAN; }
  "<="          { return TLTEQ; }
  ">"           { return TGREATERTHAN; }
  ">="          { return TGTEQ; }
  "++"          { return TINC; }
  "+"           { return TPLUS; }
  "--"          { return TDEC; }
  "-"           { return TMINUS; }
  "*"           { return TMULT; }
  "/"           { return TDIV; }
  "("           { return TLPAREN; }
  ")"           { return TRPAREN; }
  ";"           { return TSEMI; }
  "\""          { 
                  strbuf_capacity = 4;
                  strbuf = (char *)malloc(strbuf_capacity + 1);
                  strbuf_ptr = strbuf;
                  strbuf_size = 0;
                  BEGIN(STRINGLIT);
                }
  [ \t\n]+      ; // Inline whitespace handling
  {integer}     { yylval->string = strdup(yytext); return TINTEGER; }
  {local}       { yylval->string = strdup(yytext); return TLOCAL; }
  {layer}       { yylval->string = strdup(yytext); return TLAYER; }
  "\."          { return TLAYERSEP; }
  "\["          { BEGIN(DEREF);
                  yyextra.deref_depth++;
                  return TDEREFSTART;
                }
}
<STRINGLIT>{
  \"            { /* saw closing quote - all done */
                  BEGIN(INITIAL);
                  *strbuf_ptr = '\0';
                  yylval->string = strdup(strbuf);
                  free(strbuf);
                  return TSTRINGLIT;
                }
  \n            {
                  BEGIN(INITIAL);
                  yylval->string = strdup("Newline in string.\n");
                  free(strbuf);
                  return TUNKNOWNCHAR;
                }
  \\0[0-7]{2}   {
                  /* octal escape sequence */
                  int result;
                  (void) sscanf( yytext + 1, "%o", &result );
                  if ( result > 0xff ) {
                    BEGIN(INITIAL);
                    yylval->string = strdup("Escape sequence too big.\n");
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
<DEREF>{
  {local}       { yylval->string = strdup(yytext); return TLOCAL; }
  {layer}       { yylval->string = strdup(yytext); return TLAYER; }
  "\."          { return TLAYERSEP; }
  "\["          { // Handle nested dereferences
                  yyextra.deref_depth++;
                  return TDEREFSTART;
                }
  "\]"          { if (--yyextra.deref_depth == 0) {
                    BEGIN(INITIAL);
                  }
                  return TDEREFEND;
                }

}
.             { yylval->string = strdup(yytext); return TUNKNOWNCHAR; }

%%


