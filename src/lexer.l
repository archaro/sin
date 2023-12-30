%{
  #include "parser.h"
%}

%option noinput nounput noyywrap reentrant bison-bridge case-insensitive
%x DEREF

stringlit   \"([^\\\"]|\\.)*\"
local       @[a-z_][a-z0-9_]*
layer       [a-z0-9_]+
integer     [0-9]+
%%

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
  [ \t\n]+      ; // Inline whitespace handling
  {integer}     { yylval->string = strdup(yytext); return TINTEGER; }
  {stringlit}   { yylval->string = strdup(yytext); return TSTRINGLIT; }
  {local}       { yylval->string = strdup(yytext); return TLOCAL; }
  {layer}       { yylval->string = strdup(yytext); return TLAYER; }
  "\."          { return TLAYERSEP; }
  "\["          { BEGIN(DEREF);
                  yyextra.deref_depth++;
                  return TDEREFSTART;
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


