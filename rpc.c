// blocking, not thread safe

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <curl/curl.h>
#include <json-c/json.h>

#define var __auto_type
#define let var const

#define LEN 64
#define INVALID_BLOCK "0000000000000000000000000000000000000000000000000000000000000000"
static_assert(LEN == sizeof(INVALID_BLOCK) - 1, "");

typedef const char *string;

static CURL *curl;
static string server, *wallet;

static json_object *
parse(string str) {
	enum json_tokener_error err;
	var json = json_tokener_parse_verbose(str, &err);
	if (err)
		fprintf(stderr, "%s\n", json_tokener_error_desc(err));
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
	const char prefix[] = "data=";
	strcpy(buf, prefix);
	let len = vsnprintf(buf + sizeof(prefix), sizeof(buf), fmt, args);
	assert(buf[sizeof(buf) - 2] == 0);

	json_object *res = NULL;
	curl_easy_setopt(curl, CURLOPT_URL, server);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buf);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeres);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);
	let err = curl_easy_perform(curl);
	if (err) {
		fprintf(stderr, "%s\n", curl_easy_strerror(err));
		if (res)
			json_object_put(res);
		res = NULL;
	}
	return res;
}

static bool
request_str(string server, string key, size_t len, char *buf, string fmt, ...) {
	va_list args;
	va_start(args, fmt);
	var res = request(server, fmt, args);
	va_end(args);
	if (!res)
		return false;
	let str = json_object_get_string(json_object_object_get(res, key));
	strncpy(buf, str, len);
	json_object_put(res);
	return true;
}

static bool
nano_init(string node_addr, string wallet) {
	if (curl_global_init(CURL_GLOBAL_ALL))
		return false;
	if (!(curl = curl_easy_init()))
		return false;
	server = node_addr;
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
			"{'action': 'account_create', 'wallet': '%s'}", wallet);
}

bool
nano_balance(string acc, char balance[LEN]) {
	return request_str(server,
			"balance", LEN, balance,
			"{'action': 'account_balance', 'account': '%s'}", acc);
}

bool
nano_send(string acc, string dst, string amount, string guid) {
	char block[LEN];
	if (!request_str(server,
			"block", sizeof(block), block,
			"{"
			"'action': 'send',"
			"'wallet': '%s',"
			"'source': '%s',"
			"'destination': '%s',"
			"'amount': '%s',"
			"'id': '%s'"
			"}",
			wallet, acc, dst, amount, guid)) {
		return false;
	}
	if (!memcmp(block, INVALID_BLOCK, LEN))
		return false;
	return true;
}

