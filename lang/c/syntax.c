#include "purec.h"

struct syntax_c {
	Syntax syntax;
	char conceal[4];
};

struct syntax_c *syntax_new(void)
{
	struct syntax_c *syn;
       
	syn = malloc(sizeof(*syn));
	if (syn == NULL)
		return NULL;
	memset(syn, 0, sizeof(*syn));
	return syn;
}

int syntax_next(struct syntax_c *c)
{
	Syntax *const syn = &c->syntax;
	Buffer *const buf = syn->buffer;

	switch (buf->data[syn->index]) {
	case 'a' ... 'z': case 'A' ... 'Z': case '_':
		syn->attr = A_BOLD;
		while (isalnum(buf->data[syn->index]))
			syn->index++;
		break;
	default:
		syn->attr = 0;
		syn->index++;
	}
	return 0;
}

void syntax_destroy(struct syntax_c *c)
{
	free(c);
}
