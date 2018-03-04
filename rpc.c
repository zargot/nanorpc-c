#include <stdio.h>

#include <curl/curl.h>
#include <json-c/json.h>

// blocking, not thread safe

#define var __auto_type
#define let var const
#define defer 

#define AMOUNT_LEN 64
#define BLOCK_ZERO "0000000000000000000000000000000000000000000000000000000000000000"

static CURL *curl;

static bool
init() __attribute__((constructor)) {
	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();
}

static void
quit() __attribute__((destructor)) {
	curl_easy_cleanup();
	curl_global_cleanup();
}

static json_object *
parse(const char *fmt, ...) {
	static char str[1024];

	va_list args;
	va_start(args, fmt);
	let len = vsnprintf(str, sizeof(str), fmt, ap);
	assert(len < sizeof(str) - 1);
	va_end(args);

	json_tokener_error err;
	var json = json_tokener_parse_verbose(str, &err);
	if (err)
		fprintf(stderr, "%s\n", json_tokener_error_desc(err));
	return json;
}

static size_t
writeres(void *ptr, size_t size, size_t nmemb, void *userp) {
	json_object **res = userp;
	*res = parse(ptr);
	return size * nmemb;
}

static json_object *
request(const char *server, json_object *req) {
	if (!req)
		return NULL;

	size_t size;
	let data = json_object_to_json_string_length(req, 0, &size);

	char postbuf[5 + size + 1];
	snprintf(postbuf, sizeof(postbuf), "data=%s", data);

	curl_easy_setopt(curl, CURLOPT_URL, server);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postbuf);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeres);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);
	let err = curl_easy_perform(curl);
	if (err) {
		fprintf(stderr, "%s\n", curl_easy_strerror(err));
		return NULL;
	}
	return res;
}

static bool
request_str(const char *server, json_object *req, const char *key, size_t len, char *buf) {
	var res = request(server, req);
	if (!res)
		return false;
	let str = json_object_get_string(json_object_object_get(res, key));
	strncpy(buf, str, len);
	json_object_put(res);
	return true;
}

bool
nano_balance(const char *acc, char balance[AMOUNT_LEN]) {
	return request_str(server,
			parse("{account_balance: {account: %s}}", acc),
			"balance", AMOUNT_LEN, balance);
}

bool
nano_send(const char *acc, const char *dst, const char *amount) {
	char block[sizeof(BLOCK_ZERO)];
	var json = parse("{wallet: %s, source: %s, destination: %s, amount: %s}",
			wallet, acc, dst, amount);
	if (!request_str(server, json, "block", sizeof(block), block))
		return false;
	if (!memcmp(block, BLOCK_ZERO, sizeof(block) - 1))
		return false;
	return true;
}

