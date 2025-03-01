/*
 * lib/parser.c - simple parser for mount, etc. options.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include "parser.h"

/**
 * match_one: - Determines if a string matches a simple pattern
 * @s:		the string to examine for presence of the pattern
 * @p:		the string containing the pattern
 * @args:	array of %MAX_OPT_ARGS &substring_t elements. Used to return match
 * locations.
 *
 * Description: Determines if the pattern @p is present in string @s. Can only
 * match extremely simple token=arg style patterns. If the pattern is found,
 * the location(s) of the arguments will be returned in the @args array.
 */
static int match_one(char *s, const char *p, substring_t args[])
{
	char *meta;
	int argc = 0;

	if (!p)
		return 1;

	while(1) {
		int len = -1;
		meta = strchr(p, '%');
		if (!meta)
			return strcmp(p, s) == 0;

		if (strncmp(p, s, meta-p))
			return 0;

		s += meta - p;
		p = meta + 1;

		if (isdigit(*p))
			len = strtoul(p, (char **) &p, 10);
		else if (*p == '%') {
			if (*s++ != '%')
				return 0;
			p++;
			continue;
		}

		if (argc >= MAX_OPT_ARGS)
			return 0;

		args[argc].from = s;
		switch (*p++) {
		case 's': {
			size_t str_len = strlen(s);

			if (str_len == 0)
				return 0;
			if (len == -1 || len > str_len)
				len = str_len;
			args[argc].to = s + len;
			break;
		}
		case 'd':
			strtol(s, &args[argc].to, 0);
			goto num;
		case 'u':
			strtoul(s, &args[argc].to, 0);
			goto num;
		case 'o':
			strtoul(s, &args[argc].to, 8);
			goto num;
		case 'x':
			strtoul(s, &args[argc].to, 16);
		num:
			if (args[argc].to == args[argc].from)
				return 0;
			break;
		default:
			return 0;
		}
		s = args[argc].to;
		argc++;
	}
}

/**
 * match_token: - Find a token (and optional args) in a string
 * @s:		the string to examine for token/argument pairs
 * @table:	match_table_t describing the set of allowed option tokens and the
 * 		arguments that may be associated with them. Must be terminated with a
 * 		&struct match_token whose pattern is set to the NULL pointer.
 * @args:	array of %MAX_OPT_ARGS &substring_t elements. Used to return match
 * 		locations.
 *
 * Description: Detects which if any of a set of token strings has been passed
 * to it. Tokens can include up to MAX_OPT_ARGS instances of basic c-style
 * format identifiers which will be taken into account when matching the
 * tokens, and whose locations will be returned in the @args array.
 */
int match_token(char *s, const match_table_t table, substring_t args[])
{
	const struct match_token *p;

	for (p = table; !match_one(s, p->pattern, args) ; p++)
		;

	return p->token;
}

/**
 * match_number: scan a number in the given base from a substring_t
 * @s:		substring to be scanned
 * @result:	resulting integer on success
 * @base:	base to use when converting string
 *
 * Description: Given a &substring_t and a base, attempts to parse the substring
 * as a number in that base. On success, sets @result to the integer represented
 * by the string and returns 0. Returns -ENOMEM, -EINVAL, or -ERANGE on failure.
 */
static int match_number(substring_t *s, int *result, int base)
{
	char *endp;
	char *buf;
	int ret;
	long val;
	size_t len = s->to - s->from;

	buf = malloc(len + 1);
	if (!buf)
		return -ENOMEM;
	memcpy(buf, s->from, len);
	buf[len] = '\0';

	ret = 0;
	val = strtol(buf, &endp, base);
	if (endp == buf)
		ret = -EINVAL;
	else if (val < (long)INT_MIN || val > (long)INT_MAX)
		ret = -ERANGE;
	else
		*result = (int) val;
	free(buf);
	return ret;
}

/**
 * match_int: - scan a decimal representation of an integer from a substring_t
 * @s:		substring_t to be scanned
 * @result:	resulting integer on success
 *
 * Description: Attempts to parse the &substring_t @s as a decimal integer. On
 * success, sets @result to the integer represented by the string and returns 0.
 * Returns -ENOMEM, -EINVAL, or -ERANGE on failure.
 */
int match_int(substring_t *s, int *result)
{
	return match_number(s, result, 0);
}

/**
 * match_octal: - scan an octal representation of an integer from a substring_t
 * @s:		substring_t to be scanned
 * @result:	resulting integer on success
 *
 * Description: Attempts to parse the &substring_t @s as an octal integer. On
 * success, sets @result to the integer represented by the string and returns
 * 0. Returns -ENOMEM, -EINVAL, or -ERANGE on failure.
 */
int match_octal(substring_t *s, int *result)
{
	return match_number(s, result, 8);
}

/**
 * match_hex: - scan a hex representation of an integer from a substring_t
 * @s:		substring_t to be scanned
 * @result:	resulting integer on success
 *
 * Description: Attempts to parse the &substring_t @s as a hexadecimal integer.
 * On success, sets @result to the integer represented by the string and
 * returns 0. Returns -ENOMEM, -EINVAL, or -ERANGE on failure.
 */
int match_hex(substring_t *s, int *result)
{
	return match_number(s, result, 16);
}

/**
 * match_wildcard: - parse if a string matches given wildcard pattern
 * @pattern:	wildcard pattern
 * @str:	the string to be parsed
 *
 * Description: Parse the string @str to check if matches wildcard
 * pattern @pattern. The pattern may contain two type wildcardes:
 *   '*' - matches zero or more characters
 *   '?' - matches one character
 * If it's matched, return true, else return false.
 */
bool match_wildcard(const char *pattern, const char *str)
{
	const char *s = str;
	const char *p = pattern;
	bool star = false;

	while (*s) {
		switch (*p) {
		case '?':
			s++;
			p++;
			break;
		case '*':
			star = true;
			str = s;
			if (!*++p)
				return true;
			pattern = p;
			break;
		default:
			if (*s == *p) {
				s++;
				p++;
			} else {
				if (!star)
					return false;
				str++;
				s = str;
				p = pattern;
			}
			break;
		}
	}

	if (*p == '*')
		++p;
	return !*p;
}

/**
 * match_strlcpy: - Copy the characters from a substring_t to a sized buffer
 * @dest:	where to copy to
 * @src:	&substring_t to copy
 * @size:	size of destination buffer
 *
 * Description: Copy the characters in &substring_t @src to the
 * c-style string @dest.  Copy no more than @size - 1 characters, plus
 * the terminating NUL.  Return length of @src.
 */
size_t match_strlcpy(char *dest, const substring_t *src, size_t size)
{
	size_t ret = src->to - src->from;

	if (size) {
		size_t len = ret >= size ? size - 1 : ret;
		memcpy(dest, src->from, len);
		dest[len] = '\0';
	}
	return ret;
}

/**
 * match_strdup: - allocate a new string with the contents of a substring_t
 * @s: &substring_t to copy
 *
 * Description: Allocates and returns a string filled with the contents of
 * the &substring_t @s. The caller is responsible for freeing the returned
 * string with free().
 */
char *match_strdup(const substring_t *s)
{
	size_t sz = s->to - s->from + 1;
	char *p = malloc(sz);
	if (p)
		match_strlcpy(p, s, sz);
	return p;
}
