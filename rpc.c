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

#define var __auto_type
#define let var const
#define countof(a) (sizeof(a) / sizeof((a)[0]))

#define RET_ERR(ret, fmt, ...) { \
	fprintf(stderr, "EE:%d: ", __LINE__); \
	fprintf(stderr, fmt, ##__VA_ARGS__); \
	fprintf(stderr, "\n"); \
	return ret; \
}

#define DEFER_PUT __attribute__((cleanup(put))) 
#define LEN 64
#define INVALID_BLOCK "0000000000000000000000000000000000000000000000000000000000000000"
static_assert(LEN == sizeof(INVALID_BLOCK) - 1, "");

typedef unsigned uint;
typedef const char *string;

static CURL *curl;
static string server, wallet;

static void
put(json_object **obj) {
	assert(obj);
	if (*obj)
		json_object_put(*obj);
}

static bool
validate_str(string s) {
	char c;
	while ((c = *s++)) {
		if (!isalnum(c) && c != '_')
			return false;
	}
	return true;
}

static bool
encode(uint c, string v[], size_t *outlen, char **outbuf) {
	static char buf[512];
	memset(buf, 0, sizeof(buf));
	int len = 0;

	buf[len++] = '{';
	for (uint i = 0; i < c; i += 2) {
		let key = v[i];
		let value = v[i + 1];
		if (validate_str(key) && validate_str(value)) {
			let diff = snprintf(buf + len, sizeof(buf) - len,
					"\"%s\":\"%s\",", key, value);
			if (diff > 0) {
				len += diff;
				continue;
			}
		}
		RET_ERR(0, "invalid key/value: %s/%s", key, value);
	}
	assert(len > 0);
	len = fmax(len-1, 1);
	if (len + 1 >= sizeof(buf))
		RET_ERR(0, "buffer overflow: %d/%zu", len + 2, sizeof(buf));
	buf[len++] = '}';
	assert(len <= sizeof(buf));

	*outlen = len;
	*outbuf = buf;
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
	char *buf;
	size_t len;
	if (!reqc || !reqv || !encode(reqc, reqv, &len, &buf))
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
	return res;
}

static bool
request_str(string server, uint reqc, string reqv[], string key, size_t len, char *buf) {
	DEFER_PUT json_object *res = request(server, reqc, reqv);
	if (!res)
		RET_ERR(false, "");
	json_object *value;
	if (!json_object_object_get_ex(res, key, &value))
		RET_ERR(false, "invalid obj/key: %p/%s", res, key);
	if (!json_object_is_type(value, json_type_string))
		RET_ERR(false, "invalid type");
	strncpy(buf, json_object_get_string(value), len);
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
nano_create(char *acc) {
	string req[] = {
		"action", "account_create",
		"wallet", wallet,
	};
	return request_str(server, countof(req), req, "account", LEN, acc);
}

bool
nano_balance(string acc, char balance[LEN]) {
	string req[] = {
		"action", "account_balance",
		"account", acc,
	};
	return request_str(server, countof(req), req, "balance", LEN, balance);
}

bool
nano_send(string acc, string dst, string amount, string guid) {
	char res[LEN];
	string req[] = {
			"action", "send",
			"wallet", wallet,
			"source", acc,
			"destination", dst,
			"amount", amount,
			"id", guid,
	};
	if (!request_str(server, countof(req), req, "block", sizeof(res), res))
		return false;
	if (!memcmp(res, INVALID_BLOCK, LEN))
		return false;
	return true;
}

int
main(int argc, char **argv) {
	if (argc < 4) {
		printf("usage: %s server wallet account\n", argv[0]);
		return 1;
	}

	if (!nano_init(argv[1], argv[2]))
		return 2;
	atexit(nano_quit);

	char balance[LEN];
	if (!nano_balance(argv[3], balance))
		return 3;
	puts(balance);

	return 0;
}

