// blocking, not thread safe
//
// TODO:
// - test create
// - test send
// - validate input

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <curl/curl.h>
#include <json-c/json.h>

#define var __auto_type
#define let var const

#define RET_ERR(ret, str) { \
	fprintf(stderr, "EE: %d:%s\n", __LINE__, str); \
	return ret; \
}

#define DEFER_PUT __attribute__((cleanup(put))) 
#define LEN 64
#define INVALID_BLOCK "0000000000000000000000000000000000000000000000000000000000000000"
static_assert(LEN == sizeof(INVALID_BLOCK) - 1, "");

typedef const char *string;

static CURL *curl;
static string server, wallet;

static void
put(json_object **obj) {
	assert(obj);
	if (*obj)
		json_object_put(*obj);
}

static json_object *
parse(string str) {
	enum json_tokener_error err;
	var json = json_tokener_parse_verbose(str, &err);
	if (err)
		RET_ERR(NULL, json_tokener_error_desc(err));
	return json;
}

static size_t
writeres(void *ptr, size_t size, size_t nmemb, void *userp) {
	var res = (json_object **)userp;
	*res = parse((string)ptr);
	return size * nmemb;
}

static json_object *
request(string server, string fmt, va_list args) {
	static char buf[1024];
	let len = vsnprintf(buf, sizeof(buf), fmt, args);
	assert(len < sizeof(buf));

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
		RET_ERR(NULL, curl_easy_strerror(err));
	return res;
}

static bool
request_str(string server, string key, size_t len, char *buf, string fmt, ...) {
	va_list args;
	va_start(args, fmt);
	DEFER_PUT json_object *res = request(server, fmt, args);
	va_end(args);
	if (!res)
		RET_ERR(false, "");
	json_object *value;
	if (!json_object_object_get_ex(res, key, &value))
		RET_ERR(false, "invalid obj or key");
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
	return request_str(server,
			"account", LEN, acc,
			"{\"action\": \"account_create\", \"wallet\": \"%s\"}",
			wallet);
}

bool
nano_balance(string acc, char balance[LEN]) {
	return request_str(server,
			"balance", LEN, balance,
			"{\"action\": \"account_balance\", \"account\": \"%s\"}",
			acc);
}

bool
nano_send(string acc, string dst, string amount, string guid) {
	char block[LEN];
	if (!request_str(server,
			"block", sizeof(block), block,
			"{"
			"\"action\": \"send\","
			"\"wallet\": \"%s\","
			"\"source\": \"%s\","
			"\"destination\": \"%s\","
			"\"amount\": \"%s\","
			"\"id\": \"%s\""
			"}",
			wallet, acc, dst, amount, guid)) {
		return false;
	}
	if (!memcmp(block, INVALID_BLOCK, LEN))
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

