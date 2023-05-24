%{
#include "parser.h"
%}

%option noyywrap reentrant bison-bridge

ws          [ \t\n]+
digit       [0-9]
alpha 		  [a-zA-Z]
alnum 		  {alpha}|{digit}
integer     {digit}+
stringlit   \"([^\\\"]|\\.)*\"
identifier  {alpha}+{alnum}*
local       @{identifier}
%%

{integer}     { yylval->string = strdup(yytext); return TINTEGER; }
{stringlit}   { yylval->string = strdup(yytext); return TSTRINGLIT; }
{local}       { yylval->string = strdup(yytext); return TLOCAL; }
"="           { return TASSIGN; }
"++"          { return TINC; }
"+"           { return TPLUS; }
"--"          { return TDEC; }
"-"           { return TMINUS; }
"*"           { return TMULT; }
"/"           { return TDIV; }
"("           { return TLPAREN; }
")"           { return TRPAREN; }
";"           { return TSEMI; }
{ws}          ;

%%


