// blocking, not thread safe
//
// TODO:
// - test create
// - test send

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <curl/curl.h>
#include <json-c/json.h>

#include "common.h"
#include "util.h"

#define RET_ERR(ret, fmt, ...) { \
	fprintf(stderr, "EE:%d: ", __LINE__); \
	fprintf(stderr, fmt, ##__VA_ARGS__); \
	fprintf(stderr, "\n"); \
	return ret; \
}

#define DEFER_PUT __attribute__((cleanup(put_json))) 
#define LEN 64

static CURL *curl;
static string server, wallet;

static void
put_json(json_object **obj) {
	assert(obj);
	if (*obj)
		json_object_put(*obj);
}

static string
get_json_str(json_object *json, string key) {
	json_object *jsonval;
	if (!json_object_object_get_ex(json, key, &jsonval))
		if (strcmp(key, "error") != 0)
			RET_ERR(NULL, "invalid obj/key: %p/%s", json, key);
	if (!json_object_is_type(jsonval, json_type_string))
		if (strcmp(key, "error") != 0)
			RET_ERR(NULL, "invalid type");
	return json_object_get_string(jsonval);
}

static bool
encode(uint c, string v[], size_t bufmax, char *buf, size_t *len) {
	assert(c && v && bufmax && buf && len);

	DEFER_PUT json_object *json = json_object_new_object();
	for (uint i = 0; i < c; i += 2) {
		let key = v[i];
		let value = v[i + 1];
		if (json_object_object_add(json, key, json_object_new_string(value)))
			RET_ERR(false, "invalid key/value: %s/%s", key, value);
	}
	let str = json_object_to_json_string_length(json, 0, len);
	if (*len >= bufmax)
		RET_ERR(false, "buffer overflow");

	memcpy(buf, str, *len);
	buf[*len] = 0;
	return true;
}

static json_object *
decode(string str) {
	enum json_tokener_error err;
	var json = json_tokener_parse_verbose(str, &err);
	if (err)
		RET_ERR(NULL, "%s", json_tokener_error_desc(err));
	return json;
}

static size_t
writeres(void *ptr, size_t size, size_t nmemb, void *userp) {
	var res = (json_object **)userp;
	*res = decode((string)ptr);
	return size * nmemb;
}

static json_object *
request(string server, size_t reqc, string reqv[]) {
	static char buf[512];
	size_t len;
	if (!reqc || !reqv || !encode(reqc, reqv, sizeof(buf), buf, &len))
		RET_ERR(NULL, "invalid request");

	json_object *res = NULL;
	var ctype = curl_slist_append(NULL, "Content-Type: application/json");
	curl_easy_setopt(curl, CURLOPT_URL, server);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, ctype);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buf);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeres);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);
	let err = curl_easy_perform(curl);
	curl_slist_free_all(ctype);
	if (err)
		RET_ERR(NULL, "%s", curl_easy_strerror(err));

	string err_res = NULL;
	if ((err_res = get_json_str(res, "error")))
		RET_ERR(NULL, "server res: %s\n", err_res);

	return res;
}

static bool
request_str(string server, uint reqc, string reqv[], uint resc, char *resv[]) {
	DEFER_PUT json_object *json = request(server, reqc, reqv);
	if (!json)
		return false;

	forrange (i, 0, resc, 2) {
		let key = resv[i];
		var value = resv[i + 1];
		let str = get_json_str(json, key);
		if (!str)
			return false;
		strncpy(value, str, LEN);
	}
	return true;
}

static bool
nano_init(string node_addr, string wallet_addr) {
	if (curl_global_init(CURL_GLOBAL_ALL))
		return false;
	if (!(curl = curl_easy_init()))
		return false;
	server = node_addr;
	wallet = wallet_addr;
	return true;
}

static void
nano_quit() {
	curl_easy_cleanup(curl);
	curl_global_cleanup();
}

bool
nano_rawtonano(string raw, char *nano) {
	string req[] = {
		"action", "rai_from_raw",
		"amount", raw,
	};
	char *res[] = {
		"amount", nano,
	};
	return request_str(server, countof(req), req, countof(res), res);
}

bool
nano_create(char *acc) {
	string req[] = {
		"action", "account_create",
		"wallet", wallet,
	};
	char *res[] = {
		"account", acc,
	};
	return request_str(server, countof(req), req, countof(res), res);
}

bool
nano_balance(string acc, char *balance, char *pending) {
	string req[] = {
		"action", "account_balance",
		"account", acc,
	};
	char *res[] = {
		"balance", balance,
		"pending", pending,
	};
	return request_str(server, countof(req), req, countof(res), res);
}

bool
nano_send(string acc, string dst, string amount, string guid) {
	char block[LEN];
	string req[] = {
		"action", "send",
		"wallet", wallet,
		"source", acc,
		"destination", dst,
		"amount", amount,
		"id", guid,
	};
	char *res[] = {
		"block", block,
	};
	if (!request_str(server, countof(req), req, countof(res), res))
		return false;
	if (atoi(block) == 0)
		return false;
	return true;
}

static void
print_balance(string acc) {
	printf("\n%s:\n", acc);

	char balance[LEN], pending[LEN];
	if (!nano_balance(acc, balance, pending))
		goto err;

	char balance_nano[LEN], pending_nano[LEN];
	if (!nano_rawtonano(balance, balance_nano))
		goto err;
	if (!nano_rawtonano(pending, pending_nano))
		goto err;

	printf("balance: %s nano, pending: %s nano\n\n",
			balance_nano, pending_nano);
	return;
err:
	abort();
}

int
main(int argc, char **argv) {
	if (argc < 4) {
		printf("usage: %s server wallet account1 [account2]\n", argv[0]);
		return 1;
	}
	const string acc1 = argv[3], acc2 = argc > 4 ? argv[4] : NULL;

	if (!nano_init(argv[1], argv[2]))
		return 2;
	atexit(nano_quit);

	print_balance(acc1);
	puts("sending 1 nano from account1 to account2");
	if (!nano_send(acc1, acc2, "1000000000000000000000000", gettime_ns_str()))
		return 3;
	print_balance(acc1);
	print_balance(acc2);

	return 0;
}

