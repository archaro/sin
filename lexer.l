%{
#include "parser.h"
%}

%option noyywrap reentrant bison-bridge

ws        [ \t\n]+
digit     [0-9]
integer   {digit}+
stringlit \"([^\\\"]|\\.)*\"

%%

{integer}   { yylval->string = yytext; return TINTEGER; }
{stringlit} { yylval->string = yytext; return TSTRINGLIT; }
"+"         { return TPLUS; }
"-"         { return TMINUS; }
"*"         { return TMULT; }
"/"         { return TDIV; }
";"         { return TSEMI; }
{ws}        ;

%%


