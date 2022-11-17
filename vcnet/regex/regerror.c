#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "regex.h"

static struct rerr {
	int code;
	char *name;
	char *explain;
} rerrs[] = {
	{REG_OKAY,	"REG_OKAY",	"no errors detected"},
	{REG_NOMATCH,	"REG_NOMATCH",	"regexec() failed to match"},
	{REG_BADPAT,	"REG_BADPAT",	"invalid regular expression"},
	{REG_ECOLLATE,	"REG_ECOLLATE",	"invalid collating element"},
	{REG_ECTYPE,	"REG_ECTYPE",	"invalid character class"},
	{REG_EESCAPE,	"REG_EESCAPE",	"trailing backslash (\\)"},
	{REG_ESUBREG,	"REG_ESUBREG",	"invalid backreference number"},
	{REG_EBRACK,	"REG_EBRACK",	"brackets ([ ]) not balanced"},
	{REG_EPAREN,	"REG_EPAREN",	"parentheses not balanced"},
	{REG_EBRACE,	"REG_EBRACE",	"braces not balanced"},
	{REG_BADBR,	"REG_BADBR",	"invalid repetition count(s)"},
	{REG_ERANGE,	"REG_ERANGE",	"invalid character range"},
	{REG_ESPACE,	"REG_ESPACE",	"out of memory"},
	{REG_BADRPT,	"REG_BADRPT",	"repetition-operator operand invalid"},
	{REG_EMPTY,	"REG_EMPTY",	"empty (sub)expression"},
	{REG_ASSERT,	"REG_ASSERT",	"\"can't happen\" -- you found a bug"},
	{REG_INVARG,	"REG_INVARG",	"invalid argument to regex routine"},
	{-1,		"",		"*** unknown regexp error code ***"},
};

static size_t
set_result(char *errbuf, size_t errbuf_size, char *result) {
	size_t result_len = strlen(result);
	if (errbuf_size > 0) {
		size_t copy_len = result_len < errbuf_size ? result_len : errbuf_size - 1;
		memcpy(errbuf, result, copy_len);
		errbuf[copy_len] = '\0';
	}
	return result_len + 1;
}

/*
 - regerror - the interface to error numbers
 */
size_t
regerror(int errorcode, const regex_t *preg, char *errbuf, size_t errbuf_size)
{
	struct rerr *r;
	char convbuf[50];

	if (errorcode == REG_ATOI) {
		if (preg == NULL || preg->re_endp == NULL)
			return set_result(errbuf, errbuf_size, "0");
		for (r = rerrs; r->code >= 0; r++)
			if (strcmp(r->name, preg->re_endp) == 0)
				break;
		if (r->code < 0)
			return set_result(errbuf, errbuf_size, "0");
		snprintf(convbuf, sizeof convbuf, "%d", r->code);
		return set_result(errbuf, errbuf_size, convbuf);
	}
	else {
		int target = errorcode &~ REG_ITOA;
		
		for (r = rerrs; r->code >= 0; r++)
			if (r->code == target)
				break;
		if (errorcode & REG_ITOA) {
			if (r->code >= 0)
				return set_result(errbuf, errbuf_size, r->name);
			snprintf(convbuf, sizeof convbuf, "REG_0x%x", target);
			return set_result(errbuf, errbuf_size, convbuf);
		}
		return set_result(errbuf, errbuf_size, r->explain);
	}
}
