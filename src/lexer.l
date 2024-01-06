%{
  #include "log.h"
  #include "parser.h"
%}

%option noinput nounput noyywrap reentrant bison-bridge case-insensitive
%x DEREF
%x STRINGLIT

local       @[a-z_][a-z0-9_]*
layer       [a-z0-9_]+
integer     [0-9]+
%%
                char string_buf[1025];
                char *string_buf_ptr;

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
  "\""          { string_buf_ptr = string_buf; BEGIN(STRINGLIT); }
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
                  *string_buf_ptr = '\0';
                  yylval->string = strdup(string_buf);
                  return TSTRINGLIT;
                }
  \n            {
                  BEGIN(INITIAL);
                  yylval->string = strdup("Newline in string.\n");
                  return TUNKNOWNCHAR;
                }
  \\[0-7]{1,3}  {
                  /* octal escape sequence */
                  int result;
                  (void) sscanf( yytext + 1, "%o", &result );
                  if ( result > 0xff ) {
                    BEGIN(INITIAL);
                    yylval->string = strdup("Escape sequence too big.\n");
                    return TUNKNOWNCHAR;
                  }
                  *string_buf_ptr++ = result;
                }
  \\[0-9]+      {
                  BEGIN(INITIAL);
                  yylval->string = strdup("Invalid escape sequence.\n");
                  return TUNKNOWNCHAR;
                }
  \\n           { *string_buf_ptr++ = '\n'; }
  \\t           { *string_buf_ptr++ = '\t'; }
  \\r           { *string_buf_ptr++ = '\r'; }
  \\b           { *string_buf_ptr++ = '\b'; }
  \\f           { *string_buf_ptr++ = '\f'; }
  \\(.|\n)      { *string_buf_ptr++ = yytext[1]; }
  [^\\\n\"]+    {
                  char *yptr = yytext;
                  while (*yptr) { *string_buf_ptr++ = *yptr++; }
                }
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


