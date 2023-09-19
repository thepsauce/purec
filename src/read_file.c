#include "purec.h"

magic_t magic_cookie;
bool magic_loaded;

static int init_magic(void)
{
	if (magic_cookie == NULL) {
		magic_cookie = magic_open(MAGIC_MIME_ENCODING);
		if (magic_cookie == NULL)
			return -1;
	}
	if (magic_cookie != NULL)
		magic_loaded = magic_load(magic_cookie, NULL) != -1;
	return magic_loaded ? 0 : -1;
}

__attribute__((destructor)) static void uninit_magic(void)
{
	/* TODO: why does this make the program crash? */
	if (magic_cookie != NULL)
		magic_close(magic_cookie);
}

char *read_file_utf8(const char *path, size_t *dest_ndata)
{
	int fd;
	const char *encoding;
	iconv_t cd = (iconv_t) -1;
	char outbufst[1024];
	char *outbuf;
	size_t outbytesleft;
	char inbufst[1024];
	char *inbuf;
	size_t inbytesleft;
	char *data, *newdata;
	size_t ndata;
	size_t nbytesconv;
	size_t nbytes;
	ssize_t r;

	encoding = init_magic() != -1 ?
		magic_file(magic_cookie, path) : NULL;
	if (encoding != NULL)
		cd = iconv_open("UTF-8", encoding);

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		iconv_close(cd);
		//purec_errno = PEOPEN;
		return NULL;
	}
	data = NULL;
	ndata = 0;
	inbytesleft = 0;
	while ((r = read(fd, inbufst + inbytesleft,
				sizeof(inbufst) - inbytesleft)) > 0) {
		inbuf = inbufst;
		inbytesleft += r;
		outbuf = outbufst;
		outbytesleft = sizeof(outbufst);
		if (cd != (iconv_t) -1) {
			nbytesconv = iconv(cd,
					&inbuf, &inbytesleft,
					&outbuf, &outbytesleft);
			if(nbytesconv == (size_t) -1) {
				//purec_errno = PEICONV;
				break;
			}
		} else {
			/* copy without converting */
			memcpy(outbuf, inbuf, inbytesleft);
			outbuf += inbytesleft;
			outbytesleft = sizeof(outbufst) - inbytesleft;
			inbuf += inbytesleft;
			inbytesleft = 0;
		}
		nbytes = sizeof(outbufst) - outbytesleft;
		newdata = realloc(data, ndata + nbytes);
		if (newdata == NULL)
			break;
		data = newdata;
		newdata += ndata;
		ndata += nbytes;
		memcpy(newdata, outbufst, nbytes);
		memmove(inbufst, inbuf, inbytesleft);
	}
	iconv_close(cd);
	close(fd);

	//purec_errno = PESUCCESS;
	*dest_ndata = ndata;
	return data;
}
