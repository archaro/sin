%{
#include "parser.h"
%}

%option noinput nounput noyywrap reentrant bison-bridge case-insensitive

integer     [0-9]+
stringlit   \"([^\\\"]|\\.)*\"
local       @[a-z_][a-z0-9_]*
%%

"code"        { return TCODE; }
"do"          { return TDO; }
"else"        { return TELSE; }
"elsif"       { return TELSIF; }
"endif"       { return TENDIF; }
"endwhile"    { return TENDWHILE; }
"if"          { return TIF; }
"then"        { return TTHEN; }
"while"       { return TWHILE; }
"="           { return TASSIGN; }
"=="          { return TEQUAL; }
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
[ \t\n]+      ; /* inline whitespace handling */
{integer}     { yylval->string = strdup(yytext); return TINTEGER; }
{stringlit}   { yylval->string = strdup(yytext); return TSTRINGLIT; }
{local}       { yylval->string = strdup(yytext); return TLOCAL; }
.             { yylval->string = strdup(yytext); return TUNKNOWNCHAR; }

%%


