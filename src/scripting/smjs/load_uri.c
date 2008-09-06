/* ECMAScript browser scripting module */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "elinks.h"

#include "ecmascript/spidermonkey/util.h"
#include "network/connection.h"
#include "protocol/uri.h"
#include "scripting/smjs/core.h"
#include "scripting/smjs/cache_object.h"
#include "scripting/smjs/elinks_object.h"
#include "session/download.h"


struct smjs_load_uri_hop {
	struct session *ses;
	JSFunction *callback;
};

static void
smjs_loading_callback(struct download *download, void *data)
{
	struct session *saved_smjs_ses = smjs_ses;
	struct smjs_load_uri_hop *hop = data;
	jsval args[1], rval;
	JSObject *cache_entry_object;

	if (is_in_progress_state(download->state)) return;

	if (!download->cached) goto end;

	assert(hop->callback);

	smjs_ses = hop->ses;

	cache_entry_object = smjs_get_cache_entry_object(download->cached);
	if (!cache_entry_object) goto end;

	args[0] = OBJECT_TO_JSVAL(cache_entry_object);

	JS_CallFunction(smjs_ctx, NULL, hop->callback, 1, args, &rval);

end:
	mem_free(download->data);
	mem_free(download);

	smjs_ses = saved_smjs_ses;
}

static JSBool
smjs_load_uri(JSContext *ctx, JSObject *obj, uintN argc, jsval *argv,
              jsval *rval)
{
	struct smjs_load_uri_hop *hop;
	struct download *download;
	JSString *jsstr;
	unsigned char *uri_string;
	struct uri *uri;

	if (argc < 2) return JS_FALSE;

	jsstr = JS_ValueToString(smjs_ctx, argv[0]);
	uri_string = JS_GetStringBytes(jsstr);

	uri = get_uri(uri_string, 0);
	if (!uri) return JS_FALSE;

	download = mem_alloc(sizeof(*download));
	if (!download) {
		done_uri(uri);
		return JS_FALSE;
	}

	hop = mem_alloc(sizeof(*hop));
	if (!hop) {
		done_uri(uri);
		mem_free(download);
		return JS_FALSE;
	}

	hop->callback = JS_ValueToFunction(smjs_ctx, argv[1]);
	hop->ses = smjs_ses;

	download->data = hop;
	download->callback = (download_callback_T *) smjs_loading_callback;

	load_uri(uri, NULL, download, PRI_MAIN, CACHE_MODE_NORMAL, -1);

	done_uri(uri);

	return JS_TRUE;
}

void
smjs_init_load_uri_interface(void)
{
	if (!smjs_ctx || !smjs_elinks_object)
		return;

	JS_DefineFunction(smjs_ctx, smjs_elinks_object, "load_uri",
	                  &smjs_load_uri, 2, 0);
}
