%{
#include "parser.h"
%}

%option noyywrap

ws      [ \t\n]+
digit   [0-9]
integer {digit}+

%%

{integer}   { yylval.string = yytext; return TINTEGER; }
"+"         { return TPLUS; }
"-"         { return TMINUS; }
";"         { return TSEMI; }
{ws}        ;

%%


