%{
#include "bison.tab.h"
%}

%%
">" {return '>';}
"<" {return '<';}
"|" {return '|';}
"&" {return '&';}
[^><&\t |]+ {return STRING;}
%%
