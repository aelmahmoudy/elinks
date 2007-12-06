/* Digest MD5 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "elinks.h"

#include "protocol/auth/auth.h"
#include "protocol/auth/digest.h"
#include "util/conv.h"
#include "util/md5.h"
#include "util/memory.h"


/* Hexes a binary md5 digest. Taken from RFC 2617 */
static void
convert_to_md5_digest_hex_T(md5_digest_bin_T bin, md5_digest_hex_T hex)
{
	int i;

	for (i = 0; i < sizeof(bin); i++) {
		int j = i * 2;

		hex[j]   = hx(bin[i] >> 4 & 0xF);
		hex[j+1] = hx(bin[i] & 0xF);
	}
}

/* Initializes a random cnonce that is also a hexed md5 digest. */
static void
init_cnonce_digest(md5_digest_hex_T cnonce)
{
	md5_digest_bin_T md5;
	int random;

	srand(time(NULL));

	random = rand();
	MD5((const unsigned char *) &random, sizeof(random), md5);

	convert_to_md5_digest_hex_T(md5, cnonce);
}

/* Initializes what RFC 2617 refers to as H(A1) by digesting and hexing the
 * credentials: <user> ':' <realm> ':' <password> */
/* FIXME: Support for further digesting: H(A1) ':' <nonce> ':' <cnonce> if
 * the server requests algorithm = "MD5-sess". */
static void
init_credential_digest(md5_digest_hex_T ha1, struct auth_entry *entry)
{
	MD5_CTX MD5Ctx;
	md5_digest_bin_T skey;

	MD5_Init(&MD5Ctx);
	MD5_Update(&MD5Ctx, entry->user, strlen(entry->user));
	MD5_Update(&MD5Ctx, ":", 1);
	MD5_Update(&MD5Ctx, entry->realm, strlen(entry->realm));
	MD5_Update(&MD5Ctx, ":", 1);
	MD5_Update(&MD5Ctx, entry->password, strlen(entry->password));
	MD5_Final(skey, &MD5Ctx);

	convert_to_md5_digest_hex_T(skey, ha1);
}

/* Initializes what RFC 2617 refers to as H(A2) by digesting and hexing the
 * method and requested URI path. */
/* FIXME: Support for more HTTP access methods (POST). */
/* FIXME: Support for by appending ':' H(entity body) if qop = "auti-int". The
 * H(entity-body) is the hash of the entity body, not the message body - it is
 * computed before any transfer encoding is applied by the sender and after it
 * has been removed by the recipient. Note that this includes multipart
 * boundaries and embedded headers in each part of any multipart content-type.
 */
static void
init_uri_method_digest(md5_digest_hex_T uri_method, struct uri *uri)
{
	MD5_CTX MD5Ctx;
	md5_digest_bin_T ha2;

	MD5_Init(&MD5Ctx);
	MD5_Update(&MD5Ctx, "GET", 3);
	MD5_Update(&MD5Ctx, ":/", 2);
	MD5_Update(&MD5Ctx, uri->data, uri->datalen);
	MD5_Final(ha2, &MD5Ctx);

	convert_to_md5_digest_hex_T(ha2, uri_method);
}

/* Calculates the value of the response parameter in the Digest header entry. */
/* FIXME: Support for also digesting: <nonce-count> ':' <cnonce> ':' <qoup> ':'
 * before digesting the H(A2) value if the qop Digest header entry parameter is
 * non-empty. */
static void
init_response_digest(md5_digest_hex_T response, struct auth_entry *entry,
		     struct uri *uri, md5_digest_hex_T cnonce)
{
	MD5_CTX MD5Ctx;
	md5_digest_hex_T ha1;
	md5_digest_bin_T Ha2;
	md5_digest_hex_T Ha2_hex;

	init_credential_digest(ha1, entry);
	init_uri_method_digest(Ha2_hex, uri);

	MD5_Init(&MD5Ctx);
	MD5_Update(&MD5Ctx, ha1, sizeof(ha1));
	MD5_Update(&MD5Ctx, ":", 1);
	if (entry->nonce)
		MD5_Update(&MD5Ctx, entry->nonce, strlen(entry->nonce));
	MD5_Update(&MD5Ctx, ":", 1);
	MD5_Update(&MD5Ctx, "00000001", 8);
	MD5_Update(&MD5Ctx, ":", 1);
	MD5_Update(&MD5Ctx, cnonce, sizeof(cnonce));
	MD5_Update(&MD5Ctx, ":", 1);
	MD5_Update(&MD5Ctx, "auth", 4);
	MD5_Update(&MD5Ctx, ":", 1);
	MD5_Update(&MD5Ctx, Ha2_hex, sizeof(Ha2_hex));
	MD5_Final(Ha2, &MD5Ctx);

	convert_to_md5_digest_hex_T(Ha2, response);
}


unsigned char *
get_http_auth_digest_response(struct auth_entry *entry, struct uri *uri)
{
	struct string string;
	md5_digest_hex_T cnonce;
	md5_digest_hex_T response;

	if (!init_string(&string))
		return NULL;

	init_cnonce_digest(cnonce);
	init_response_digest(response, entry, uri, cnonce);

	add_to_string(&string, "username=\"");
	add_to_string(&string, entry->user);
	add_to_string(&string, "\", ");
	add_to_string(&string, "realm=\"");
	if (entry->realm)
		add_to_string(&string, entry->realm);
	add_to_string(&string, "\", ");
	add_to_string(&string, "nonce=\"");
	if (entry->nonce)
		add_to_string(&string, entry->nonce);
	add_to_string(&string, "\", ");
	add_to_string(&string, "uri=\"/");
	add_bytes_to_string(&string, uri->data, uri->datalen);
	add_to_string(&string, "\", ");
	add_to_string(&string, "qop=auth, nc=00000001, ");
	add_to_string(&string, "cnonce=\"");
	add_bytes_to_string(&string, cnonce, sizeof(cnonce));
	add_to_string(&string, "\", ");
	add_to_string(&string, "response=\"");
	add_bytes_to_string(&string, response, sizeof(response));
	add_to_string(&string, "\"");

	if (entry->opaque) {
		add_to_string(&string, ", opaque=\"");
		add_to_string(&string, entry->opaque);
		add_to_string(&string, "\"");
	}

	return string.source;
}
