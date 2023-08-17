%{
#include "parser.h"
%}

%option noinput nounput noyywrap reentrant bison-bridge case-insensitive

ws          [ \t\n]+
digit       [0-9]
alpha 		  [a-z]
alnum 		  {alpha}|{digit}
integer     {digit}+
stringlit   \"([^\\\"]|\\.)*\"
identifier  {alpha}+{alnum}*
local       @{identifier}
%%

{integer}     { yylval->string = strdup(yytext); return TINTEGER; }
{stringlit}   { yylval->string = strdup(yytext); return TSTRINGLIT; }
{local}       { yylval->string = strdup(yytext); return TLOCAL; }
"code"        { return TCODE; }
"do"          { return TDO; }
"endwhile"    { return TENDWHILE; }
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
{ws}          ;

%%


