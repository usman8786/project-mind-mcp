/*
 * service_patterns.c — Classify call edges by library identity in resolved QN.
 *
 * Instead of matching callee names (ambiguous: "get", "post", "send"),
 * we match library identifiers in the RESOLVED qualified name. The QN
 * contains the full module path, so import aliases are transparent:
 *   r.get("/api") → QN: project.venv.requests.api.get → match "requests" → HTTP_CALLS
 *
 * Two-level matching:
 *   1. Library identifier in QN → determines edge type (HTTP/ASYNC/CONFIG)
 *   2. Method suffix → determines HTTP method (get→GET, post→POST)
 */
#include "service_patterns.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* ── Library identifier → edge type ────────────────────────────── */

typedef struct {
    const char *library_id; /* substring to find in resolved QN */
    pmm_svc_kind_t kind;    /* HTTP_CALLS, ASYNC_CALLS, CONFIGURES */
    const char *broker;     /* for ASYNC: broker name (NULL otherwise) */
} lib_pattern_t;

/* HTTP client libraries — match these substrings in the resolved QN.
 * Sources: github.com/easybase/awesome-http, official SDK docs, agent research */
static const lib_pattern_t http_libraries[] = {
    /* Python */
    {"requests", PMM_SVC_HTTP, NULL},
    {"httpx", PMM_SVC_HTTP, NULL},
    {"aiohttp", PMM_SVC_HTTP, NULL},
    {"urllib", PMM_SVC_HTTP, NULL},
    {"urllib3", PMM_SVC_HTTP, NULL},
    {"httplib2", PMM_SVC_HTTP, NULL},
    {"pycurl", PMM_SVC_HTTP, NULL},
    {"treq", PMM_SVC_HTTP, NULL},
    {"uplink", PMM_SVC_HTTP, NULL},

    /* JavaScript / TypeScript */
    {"axios", PMM_SVC_HTTP, NULL},
    {"superagent", PMM_SVC_HTTP, NULL},
    {"needle", PMM_SVC_HTTP, NULL},
    {"node-fetch", PMM_SVC_HTTP, NULL},
    {"undici", PMM_SVC_HTTP, NULL},
    {"ofetch", PMM_SVC_HTTP, NULL},
    {"wretch", PMM_SVC_HTTP, NULL},
    {"sindresorhus/ky", PMM_SVC_HTTP, NULL},
    {"phin", PMM_SVC_HTTP, NULL},

    /* Go */
    {"net/http", PMM_SVC_HTTP, NULL},
    {"resty", PMM_SVC_HTTP, NULL},
    {"sling", PMM_SVC_HTTP, NULL},
    {"heimdall", PMM_SVC_HTTP, NULL},
    {"gentleman", PMM_SVC_HTTP, NULL},
    {"retryablehttp", PMM_SVC_HTTP, NULL},

    /* Java / Kotlin */
    {"HttpClient", PMM_SVC_HTTP, NULL},
    {"OkHttp", PMM_SVC_HTTP, NULL},
    {"okhttp3", PMM_SVC_HTTP, NULL},
    {"RestTemplate", PMM_SVC_HTTP, NULL},
    {"WebClient", PMM_SVC_HTTP, NULL},
    {"Unirest", PMM_SVC_HTTP, NULL},
    {"AsyncHttpClient", PMM_SVC_HTTP, NULL},
    {"apache.http", PMM_SVC_HTTP, NULL},
    {"Retrofit", PMM_SVC_HTTP, NULL},
    {"Feign", PMM_SVC_HTTP, NULL},
    {"ktor.client", PMM_SVC_HTTP, NULL},
    {"kittinunf.fuel", PMM_SVC_HTTP, NULL},

    /* Rust */
    {"reqwest", PMM_SVC_HTTP, NULL},
    {"hyper", PMM_SVC_HTTP, NULL},
    {"surf", PMM_SVC_HTTP, NULL},
    {"ureq", PMM_SVC_HTTP, NULL},
    {"isahc", PMM_SVC_HTTP, NULL},
    {"attohttpc", PMM_SVC_HTTP, NULL},

    /* C# */
    {"HttpClient", PMM_SVC_HTTP, NULL},
    {"RestSharp", PMM_SVC_HTTP, NULL},
    {"Flurl", PMM_SVC_HTTP, NULL},
    {"Refit", PMM_SVC_HTTP, NULL},

    /* Ruby */
    {"HTTParty", PMM_SVC_HTTP, NULL},
    {"Faraday", PMM_SVC_HTTP, NULL},
    {"RestClient", PMM_SVC_HTTP, NULL},
    {"Typhoeus", PMM_SVC_HTTP, NULL},
    {"Excon", PMM_SVC_HTTP, NULL},
    {"Net::HTTP", PMM_SVC_HTTP, NULL},

    /* PHP */
    {"Guzzle", PMM_SVC_HTTP, NULL},
    {"guzzle", PMM_SVC_HTTP, NULL},
    {"curl", PMM_SVC_HTTP, NULL},
    {"Symfony\\HttpClient", PMM_SVC_HTTP, NULL},

    /* C/C++ */
    {"cpr", PMM_SVC_HTTP, NULL},
    {"cpp-httplib", PMM_SVC_HTTP, NULL},
    {"Poco.Net", PMM_SVC_HTTP, NULL},
    {"Beast", PMM_SVC_HTTP, NULL},

    /* Swift */
    {"Alamofire", PMM_SVC_HTTP, NULL},
    {"Moya", PMM_SVC_HTTP, NULL},
    {"URLSession", PMM_SVC_HTTP, NULL},

    /* Dart */
    {"Dio", PMM_SVC_HTTP, NULL},
    {"dio", PMM_SVC_HTTP, NULL},
    {"package:http", PMM_SVC_HTTP, NULL},
    {"Chopper", PMM_SVC_HTTP, NULL},

    /* Elixir */
    {"HTTPoison", PMM_SVC_HTTP, NULL},
    {"Tesla", PMM_SVC_HTTP, NULL},
    {"Finch", PMM_SVC_HTTP, NULL},
    {"Mint.HTTP", PMM_SVC_HTTP, NULL},

    /* Scala */
    {"sttp", PMM_SVC_HTTP, NULL},
    {"akka.http", PMM_SVC_HTTP, NULL},
    {"http4s", PMM_SVC_HTTP, NULL},
    {"scalaj", PMM_SVC_HTTP, NULL},

    /* Haskell */
    {"wreq", PMM_SVC_HTTP, NULL},
    {"http-client", PMM_SVC_HTTP, NULL},
    {"http-conduit", PMM_SVC_HTTP, NULL},
    {"servant-client", PMM_SVC_HTTP, NULL},
    {"Network.HTTP", PMM_SVC_HTTP, NULL},

    /* Lua */
    {"socket.http", PMM_SVC_HTTP, NULL},
    {"resty.http", PMM_SVC_HTTP, NULL},

    {NULL, PMM_SVC_NONE, NULL},
};

/* Async dispatch / message broker libraries */
static const lib_pattern_t async_libraries[] = {
    /* GCP */
    {"cloudtasks", PMM_SVC_ASYNC, "cloud_tasks"},
    {"cloud_tasks", PMM_SVC_ASYNC, "cloud_tasks"},
    {"cloud.tasks", PMM_SVC_ASYNC, "cloud_tasks"},
    {"CloudTasks", PMM_SVC_ASYNC, "cloud_tasks"},
    {"pubsub", PMM_SVC_ASYNC, "pubsub"},
    {"cloud.pubsub", PMM_SVC_ASYNC, "pubsub"},
    {"PubSub", PMM_SVC_ASYNC, "pubsub"},

    /* AWS — use SDK module paths to avoid false positives.  pmm_fqn_compute
     * converts path slashes to '.', so a resolved local Go QN reads
     * "aws-sdk-go.service.sqs..."; include both slash and dot forms so the
     * substring match fires whether the id comes from an import path or a QN. */
    {"aws-sdk-go/service/sqs", PMM_SVC_ASYNC, "sqs"},
    {"aws-sdk-go.service.sqs", PMM_SVC_ASYNC, "sqs"},
    {"aws_sdk_sqs", PMM_SVC_ASYNC, "sqs"},
    {"Amazon.SQS", PMM_SVC_ASYNC, "sqs"},
    {"@aws-sdk/client-sqs", PMM_SVC_ASYNC, "sqs"},
    {"boto3.client.sqs", PMM_SVC_ASYNC, "sqs"},
    {"aws-sdk-go/service/sns", PMM_SVC_ASYNC, "sns"},
    {"aws-sdk-go.service.sns", PMM_SVC_ASYNC, "sns"},
    {"aws_sdk_sns", PMM_SVC_ASYNC, "sns"},
    {"Amazon.SNS", PMM_SVC_ASYNC, "sns"},
    {"@aws-sdk/client-sns", PMM_SVC_ASYNC, "sns"},
    {"eventbridge", PMM_SVC_ASYNC, "eventbridge"},
    {"EventBridge", PMM_SVC_ASYNC, "eventbridge"},
    {"aws-sdk-go/service/lambda", PMM_SVC_ASYNC, "lambda"},
    {"aws-sdk-go.service.lambda", PMM_SVC_ASYNC, "lambda"},
    {"aws_sdk_lambda", PMM_SVC_ASYNC, "lambda"},
    {"@aws-sdk/client-lambda", PMM_SVC_ASYNC, "lambda"},
    {"stepfunctions", PMM_SVC_ASYNC, "stepfunctions"},

    /* Azure */
    {"ServiceBus", PMM_SVC_ASYNC, "servicebus"},
    {"Azure.Messaging", PMM_SVC_ASYNC, "servicebus"},

    /* Kafka */
    {"kafka", PMM_SVC_ASYNC, "kafka"},
    {"Kafka", PMM_SVC_ASYNC, "kafka"},
    {"kafkajs", PMM_SVC_ASYNC, "kafka"},
    {"sarama", PMM_SVC_ASYNC, "kafka"},
    {"rdkafka", PMM_SVC_ASYNC, "kafka"},
    {"confluent", PMM_SVC_ASYNC, "kafka"},
    {"Confluent.Kafka", PMM_SVC_ASYNC, "kafka"},

    /* RabbitMQ */
    {"amqp", PMM_SVC_ASYNC, "rabbitmq"},
    {"AMQP", PMM_SVC_ASYNC, "rabbitmq"},
    {"amqplib", PMM_SVC_ASYNC, "rabbitmq"},
    {"RabbitMQ", PMM_SVC_ASYNC, "rabbitmq"},
    {"lapin", PMM_SVC_ASYNC, "rabbitmq"},
    {"MassTransit", PMM_SVC_ASYNC, "rabbitmq"},

    /* NATS */
    {"nats", PMM_SVC_ASYNC, "nats"},
    {"NATS", PMM_SVC_ASYNC, "nats"},

    /* Redis pub/sub */
    {"ioredis", PMM_SVC_ASYNC, "redis"},

    /* Task queues */
    {"celery", PMM_SVC_ASYNC, "celery"},
    {"Celery", PMM_SVC_ASYNC, "celery"},
    {"dramatiq", PMM_SVC_ASYNC, "dramatiq"},
    {"huey", PMM_SVC_ASYNC, "huey"},
    {"python-rq", PMM_SVC_ASYNC, "rq"},
    {"rq.Queue", PMM_SVC_ASYNC, "rq"},
    {"bullmq", PMM_SVC_ASYNC, "bullmq"},
    {"BullMQ", PMM_SVC_ASYNC, "bullmq"},
    {"bull.Queue", PMM_SVC_ASYNC, "bull"},
    {"Sidekiq", PMM_SVC_ASYNC, "sidekiq"},
    {"sidekiq", PMM_SVC_ASYNC, "sidekiq"},
    {"Resque", PMM_SVC_ASYNC, "resque"},
    {"GoodJob", PMM_SVC_ASYNC, "goodjob"},
    {"DelayedJob", PMM_SVC_ASYNC, "delayed_job"},
    {"Hangfire", PMM_SVC_ASYNC, "hangfire"},
    {"NServiceBus", PMM_SVC_ASYNC, "nservicebus"},
    {"asynq", PMM_SVC_ASYNC, "asynq"},
    {"RichardKnop/machinery", PMM_SVC_ASYNC, "machinery"},

    /* Workflow engines — use specific module paths to avoid "Temporal" in Django etc. */
    {"temporalio", PMM_SVC_ASYNC, "temporal"},
    {"@temporalio", PMM_SVC_ASYNC, "temporal"},
    {"temporal.client", PMM_SVC_ASYNC, "temporal"},
    {"temporal.worker", PMM_SVC_ASYNC, "temporal"},
    {"inngest", PMM_SVC_ASYNC, "inngest"},

    /* Elixir */
    {"Oban", PMM_SVC_ASYNC, "oban"},
    {"Broadway", PMM_SVC_ASYNC, "broadway"},
    {"GenStage", PMM_SVC_ASYNC, "genstage"},
    {"Phoenix.PubSub", PMM_SVC_ASYNC, "phoenix_pubsub"},

    /* Scala */
    {"Alpakka", PMM_SVC_ASYNC, "alpakka"},

    /* MQTT */
    {"mqtt", PMM_SVC_ASYNC, "mqtt"},
    {"paho.mqtt", PMM_SVC_ASYNC, "mqtt"},
    {"MQTTClient", PMM_SVC_ASYNC, "mqtt"},
    {"mosquitto", PMM_SVC_ASYNC, "mqtt"},
    {"asyncio_mqtt", PMM_SVC_ASYNC, "mqtt"},
    {"gmqtt", PMM_SVC_ASYNC, "mqtt"},
    {"rumqttc", PMM_SVC_ASYNC, "mqtt"},

    /* NATS */
    {"nats.go", PMM_SVC_ASYNC, "nats"},
    {"nats-py", PMM_SVC_ASYNC, "nats"},
    {"nats.ws", PMM_SVC_ASYNC, "nats"},
    {"nats.java", PMM_SVC_ASYNC, "nats"},
    {"nats.net", PMM_SVC_ASYNC, "nats"},
    {"async-nats", PMM_SVC_ASYNC, "nats"},
    {"nats.rs", PMM_SVC_ASYNC, "nats"},

    /* Dapr pub/sub */
    {"dapr.clients.grpc", PMM_SVC_ASYNC, "dapr"},
    {"DaprClient", PMM_SVC_ASYNC, "dapr"},

    {NULL, PMM_SVC_NONE, NULL},
};

/* Config accessor libraries */
static const lib_pattern_t config_libraries[] = {
    /* Universal */
    {"getenv", PMM_SVC_CONFIG, NULL},
    {"Getenv", PMM_SVC_CONFIG, NULL},
    {"getEnv", PMM_SVC_CONFIG, NULL},
    {"LookupEnv", PMM_SVC_CONFIG, NULL},
    {"lookupEnv", PMM_SVC_CONFIG, NULL},
    {"get_env", PMM_SVC_CONFIG, NULL},
    {"fetch_env", PMM_SVC_CONFIG, NULL},
    {"GetEnvironmentVariable", PMM_SVC_CONFIG, NULL},
    {"getProperty", PMM_SVC_CONFIG, NULL},
    {"getEnvironment", PMM_SVC_CONFIG, NULL},

    /* Go */
    {"viper", PMM_SVC_CONFIG, NULL},
    {"envconfig", PMM_SVC_CONFIG, NULL},
    {"godotenv", PMM_SVC_CONFIG, NULL},

    /* Python */
    {"decouple", PMM_SVC_CONFIG, NULL},
    {"dynaconf", PMM_SVC_CONFIG, NULL},
    {"dotenv", PMM_SVC_CONFIG, NULL},

    /* JS/TS */
    {"nconf", PMM_SVC_CONFIG, NULL},
    {"convict", PMM_SVC_CONFIG, NULL},
    {"envalid", PMM_SVC_CONFIG, NULL},

    /* Rust */
    {"dotenvy", PMM_SVC_CONFIG, NULL},
    {"figment", PMM_SVC_CONFIG, NULL},
    {"config-rs", PMM_SVC_CONFIG, NULL},

    /* Java/Scala */
    {"ConfigFactory", PMM_SVC_CONFIG, NULL},
    {"ConfigurationProperties", PMM_SVC_CONFIG, NULL},

    /* Elixir */
    {"Application.get_env", PMM_SVC_CONFIG, NULL},
    {"Application.fetch_env", PMM_SVC_CONFIG, NULL},

    {NULL, PMM_SVC_NONE, NULL},
};

/* Route registration frameworks — callee resolves to one of these AND
 * has an HTTP method suffix → PMM_SVC_ROUTE_REG.
 * Distinguished from HTTP clients: "gin.GET" registers a handler,
 * "requests.get" makes an outbound HTTP call. */
static const lib_pattern_t route_reg_libraries[] = {
    /* Go */
    {"gin-gonic/gin", PMM_SVC_ROUTE_REG, NULL},
    {"gin.", PMM_SVC_ROUTE_REG, NULL},
    {"go-chi/chi", PMM_SVC_ROUTE_REG, NULL},
    {"chi.", PMM_SVC_ROUTE_REG, NULL},
    {"gorilla/mux", PMM_SVC_ROUTE_REG, NULL},
    {"labstack/echo", PMM_SVC_ROUTE_REG, NULL},
    {"echo.", PMM_SVC_ROUTE_REG, NULL},
    {"gofiber/fiber", PMM_SVC_ROUTE_REG, NULL},
    {"fiber.", PMM_SVC_ROUTE_REG, NULL},
    {"net/http.ServeMux", PMM_SVC_ROUTE_REG, NULL},
    {"http.ServeMux", PMM_SVC_ROUTE_REG, NULL},
    {"httprouter", PMM_SVC_ROUTE_REG, NULL},

    /* JavaScript / TypeScript */
    {"express", PMM_SVC_ROUTE_REG, NULL},
    {"fastify", PMM_SVC_ROUTE_REG, NULL},
    {"koa-router", PMM_SVC_ROUTE_REG, NULL},
    {"hono", PMM_SVC_ROUTE_REG, NULL},
    {"hapi", PMM_SVC_ROUTE_REG, NULL},

    /* Python (non-decorator, e.g., Flask add_url_rule) */
    {"flask", PMM_SVC_ROUTE_REG, NULL},
    {"FastAPI", PMM_SVC_ROUTE_REG, NULL},
    {"starlette", PMM_SVC_ROUTE_REG, NULL},

    /* PHP */
    {"Laravel", PMM_SVC_ROUTE_REG, NULL},
    {"Illuminate.Routing", PMM_SVC_ROUTE_REG, NULL},
    {"Symfony.Routing", PMM_SVC_ROUTE_REG, NULL},

    /* Kotlin */
    {"ktor.server", PMM_SVC_ROUTE_REG, NULL},
    {"ktor.routing", PMM_SVC_ROUTE_REG, NULL},

    /* Rust */
    {"actix-web", PMM_SVC_ROUTE_REG, NULL},
    {"actix_web", PMM_SVC_ROUTE_REG, NULL},
    {"axum", PMM_SVC_ROUTE_REG, NULL},
    {"rocket", PMM_SVC_ROUTE_REG, NULL},

    /* Java */
    {"Spring", PMM_SVC_ROUTE_REG, NULL},
    {"jakarta.ws.rs", PMM_SVC_ROUTE_REG, NULL},

    /* C# */
    {"Microsoft.AspNetCore", PMM_SVC_ROUTE_REG, NULL},
    {"MapGet", PMM_SVC_ROUTE_REG, NULL},
    {"MapPost", PMM_SVC_ROUTE_REG, NULL},

    /* Ruby */
    {"ActionDispatch", PMM_SVC_ROUTE_REG, NULL},
    {"Sinatra", PMM_SVC_ROUTE_REG, NULL},

    /* Elixir */
    {"Phoenix.Router", PMM_SVC_ROUTE_REG, NULL},

    /* Scala */
    {"akka.http.scaladsl.server", PMM_SVC_ROUTE_REG, NULL},
    {"play.api.routing", PMM_SVC_ROUTE_REG, NULL},

    {NULL, PMM_SVC_NONE, NULL},
};

/* gRPC client libraries — protobuf stub invocations */
static const lib_pattern_t grpc_libraries[] = {
    /* Go */
    {"google.golang.org/grpc", PMM_SVC_GRPC, NULL},
    {"grpc.Dial", PMM_SVC_GRPC, NULL},
    {"grpc.NewClient", PMM_SVC_GRPC, NULL},
    {"grpc.DialContext", PMM_SVC_GRPC, NULL},

    /* Python */
    {"grpc.insecure_channel", PMM_SVC_GRPC, NULL},
    {"grpc.secure_channel", PMM_SVC_GRPC, NULL},
    {"grpcio", PMM_SVC_GRPC, NULL},
    {"grpc.aio", PMM_SVC_GRPC, NULL},

    /* Java/Kotlin */
    {"io.grpc", PMM_SVC_GRPC, NULL},
    {"ManagedChannelBuilder", PMM_SVC_GRPC, NULL},
    {"ManagedChannel", PMM_SVC_GRPC, NULL},
    {"newBlockingStub", PMM_SVC_GRPC, NULL},
    {"newFutureStub", PMM_SVC_GRPC, NULL},

    /* C# */
    {"Grpc.Net.Client", PMM_SVC_GRPC, NULL},
    {"GrpcChannel", PMM_SVC_GRPC, NULL},
    {"Grpc.Core", PMM_SVC_GRPC, NULL},

    /* JS/TS */
    {"@grpc/grpc-js", PMM_SVC_GRPC, NULL},
    {"grpc-web", PMM_SVC_GRPC, NULL},

    /* Rust */
    {"tonic", PMM_SVC_GRPC, NULL},

    /* Dart/Flutter */
    {"package:grpc", PMM_SVC_GRPC, NULL},

    {NULL, PMM_SVC_NONE, NULL},
};

/* GraphQL client libraries */
static const lib_pattern_t graphql_libraries[] = {
    /* JS/TS */
    {"graphql-request", PMM_SVC_GRAPHQL, NULL},
    {"@apollo/client", PMM_SVC_GRAPHQL, NULL},
    {"apollo-client", PMM_SVC_GRAPHQL, NULL},
    {"urql", PMM_SVC_GRAPHQL, NULL},
    {"graphql-tag", PMM_SVC_GRAPHQL, NULL},

    /* Python */
    {"gql", PMM_SVC_GRAPHQL, NULL},
    {"sgqlc", PMM_SVC_GRAPHQL, NULL},
    {"graphene", PMM_SVC_GRAPHQL, NULL},

    /* Java */
    {"graphql-java", PMM_SVC_GRAPHQL, NULL},
    {"DgsQueryExecutor", PMM_SVC_GRAPHQL, NULL},

    /* Go */
    {"graphql-go", PMM_SVC_GRAPHQL, NULL},
    {"gqlgen", PMM_SVC_GRAPHQL, NULL},

    /* Ruby */
    {"graphql-ruby", PMM_SVC_GRAPHQL, NULL},

    /* Rust */
    {"async-graphql", PMM_SVC_GRAPHQL, NULL},
    {"juniper", PMM_SVC_GRAPHQL, NULL},

    {NULL, PMM_SVC_NONE, NULL},
};

/* tRPC libraries (TypeScript only) */
static const lib_pattern_t trpc_libraries[] = {
    {"@trpc/server", PMM_SVC_TRPC, NULL},
    {"@trpc/client", PMM_SVC_TRPC, NULL},
    {"@trpc/react-query", PMM_SVC_TRPC, NULL},
    {"createTRPCRouter", PMM_SVC_TRPC, NULL},
    {"createTRPCProxyClient", PMM_SVC_TRPC, NULL},

    {NULL, PMM_SVC_NONE, NULL},
};

/* Method suffix type (used by both route registration and HTTP client tables) */
typedef struct {
    const char *suffix;
    const char *method;
} method_suffix_t;

/* Route registration method suffixes — matched on callee name.
 * These are methods on router objects that register handlers. */
static const method_suffix_t route_reg_suffixes[] = {
    /* HTTP method registrations */
    {".GET", "GET"},
    {".Get", "GET"},
    {".get", "GET"},
    {".POST", "POST"},
    {".Post", "POST"},
    {".post", "POST"},
    {".PUT", "PUT"},
    {".Put", "PUT"},
    {".put", "PUT"},
    {".DELETE", "DELETE"},
    {".Delete", "DELETE"},
    {".delete", "DELETE"},
    {".PATCH", "PATCH"},
    {".Patch", "PATCH"},
    {".patch", "PATCH"},
    /* Handle/HandleFunc (Go stdlib, gorilla) */
    {".Handle", "ANY"},
    {".HandleFunc", "ANY"},
    {".handle", "ANY"},
    /* Framework-specific route registration */
    {".Route", "ANY"},
    {".route", "ANY"},
    {"::get", "GET"},
    {"::post", "POST"},
    {"::put", "PUT"},
    {"::delete", "DELETE"},
    {"::patch", "PATCH"},
    /* Minimal API (C# ASP.NET) */
    {".MapGet", "GET"},
    {".MapPost", "POST"},
    {".MapPut", "PUT"},
    {".MapDelete", "DELETE"},
    /* Router mounting / prefix registration (any method) */
    {".include_router", "ANY"},
    {".mount", "ANY"},
    {".add_url_rule", "ANY"},
    {".register_blueprint", "ANY"},
    {".use", "ANY"},
    {".register", "ANY"},
    {".add_route", "ANY"},
    {".add_api_route", "ANY"},
    {".add_api_websocket_route", "ANY"},
    {NULL, NULL},
};

/* ── HTTP method inference from function/method name suffix ───── */

static const method_suffix_t method_suffixes[] = {
    {".get", "GET"},           {".Get", "GET"},           {".GET", "GET"},
    {".post", "POST"},         {".Post", "POST"},         {".POST", "POST"},
    {".put", "PUT"},           {".Put", "PUT"},           {".PUT", "PUT"},
    {".delete", "DELETE"},     {".Delete", "DELETE"},     {".DELETE", "DELETE"},
    {".patch", "PATCH"},       {".Patch", "PATCH"},       {".PATCH", "PATCH"},
    {".head", "HEAD"},         {".Head", "HEAD"},         {".HEAD", "HEAD"},
    {".options", "OPTIONS"},   {".Options", "OPTIONS"},   {"GetAsync", "GET"},
    {"PostAsync", "POST"},     {"PutAsync", "PUT"},       {"DeleteAsync", "DELETE"},
    {"SendAsync", NULL},       {"getForObject", "GET"},   {"getForEntity", "GET"},
    {"postForObject", "POST"}, {"postForEntity", "POST"}, {NULL, NULL},
};

/* ── Matching implementation ───────────────────────────────────── */

/* Check if any library identifier appears as a substring in the QN.
 * Case-sensitive: "requests" matches "project.venv.requests.api.get"
 * but not "Requests". Library names are specific enough to avoid
 * false positives even with substring matching. */
static const lib_pattern_t *match_qn(const char *qn, const lib_pattern_t *patterns) {
    if (!qn || !qn[0]) {
        return NULL;
    }
    for (int i = 0; patterns[i].library_id != NULL; i++) {
        if (strstr(qn, patterns[i].library_id) != NULL) {
            return &patterns[i];
        }
    }
    return NULL;
}

static bool starts_with_segment(const char *path, const char *segment) {
    if (!path || path[0] != '/' || !segment) {
        return false;
    }
    size_t seg_len = strlen(segment);
    const char *p = path + 1;
    return strncmp(p, segment, seg_len) == 0 && (p[seg_len] == '\0' || p[seg_len] == '/');
}

static bool contains_segment(const char *path, const char *segment) {
    if (!path || !segment) {
        return false;
    }
    size_t seg_len = strlen(segment);
    const char *p = path;
    while ((p = strchr(p, '/')) != NULL) {
        p++;
        if (strncmp(p, segment, seg_len) == 0 && (p[seg_len] == '\0' || p[seg_len] == '/')) {
            return true;
        }
    }
    return false;
}

static bool is_digit_char(char ch) {
    return ch >= '0' && ch <= '9';
}

static bool has_http_route_marker(const char *path) {
    if (starts_with_segment(path, "api") || starts_with_segment(path, "apis") ||
        starts_with_segment(path, "graphql") || starts_with_segment(path, "health") ||
        starts_with_segment(path, "metrics")) {
        return true;
    }
    return path && path[0] == '/' && path[1] == 'v' && is_digit_char(path[2]) &&
           (path[3] == '\0' || path[3] == '/');
}

static bool has_filesystem_root(const char *path) {
    static const char *const roots[] = {"etc",     "root", "var",   "usr",     "home", "tmp",
                                        "private", "opt",  "bin",   "sbin",    "dev",  "proc",
                                        "sys",     "run",  "lib",   "lib64",   "mnt",  "media",
                                        "boot",    "srv",  "Users", "Volumes", NULL};
    for (int i = 0; roots[i]; i++) {
        if (starts_with_segment(path, roots[i])) {
            return true;
        }
    }
    return false;
}

static bool has_hidden_config_segment(const char *path) {
    static const char *const segments[] = {".aws", ".azure", ".config", ".docker", ".env",
                                           ".git", ".gnupg", ".kube",   ".ssh",    NULL};
    for (int i = 0; segments[i]; i++) {
        if (contains_segment(path, segments[i])) {
            return true;
        }
    }
    return false;
}

static bool path_ext_matches(const char *ext, const char *wanted) {
    return ext && wanted && strcmp(ext, wanted) == 0;
}

static bool has_filesystem_extension(const char *path) {
    if (!path) {
        return false;
    }
    const char *end = strpbrk(path, "?#");
    if (!end) {
        end = path + strlen(path);
    }
    const char *last_slash = path;
    for (const char *p = path; p < end; p++) {
        if (*p == '/') {
            last_slash = p;
        }
    }
    const char *dot = NULL;
    for (const char *p = last_slash + 1; p < end; p++) {
        if (*p == '.') {
            dot = p;
        }
    }
    if (!dot || dot == end - 1) {
        return false;
    }
    char ext[32];
    size_t ext_len = (size_t)(end - dot);
    if (ext_len >= sizeof(ext)) {
        return false;
    }
    memcpy(ext, dot, ext_len);
    ext[ext_len] = '\0';

    static const char *const hard_file_exts[] = {
        ".cfg",  ".conf",   ".credentials", ".crt",  ".db",         ".env",
        ".ini",  ".key",    ".pem",         ".pid",  ".properties", ".service",
        ".sock", ".socket", ".sqlite",      ".toml", NULL};
    for (int i = 0; hard_file_exts[i]; i++) {
        if (path_ext_matches(ext, hard_file_exts[i])) {
            return true;
        }
    }
    if ((path_ext_matches(ext, ".json") || path_ext_matches(ext, ".yaml") ||
         path_ext_matches(ext, ".yml") || path_ext_matches(ext, ".xml")) &&
        !has_http_route_marker(path)) {
        return true;
    }
    return false;
}

static bool callee_is_delimiter_or_filesystem_builder(const char *callee_name) {
    if (!callee_name) {
        return false;
    }
    const char *last_dot = strrchr(callee_name, '.');
    const char *last_colon = strstr(callee_name, "::");
    const char *method = callee_name;
    if (last_dot && last_dot[1]) {
        method = last_dot + 1;
    }
    if (last_colon && last_colon[2]) {
        method = last_colon + 2;
    }
    if (strcmp(method, "split") == 0 || strcmp(method, "rsplit") == 0 ||
        strcmp(method, "partition") == 0 || strcmp(method, "join") == 0) {
        return true;
    }
    return strstr(callee_name, "os.path.join") != NULL || strstr(callee_name, "path.join") != NULL;
}

static const char *strip_string_delimiters(const char *literal, char *buf, size_t buf_sz) {
    if (!literal || !literal[0]) {
        return NULL;
    }
    const char *start = literal;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        start++;
    }
    size_t len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' || start[len - 1] == '\n' ||
                       start[len - 1] == '\r')) {
        len--;
    }
    if (len >= 2 && (start[0] == '"' || start[0] == '\'' || start[0] == '`') &&
        start[len - 1] == start[0]) {
        start++;
        len -= 2;
    }
    if (len == 0 || len >= buf_sz) {
        return NULL;
    }
    memcpy(buf, start, len);
    buf[len] = '\0';
    return buf;
}

bool pmm_service_pattern_is_http_route_literal(const char *literal, const char *callee_name) {
    char path_buf[1024];
    const char *path = strip_string_delimiters(literal, path_buf, sizeof(path_buf));
    if (!path || !path[0]) {
        return false;
    }
    if (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0) {
        return true;
    }
    if (strstr(path, "://") != NULL) {
        return false;
    }
    if (path[0] != '/') {
        return false;
    }
    if (callee_is_delimiter_or_filesystem_builder(callee_name)) {
        return false;
    }
    if (has_filesystem_root(path) || has_hidden_config_segment(path) ||
        has_filesystem_extension(path)) {
        return false;
    }
    return true;
}

/* ── Public API ────────────────────────────────────────────────── */

/* Per-worker TLS cache of pmm_service_pattern_match results.
 * The hot path in resolve_file_calls invokes pattern matching for
 * EVERY resolved CALL (via emit_service_edge) — that's 6 pattern-list
 * scans × ~30 patterns × strstr per call. On kubernetes (~600k
 * resolved call edges), the same resolved QN (e.g. "context.Context.
 * Done", "fmt.Errorf", "errors.New") repeats hundreds of thousands of
 * times. A simple TLS hash cache turns the linear scan into one
 * lookup after the first miss for that QN. Lifetime is per-worker for
 * the duration of the parallel_resolve phase. */
#include "foundation/hash_table.h"
#include "foundation/compat.h"

static PMM_TLS CBMHashTable *_svc_cache = NULL;
/* Encode the enum + 1 in the pointer so 0/NULL means "miss". */
static inline void *svc_enum_to_ptr(pmm_svc_kind_t k) {
    return (void *)(uintptr_t)((unsigned)k + 1u);
}
static inline pmm_svc_kind_t svc_ptr_to_enum(void *p) {
    return (pmm_svc_kind_t)((uintptr_t)p - 1u);
}

static void svc_cache_free_key(const char *key, void *val, void *ud) {
    (void)val;
    (void)ud;
    free((char *)key);
}

void pmm_service_pattern_cache_begin(void) {
    if (_svc_cache)
        return; /* idempotent */
    _svc_cache = pmm_ht_create(8192);
}

void pmm_service_pattern_cache_end(void) {
    if (!_svc_cache)
        return;
    pmm_ht_foreach(_svc_cache, svc_cache_free_key, NULL);
    pmm_ht_free(_svc_cache);
    _svc_cache = NULL;
}

void pmm_service_patterns_init(void) {
    /* No-op — tables are static const */
}

bool pmm_service_pattern_is_global_fetch(const char *callee_name) {
    return callee_name != NULL && strcmp(callee_name, "fetch") == 0;
}

pmm_svc_kind_t pmm_service_pattern_match(const char *resolved_qn) {
    if (!resolved_qn || !resolved_qn[0]) {
        return PMM_SVC_NONE;
    }

    if (_svc_cache) {
        void *cached = pmm_ht_get(_svc_cache, resolved_qn);
        if (cached) {
            return svc_ptr_to_enum(cached);
        }
    }

    pmm_svc_kind_t result = PMM_SVC_NONE;
    const lib_pattern_t *p;

    /* Route registration checked first — prevents gin/echo from matching
     * as HTTP clients (both have .get/.post suffixes). */
    if ((p = match_qn(resolved_qn, route_reg_libraries)))
        result = p->kind;
    else if ((p = match_qn(resolved_qn, http_libraries)))
        result = p->kind;
    else if ((p = match_qn(resolved_qn, async_libraries)))
        result = p->kind;
    else if ((p = match_qn(resolved_qn, config_libraries)))
        result = p->kind;
    else if ((p = match_qn(resolved_qn, grpc_libraries)))
        result = p->kind;
    else if ((p = match_qn(resolved_qn, graphql_libraries)))
        result = p->kind;
    else if ((p = match_qn(resolved_qn, trpc_libraries)))
        result = p->kind;

    if (_svc_cache) {
        char *kdup = strdup(resolved_qn);
        if (kdup)
            pmm_ht_set(_svc_cache, kdup, svc_enum_to_ptr(result));
    }
    return result;
}

const char *pmm_service_pattern_http_method(const char *callee_name) {
    if (!callee_name) {
        return NULL;
    }
    for (int i = 0; method_suffixes[i].suffix != NULL; i++) {
        size_t slen = strlen(method_suffixes[i].suffix);
        size_t clen = strlen(callee_name);
        if (clen >= slen && strcmp(callee_name + clen - slen, method_suffixes[i].suffix) == 0) {
            return method_suffixes[i].method;
        }
    }
    return NULL;
}

const char *pmm_service_pattern_route_method(const char *callee_name) {
    if (!callee_name) {
        return NULL;
    }
    size_t clen = strlen(callee_name);
    for (int i = 0; route_reg_suffixes[i].suffix != NULL; i++) {
        size_t slen = strlen(route_reg_suffixes[i].suffix);
        if (clen >= slen && strcmp(callee_name + clen - slen, route_reg_suffixes[i].suffix) == 0) {
            return route_reg_suffixes[i].method;
        }
    }
    return NULL;
}

const char *pmm_service_pattern_broker(const char *resolved_qn) {
    if (!resolved_qn) {
        return NULL;
    }
    const lib_pattern_t *p = match_qn(resolved_qn, async_libraries);
    return p ? p->broker : NULL;
}
