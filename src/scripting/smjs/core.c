/* ECMAScript browser scripting module */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "elinks.h"

#include "config/home.h"
#include "ecmascript/spidermonkey-shared.h"
#include "main/module.h"
#include "osdep/osdep.h"
#include "scripting/scripting.h"
#include "scripting/smjs/core.h"
#include "scripting/smjs/elinks_object.h"
#include "scripting/smjs/global_object.h"
#include "scripting/smjs/smjs.h"
#include "util/file.h"
#include "util/string.h"


#define SMJS_HOOKS_FILENAME "hooks.js"

JSContext *smjs_ctx;
JSObject *smjs_elinks_object;
struct session *smjs_ses;


void
alert_smjs_error(unsigned char *msg)
{
	report_scripting_error(&smjs_scripting_module,
	                       smjs_ses, msg);
}

void
smjs_error_reporter(JSContext *ctx, const char *message, JSErrorReport *report)
{
	unsigned char *strict, *exception, *warning, *error;
	struct string msg;

	if (!init_string(&msg)) goto reported;

	strict	  = JSREPORT_IS_STRICT(report->flags) ? " strict" : "";
	exception = JSREPORT_IS_EXCEPTION(report->flags) ? " exception" : "";
	warning   = JSREPORT_IS_WARNING(report->flags) ? " warning" : "";
	error	  = !report->flags ? " error" : "";

	add_format_to_string(&msg, "A client script raised the following%s%s%s%s",
			strict, exception, warning, error);

	add_to_string(&msg, ":\n\n");
	add_to_string(&msg, message);

	if (report->linebuf && report->tokenptr) {
		int pos = report->tokenptr - report->linebuf;

		add_format_to_string(&msg, "\n\n%s\n.%*s^%*s.",
			       report->linebuf,
			       pos - 2, " ",
			       strlen(report->linebuf) - pos - 1, " ");
	}

	alert_smjs_error(msg.source);
	done_string(&msg);

reported:
	JS_ClearPendingException(ctx);
}

static int
smjs_do_file(unsigned char *path)
{
	int ret = 1;
	jsval rval;
	struct string script;

	if (!init_string(&script)) return 0;

	if (!add_file_to_string(&script, path)
	     || JS_FALSE == JS_EvaluateScript(smjs_ctx,
				JS_GetGlobalObject(smjs_ctx),
				script.source, script.length, path, 1, &rval)) {
		alert_smjs_error("error loading script file");
		ret = 0;
	}

	done_string(&script);

	return ret;
}

static JSBool
smjs_do_file_wrapper(JSContext *ctx, JSObject *obj, uintN argc,
                     jsval *argv, jsval *rval)
{
	JSString *jsstr = JS_ValueToString(smjs_ctx, *argv);
	unsigned char *path = JS_GetStringBytes(jsstr);

	if (smjs_do_file(path))
		return JS_TRUE;

	return JS_FALSE;
}

static void
smjs_load_hooks(void)
{
	unsigned char *path;

	assert(smjs_ctx);

	if (elinks_home) {
		path = straconcat(elinks_home, SMJS_HOOKS_FILENAME,
				  (unsigned char *) NULL);
	} else {
		path = stracpy(CONFDIR STRING_DIR_SEP SMJS_HOOKS_FILENAME);
	}

	if (file_exists(path))
		smjs_do_file(path);
	mem_free(path);
}

void
init_smjs(struct module *module)
{
	if (!spidermonkey_runtime_addref()) return;

	/* Set smjs_ctx immediately after creating the JSContext, so
	 * that any error reports from SpiderMonkey are forwarded to
	 * smjs_error_reporter().  */
	smjs_ctx = JS_NewContext(spidermonkey_runtime, 8192);
	if (!smjs_ctx) {
		spidermonkey_runtime_release();
		return;
	}

	smjs_init_global_object();

	smjs_init_elinks_object();

	JS_DefineFunction(smjs_ctx, smjs_global_object, "do_file",
	                  &smjs_do_file_wrapper, 1, 0);

	smjs_load_hooks();
}

void
cleanup_smjs(struct module *module)
{
	if (!smjs_ctx) return;

	/* JS_DestroyContext also collects garbage in the JSRuntime.
	 * Because the JSObjects created in smjs_ctx have not been
	 * made visible to any other JSContext, and the garbage
	 * collector of SpiderMonkey is precise, SpiderMonkey
	 * finalizes all of those objects, so cache_entry_finalize
	 * gets called and resets each cache_entry.jsobject = NULL.
	 * If the garbage collector were conservative, ELinks would
	 * have to call smjs_detach_cache_entry_object on each cache
	 * entry before it releases the runtime here.  */
	JS_DestroyContext(smjs_ctx);
	spidermonkey_runtime_release();
}
